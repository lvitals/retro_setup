#include "tasks.h"
#include "download.h"
#include "fs.h"
#include "playlist.h"
#include "platform_data.h"
#include "config_url_parser.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_atomic.h>

typedef struct {
    TaskType task;
    bool is_running;
    bool is_finished;
    bool cancel_requested;
    bool pause_requested;
    int exit_code;
    float progress;
    char status_message[256];
    SDL_Thread* thread;
    SDL_atomic_t work_completed;
    SDL_atomic_t work_total;
} TaskManager;

static TaskManager g_task_mgr;
static SDL_mutex* g_shared_asset_mutex;

static void download_progress_cb(const DownloadResult* result, void* user_data);

static bool ensure_core_info_for_selected(void) {
    char info_dir[MAX_PATH_LEN];
    fs_join_path(info_dir, sizeof(info_dir), g_config.ra_dir, "core_info");
    fs_mkdir_p(info_dir);
    bool missing = false;
    for (int i = 0; i < TOTAL_PLATFORMS; ++i) {
        if (!g_platforms[i].selected) continue;
        if (!g_platforms[i].core_file[0]) continue;
        char name[160], path[MAX_PATH_LEN];
        snprintf(name, sizeof(name), "%s", g_platforms[i].core_file);
        char* suffix = strstr(name, ".so");
        if (suffix) snprintf(suffix, sizeof(name) - (size_t)(suffix - name), ".info");
        fs_join_path(path, sizeof(path), info_dir, name);
        if (!fs_exists(path)) { missing = true; break; }
    }
    if (!missing) return true;

    const char* url = url_config_get_string("CORE_INFO_URL",
                                             "https://buildbot.libretro.com/assets/frontend/info.zip");
    char archive[MAX_PATH_LEN];
    fs_join_path(archive, sizeof(archive), g_config.config_dir, "retroarch-core-info.zip");
    DownloadResult result;
    log_add(LOG_LEVEL_INFO, "[DOWNLOAD] RetroArch core information");
    if (!download_file(url, archive, download_progress_cb, NULL,
                       &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &result)) return false;
    log_add(LOG_LEVEL_INFO, "[EXTRACT] RetroArch core information");
    if (!fs_extract_archive(archive, info_dir)) return false;

    if (g_config.mode == MODE_STEAM) {
        char cores_dir[MAX_PATH_LEN];
        fs_join_path(cores_dir, sizeof(cores_dir), g_config.ra_dir, "cores");
        for (int i = 0; i < TOTAL_PLATFORMS; ++i) {
            if (!g_platforms[i].selected) continue;
            if (!g_platforms[i].core_file[0]) continue;
            char name[160], source[MAX_PATH_LEN], target[MAX_PATH_LEN];
            snprintf(name, sizeof(name), "%s", g_platforms[i].core_file);
            char* suffix = strstr(name, ".so");
            if (suffix) snprintf(suffix, sizeof(name) - (size_t)(suffix - name), ".info");
            fs_join_path(source, sizeof(source), info_dir, name);
            fs_join_path(target, sizeof(target), cores_dir, name);
            if (fs_exists(source)) fs_copy_file(source, target);
        }
    }
    return true;
}

static void configure_retroarch_paths(void) {
    char config_path[MAX_PATH_LEN];
    fs_join_path(config_path, sizeof(config_path), g_config.ra_dir, "retroarch.cfg");
    if (!fs_exists(config_path)) {
        log_add(LOG_LEVEL_WARN, "retroarch.cfg not found; open RetroArch once to create it: %s", config_path);
        return;
    }
    const char* keys[] = {"libretro_directory", "libretro_info_path", "system_directory",
                          "playlist_directory", "thumbnails_directory", "network_on_demand_thumbnails",
                          "quick_menu_show_download_thumbnails"};
    char values[7][4096];
    fs_join_path(values[0], sizeof(values[0]), g_config.ra_dir, "cores");
    fs_join_path(values[1], sizeof(values[1]), g_config.ra_dir, "core_info");
    fs_join_path(values[2], sizeof(values[2]), g_config.ra_dir, "system");
    fs_join_path(values[3], sizeof(values[3]), g_config.ra_dir, "playlists");
    fs_join_path(values[4], sizeof(values[4]), g_config.ra_dir, "thumbnails");
    snprintf(values[5], sizeof(values[5]), "true");
    snprintf(values[6], sizeof(values[6]), "true");
    char temp_path[4096];
    snprintf(temp_path, sizeof(temp_path), "%s.retro_setup.tmp", config_path);
    FILE* input = fopen(config_path, "r");
    FILE* output = fopen(temp_path, "w");
    if (!input || !output) { if (input) fclose(input); if (output) fclose(output); return; }
    bool found[7] = {false};
    char line[4096];
    while (fgets(line, sizeof(line), input)) {
        bool replaced = false;
        for (int i = 0; i < 7; ++i) {
            size_t length = strlen(keys[i]);
            if (!strncmp(line, keys[i], length) && (line[length] == ' ' || line[length] == '=')) {
                fprintf(output, "%s = \"%s\"\n", keys[i], values[i]);
                found[i] = true; replaced = true; break;
            }
        }
        if (!replaced) fputs(line, output);
    }
    for (int i = 0; i < 7; ++i) if (!found[i]) fprintf(output, "%s = \"%s\"\n", keys[i], values[i]);
    fclose(input);
    if (fclose(output) == 0 && rename(temp_path, config_path) == 0)
        log_add(LOG_LEVEL_INFO, "[INSTALL] RetroArch paths configured for %s mode",
                g_config.mode == MODE_STEAM ? "Steam" : "standalone");
    else fs_remove_file(temp_path);
}

typedef struct {
    PlatformInfo* platform;
    int total_selected;
    SDL_sem* sem;
    SDL_atomic_t* processed_counter;
    SDL_atomic_t* error_counter;
} PlatformTaskWorkArgs;

static bool process_platform_downloads(PlatformInfo* p) {
    const char* core_base_url = url_config_get_string("LIBRETRO_CORE_BASE_URL", "https://buildbot.libretro.com/nightly/linux/x86_64/latest");
    bool has_errors = false;

    // 1. Download Core (if a compatible libretro implementation exists)
    char cores_dir[MAX_PATH_LEN];
    fs_join_path(cores_dir, sizeof(cores_dir), g_config.ra_dir, "cores");
    char core_target[MAX_PATH_LEN];
    fs_join_path(core_target, sizeof(core_target), cores_dir, p->core_file);

    SDL_LockMutex(g_shared_asset_mutex);
    if (!p->core_file[0]) {
        log_add(LOG_LEVEL_ERROR,
                "[UNSUPPORTED] %s has no compatible RetroArch/libretro core. "
                "ROMs will be preserved in %s/%s for a compatible external emulator; no fallback core will be used.",
                p->name, g_config.rom_dir, p->id);
        has_errors = true;
    } else if (fs_exists(core_target) && fs_file_size(core_target) > 0) {
        log_add(LOG_LEVEL_INFO, "Core %s already installed, skipping download.", p->core_file);
    } else {
        char core_url[1024];
        snprintf(core_url, sizeof(core_url), "%s/%s.zip", core_base_url, p->core_file);

        char core_zip[MAX_PATH_LEN];
        char core_zip_name[300];
        snprintf(core_zip_name, sizeof(core_zip_name), "%s.zip", p->core_file);
        fs_join_path(core_zip, sizeof(core_zip), g_config.config_dir, core_zip_name);

        DownloadResult dl;
        if (download_file(core_url, core_zip, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl)) {
            log_add(LOG_LEVEL_INFO, "[EXTRACT] %s", core_zip_name);
            if (!fs_extract_archive(core_zip, cores_dir) || !fs_exists(core_target)) {
                log_add(LOG_LEVEL_ERROR, "[FAILED] Core archive did not install %s", p->core_file);
                has_errors = true;
            } else {
                fs_remove_file(core_zip);
                log_add(LOG_LEVEL_INFO, "[DONE] Core installed: %s", p->core_file);
            }
        } else {
            log_add(LOG_LEVEL_ERROR, "ERROR: Failed to download core %s for %s: %s (HTTP status: %ld)", p->core_file, p->name, dl.error, dl.http_status);
            has_errors = true;
        }
    }
    SDL_UnlockMutex(g_shared_asset_mutex);
    if (has_errors) return false;

    // 2. Download BIOS if defined in retro_url.config
    char bios_key[128];
    snprintf(bios_key, sizeof(bios_key), "BIOS_URLS_%.64s", p->id);
    char bios_urls[MAX_URLS_PER_KEY][MAX_URL_LEN];
    int bios_cnt = url_config_get_urls(bios_key, bios_urls, MAX_URLS_PER_KEY);

    char sys_dir[MAX_PATH_LEN];
    fs_join_path(sys_dir, sizeof(sys_dir), g_config.ra_dir, "system");
    fs_mkdir_p(sys_dir);

    for (int b = 0; b < bios_cnt; b++) {
        while (g_task_mgr.pause_requested && !g_task_mgr.cancel_requested) SDL_Delay(100);
        if (g_task_mgr.cancel_requested) break;
        char fn[256];
        fs_get_filename(fn, sizeof(fn), bios_urls[b]);
        if (!fn[0]) snprintf(fn, sizeof(fn), "%.180s_bios.bin", p->id);

        char bios_dst[MAX_PATH_LEN];
        fs_join_path(bios_dst, sizeof(bios_dst), sys_dir, fn);

        SDL_LockMutex(g_shared_asset_mutex);
        log_add(LOG_LEVEL_INFO, "Checking BIOS/data asset %s: %s", p->id, fn);
        DownloadResult dl;
        bool bios_ok = download_file(bios_urls[b], bios_dst, download_progress_cb, NULL,
                                     &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl);
        if (!bios_ok)
            log_add(LOG_LEVEL_ERROR, "ERROR: Failed to download BIOS %s for %s: %s (HTTP %ld)", fn, p->name, dl.error, dl.http_status);
        if (bios_ok && (strstr(fn, ".zip") || strstr(fn, ".7z"))) {
            char archive_error[256] = {0};
            if (!fs_validate_archive(bios_dst, archive_error, sizeof(archive_error))) {
                log_add(LOG_LEVEL_ERROR, "[FAILED] Invalid BIOS archive %s: %s", fn, archive_error);
                fs_remove_file(bios_dst);
                bios_ok = false;
            } else {
                log_add(LOG_LEVEL_INFO, "[EXTRACT] %s", fn);
                if (!fs_extract_archive(bios_dst, sys_dir)) {
                    log_add(LOG_LEVEL_ERROR, "[FAILED] Could not extract BIOS/data archive %s", fn);
                    bios_ok = false;
                }
            }
        }
        SDL_UnlockMutex(g_shared_asset_mutex);
        if (!bios_ok) {
            has_errors = true;
            return false;
        }
    }

    // 3. Download ROMs if defined in retro_url.config
    char rom_urls[MAX_URLS_PER_KEY][MAX_URL_LEN];
    int rom_cnt = 0;

    char rom_key[128];
    snprintf(rom_key, sizeof(rom_key), "ROM_URLS_%.64s", p->id);
    rom_cnt += url_config_get_urls(rom_key, rom_urls + rom_cnt, MAX_URLS_PER_KEY - rom_cnt);

    char rom_dir_key[128];
    snprintf(rom_dir_key, sizeof(rom_dir_key), "ROM_DIR_URLS_%.64s", p->id);
    rom_cnt += url_config_get_urls(rom_dir_key, rom_urls + rom_cnt, MAX_URLS_PER_KEY - rom_cnt);

    if (rom_cnt > 0) {
        char platform_rom_dir[MAX_PATH_LEN];
        fs_join_path(platform_rom_dir, sizeof(platform_rom_dir), g_config.rom_dir, p->id);
        fs_mkdir_p(platform_rom_dir);

        for (int r = 0; r < rom_cnt; r++) {
            while (g_task_mgr.pause_requested && !g_task_mgr.cancel_requested) SDL_Delay(100);
            if (g_task_mgr.cancel_requested) break;
            char rfn[256];
            fs_get_filename(rfn, sizeof(rfn), rom_urls[r]);
            if (!rfn[0]) snprintf(rfn, sizeof(rfn), "%.180s_roms.zip", p->id);

            char marker_file[MAX_PATH_LEN];
            snprintf(marker_file, sizeof(marker_file), "%.1000s/.extracted.%.250s.ok", platform_rom_dir, rfn);

            if (fs_exists(marker_file)) {
                log_add(LOG_LEVEL_INFO, "ROM pack %s already extracted for %s, skipping download.", rfn, p->id);
                continue;
            }

            char rom_dst[MAX_PATH_LEN];
            fs_join_path(rom_dst, sizeof(rom_dst), platform_rom_dir, rfn);

            DownloadResult dl;
            log_add(LOG_LEVEL_INFO, "Downloading ROM pack for %s: %s", p->id, rfn);
            if (download_file(rom_urls[r], rom_dst, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl)) {
                if (strstr(rfn, ".zip") || strstr(rfn, ".7z") || strstr(rfn, ".rar") || strstr(rfn, ".tar")) {
                    log_add(LOG_LEVEL_INFO, "[EXTRACT] %s", rfn);
                    if (fs_extract_archive(rom_dst, platform_rom_dir)) {
                        FILE* mf = fopen(marker_file, "w");
                        if (mf) {
                            fprintf(mf, "extracted OK\n");
                            fclose(mf);
                        }
                        log_add(LOG_LEVEL_INFO, "[DONE] ROM pack installed for %s", p->name);
                    } else {
                        log_add(LOG_LEVEL_ERROR, "[FAILED] Could not extract ROM archive %s", rfn);
                        return false;
                    }
                }
            } else {
                log_add(LOG_LEVEL_ERROR, "ERROR: Failed to download ROM pack %s for %s [%s]: %s (HTTP %ld)", rfn, p->name, rom_urls[r], dl.error, dl.http_status);
                return false;
            }
        }
    } else {
        log_add(LOG_LEVEL_WARN, "No ROM URLs configured in retro_url.config for platform %s (%s)", p->name, p->id);
    }

    return !has_errors;
}

static int SDLCALL platform_install_worker_thread(void* data) {
    PlatformTaskWorkArgs* args = (PlatformTaskWorkArgs*)data;
    if (args && args->platform) {
        if (!process_platform_downloads(args->platform) && args->error_counter) {
            SDL_AtomicAdd(args->error_counter, 1);
        }
        int comp = SDL_AtomicAdd(args->processed_counter, 1) + 1;
        SDL_AtomicSet(&g_task_mgr.work_completed, comp);
        g_task_mgr.progress = (float)comp / (float)(args->total_selected + 1);
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                 "Installed %s (%d/%d completed)...", args->platform->name, comp, args->total_selected);
    }
    if (args && args->sem) {
        SDL_SemPost(args->sem);
    }
    return 0;
}

static int do_task_install(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Installation of Platforms & Assets ===");
    int selected_cnt = get_selected_count();
    if (selected_cnt == 0) {
        log_add(LOG_LEVEL_WARN, "No platforms selected. Please select platforms first.");
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "No platforms selected.");
        return 1;
    }

    url_config_load(g_config.url_config_file);
    download_manager_reset();
    SDL_AtomicSet(&g_task_mgr.work_completed, 0);
    SDL_AtomicSet(&g_task_mgr.work_total, selected_cnt + 1); /* platforms plus playlist generation */

    char install_dir[MAX_PATH_LEN];
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "cores"); fs_mkdir_p(install_dir);
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "system"); fs_mkdir_p(install_dir);
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "playlists"); fs_mkdir_p(install_dir);
    if (!ensure_core_info_for_selected())
        log_add(LOG_LEVEL_WARN, "Core information could not be completely installed.");
    configure_retroarch_paths();

    SDL_atomic_t error_counter;
    SDL_AtomicSet(&error_counter, 0);

    if (g_config.use_parallel_downloads && selected_cnt > 1) {
        int max_workers = (g_config.max_parallel_downloads > 0) ? g_config.max_parallel_downloads : MAX_PARALLEL_DOWNLOADS;
        if (max_workers > MAX_PARALLEL_DOWNLOADS) max_workers = MAX_PARALLEL_DOWNLOADS;
        log_add(LOG_LEVEL_INFO, "Starting multi-threaded parallel installation (Max %d concurrent connections)...", max_workers);

        SDL_sem* sem = SDL_CreateSemaphore(max_workers);
        SDL_atomic_t processed_counter;
        SDL_AtomicSet(&processed_counter, 0);

        SDL_Thread* threads[TOTAL_PLATFORMS];
        PlatformTaskWorkArgs work_args[TOTAL_PLATFORMS];
        memset(threads, 0, sizeof(threads));

        for (int i = 0; i < TOTAL_PLATFORMS; i++) {
            if (g_task_mgr.cancel_requested) break;
            if (!g_platforms[i].selected) continue;

            while (g_task_mgr.pause_requested && !g_task_mgr.cancel_requested) SDL_Delay(100);
            if (g_task_mgr.cancel_requested) break;

            SDL_SemWait(sem); // Throttles to max_workers concurrent active threads

            work_args[i].platform = &g_platforms[i];
            work_args[i].total_selected = selected_cnt;
            work_args[i].sem = sem;
            work_args[i].processed_counter = &processed_counter;
            work_args[i].error_counter = &error_counter;

            char tname[64];
            snprintf(tname, sizeof(tname), "PlatWorker_%.30s", g_platforms[i].id);
            threads[i] = SDL_CreateThread(platform_install_worker_thread, tname, &work_args[i]);
        }

        // Wait for all worker threads to complete
        for (int i = 0; i < TOTAL_PLATFORMS; i++) {
            if (threads[i]) {
                SDL_WaitThread(threads[i], NULL);
            }
        }
        SDL_DestroySemaphore(sem);
    } else {
        int processed = 0;
        for (int i = 0; i < TOTAL_PLATFORMS; i++) {
            while (g_task_mgr.pause_requested && !g_task_mgr.cancel_requested) {
                SDL_Delay(100);
            }
            if (g_task_mgr.cancel_requested) break;
            if (!g_platforms[i].selected) continue;

            PlatformInfo* p = &g_platforms[i];
            processed++;
            g_task_mgr.progress = (float)(processed - 1) / (float)(selected_cnt + 1);

            log_add(LOG_LEVEL_INFO, "[%d/%d] Processing %s (%s)...", processed, selected_cnt, p->name, p->id);
            snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Installing %s (%d/%d)...", p->name, processed, selected_cnt);

            if (!process_platform_downloads(p)) {
                SDL_AtomicAdd(&error_counter, 1);
            }
            SDL_AtomicSet(&g_task_mgr.work_completed, processed);
            g_task_mgr.progress = (float)processed / (float)(selected_cnt + 1);
        }
    }

    if (g_task_mgr.cancel_requested) return -1;

    int err_count = SDL_AtomicGet(&error_counter);
    if (err_count > 0) {
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Installation stopped with %d error(s).", err_count);
        log_add(LOG_LEVEL_ERROR, "=== Installation Stopped with %d Error(s); dependent playlist generation skipped ===", err_count);
        return 1;
    }

    // Generate Playlists only after every required asset was installed.
    log_add(LOG_LEVEL_INFO, "Generating RetroArch Playlists...");
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Generating playlists...");
    playlist_generate_selected(g_config.ra_dir, g_config.rom_dir);
    SDL_AtomicSet(&g_task_mgr.work_completed, selected_cnt + 1);

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Installation completed successfully!");
    log_add(LOG_LEVEL_INFO, "=== Installation Completed Successfully ===");
    return 0;
}

const char* task_get_title(TaskType task) {
    switch (task) {
        case TASK_PREPARE:   return "PREPARE RETROARCH";
        case TASK_INSTALL:   return "INSTALL PLATFORMS & ASSETS";
        case TASK_UNINSTALL: return "UNINSTALL PLATFORMS";
        case TASK_THUMBNAILS:return "DOWNLOAD THUMBNAILS";
        case TASK_IMPLODE:   return "RESET CONFIGURATION";
        case TASK_STATUS:    return "SYSTEM STATUS";
        case TASK_INSTALLATION_DIAGNOSTIC: return "INSTALLATION DIAGNOSTIC";
        default:             return "EXECUTING TASK";
    }
}

bool tasks_init(void) {
    memset(&g_task_mgr, 0, sizeof(g_task_mgr));
    download_init();
    g_shared_asset_mutex = SDL_CreateMutex();
    log_init();
    return true;
}

void tasks_cleanup(void) {
    task_cancel();
    if (g_task_mgr.thread) {
        SDL_WaitThread(g_task_mgr.thread, NULL);
        g_task_mgr.thread = NULL;
    }
    download_cleanup();
    if (g_shared_asset_mutex) SDL_DestroyMutex(g_shared_asset_mutex);
    g_shared_asset_mutex = NULL;
}

static void download_progress_cb(const DownloadResult* result, void* user_data) {
    (void)user_data;
    if (result) {
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                 "%s %s (%.1f MB/s)", download_state_name(result->state),
                 g_task_mgr.pause_requested ? "- paused" : "- transfer in progress",
                 result->speed_bytes_per_sec / (1024.0 * 1024.0));
    }
}

static bool check_retroarch_installed(char* out_bin, size_t out_size) {
    const char* paths[] = {
        "/usr/bin/retroarch",
        "/usr/local/bin/retroarch",
        "~/.local/bin/retroarch",
        "~/.var/app/org.libretro.RetroArch/current/active/export/bin/org.libretro.RetroArch",
        NULL
    };

    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    for (int i = 0; paths[i]; i++) {
        char full[MAX_PATH_LEN];
        if (paths[i][0] == '~') {
            snprintf(full, sizeof(full), "%.1000s%.500s", home, paths[i] + 1);
        } else {
            snprintf(full, sizeof(full), "%.1500s", paths[i]);
        }
        if (fs_exists(full)) {
            if (out_bin && out_size > 0) {
                snprintf(out_bin, out_size, "%s", full);
            }
            return true;
        }
    }
    return false;
}

static int do_task_prepare(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Prepare RetroArch ===");
    g_task_mgr.progress = 0.1f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Checking local RetroArch installation...");

    char ra_bin[MAX_PATH_LEN];
    if (check_retroarch_installed(ra_bin, sizeof(ra_bin))) {
        log_add(LOG_LEVEL_INFO, "Detected RetroArch binary at: %s", ra_bin);
    } else {
        log_add(LOG_LEVEL_WARN, "RetroArch binary not found in standard system paths.");
        log_add(LOG_LEVEL_WARN, "Install RetroArch using your distribution package manager when convenient.");
    }

    log_add(LOG_LEVEL_INFO, "Creating user RetroArch directories at %s...", g_config.ra_dir);
    char dir_cores[MAX_PATH_LEN], dir_info[MAX_PATH_LEN], dir_sys[MAX_PATH_LEN];
    char dir_playlists[MAX_PATH_LEN], dir_thumbs[MAX_PATH_LEN], dir_db[MAX_PATH_LEN];

    fs_join_path(dir_cores, sizeof(dir_cores), g_config.ra_dir, "cores");
    fs_join_path(dir_info, sizeof(dir_info), g_config.ra_dir, "core_info");
    fs_join_path(dir_sys, sizeof(dir_sys), g_config.ra_dir, "system");
    fs_join_path(dir_playlists, sizeof(dir_playlists), g_config.ra_dir, "playlists");
    fs_join_path(dir_thumbs, sizeof(dir_thumbs), g_config.ra_dir, "thumbnails");
    fs_join_path(dir_db, sizeof(dir_db), g_config.ra_dir, "database/rdb");

    fs_mkdir_p(dir_cores);
    fs_mkdir_p(dir_info);
    fs_mkdir_p(dir_sys);
    fs_mkdir_p(dir_playlists);
    fs_mkdir_p(dir_thumbs);
    fs_mkdir_p(dir_db);
    fs_mkdir_p(g_config.rom_dir);
    fs_mkdir_p(g_config.config_dir);

    g_task_mgr.progress = 0.3f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Downloading Libretro Core Info files...");

    const char* info_url = url_config_get_string("CORE_INFO_URL", "https://buildbot.libretro.com/assets/frontend/info.zip");

    char info_zip[MAX_PATH_LEN];
    fs_join_path(info_zip, sizeof(info_zip), g_config.config_dir, "info.zip");

    DownloadResult dl_res;
    if (download_file(info_url, info_zip, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl_res)) {
        log_add(LOG_LEVEL_INFO, "Extracting Core Info files...");
        fs_extract_archive(info_zip, dir_info);
        fs_remove_file(info_zip);
    } else {
        log_add(LOG_LEVEL_WARN, "Core Info download skipped or failed.");
    }

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "RetroArch directories prepared successfully.");
    log_add(LOG_LEVEL_INFO, "=== Prepare Completed Successfully ===");
    return 0;
}

static int do_task_uninstall(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Platform Uninstallation ===");
    int selected_cnt = get_selected_count();
    if (selected_cnt == 0) {
        log_add(LOG_LEVEL_WARN, "No platforms selected for uninstallation.");
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "No platforms selected.");
        return 1;
    }

    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    SDL_AtomicSet(&g_task_mgr.work_completed, 0);
    SDL_AtomicSet(&g_task_mgr.work_total, selected_cnt);
    int processed = 0;
    int failures = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_task_mgr.cancel_requested) break;
        if (!g_platforms[i].selected) continue;

        PlatformInfo* p = &g_platforms[i];
        processed++;
        g_task_mgr.progress = (float)(processed - 1) / (float)selected_cnt;
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                 "Uninstalling %s (%d/%d)...", p->name, processed, selected_cnt);

        log_add(LOG_LEVEL_INFO, "Uninstalling %s (%s)...", p->name, p->id);

        char p_rom_dir[MAX_PATH_LEN];
        fs_join_path(p_rom_dir, sizeof(p_rom_dir), g_config.rom_dir, p->id);
        if (fs_exists(p_rom_dir)) {
            if (fs_remove_dir_recursive(p_rom_dir, g_config.rom_dir))
                log_add(LOG_LEVEL_INFO, "[REMOVE] ROM directory: %s", p_rom_dir);
            else { log_add(LOG_LEVEL_ERROR, "[FAILED] Could not remove ROM directory: %s", p_rom_dir); failures++; }
        }

        char playlist_file[MAX_PATH_LEN];
        snprintf(playlist_file, sizeof(playlist_file), "%.1000s/playlists/%.500s.lpl", g_config.ra_dir, p->name);
        if (fs_exists(playlist_file) && fs_remove_file(playlist_file))
            log_add(LOG_LEVEL_INFO, "[REMOVE] Playlist: %s", playlist_file);

        char thumb_dir[MAX_PATH_LEN];
        snprintf(thumb_dir, sizeof(thumb_dir), "%.1000s/thumbnails/%.500s", g_config.ra_dir, p->name);
        if (fs_exists(thumb_dir)) {
            if (fs_remove_dir_recursive(thumb_dir, g_config.ra_dir))
                log_add(LOG_LEVEL_INFO, "[REMOVE] Thumbnails: %s", thumb_dir);
            else failures++;
        }

        char core_file[MAX_PATH_LEN];
        snprintf(core_file, sizeof(core_file), "%.1000s/cores/%.500s", g_config.ra_dir, p->core_file);
        bool core_still_needed = false;
        for (int k = 0; k < TOTAL_PLATFORMS; ++k) {
            if (!g_platforms[k].selected && strcmp(g_platforms[k].core_file, p->core_file) == 0) {
                core_still_needed = true;
                break;
            }
        }
        if (!core_still_needed && fs_exists(core_file) && fs_remove_file(core_file))
            log_add(LOG_LEVEL_INFO, "[REMOVE] Core: %s", core_file);
        else if (core_still_needed)
            log_add(LOG_LEVEL_INFO, "[KEEP] Shared core still used: %s", p->core_file);

        char info_name[160], info_file[MAX_PATH_LEN];
        snprintf(info_name, sizeof(info_name), "%s", p->core_file);
        char* so = strstr(info_name, ".so");
        if (so) snprintf(so, sizeof(info_name) - (size_t)(so - info_name), ".info");
        snprintf(info_file, sizeof(info_file), "%.1000s/info/%.500s", g_config.ra_dir, info_name);
        fs_remove_file(info_file);
        snprintf(info_file, sizeof(info_file), "%.1000s/core_info/%.500s", g_config.ra_dir, info_name);
        fs_remove_file(info_file);

        char bios_copy[sizeof(p->bios_files)];
        snprintf(bios_copy, sizeof(bios_copy), "%s", p->bios_files);
        char* save = NULL;
        for (char* bios = strtok_r(bios_copy, " ,;", &save); bios; bios = strtok_r(NULL, " ,;", &save)) {
            bool bios_still_needed = false;
            for (int k = 0; k < TOTAL_PLATFORMS; ++k) {
                if (!g_platforms[k].selected && strstr(g_platforms[k].bios_files, bios)) {
                    bios_still_needed = true;
                    break;
                }
            }
            if (bios_still_needed) {
                log_add(LOG_LEVEL_INFO, "[KEEP] Shared BIOS still used: %s", bios);
                continue;
            }
            char bios_path[MAX_PATH_LEN];
            fs_join_path(bios_path, sizeof(bios_path), g_config.ra_dir, "system");
            char full_bios[MAX_PATH_LEN];
            fs_join_path(full_bios, sizeof(full_bios), bios_path, bios);
            if (fs_exists(full_bios) && fs_remove_file(full_bios))
                log_add(LOG_LEVEL_INFO, "[REMOVE] BIOS: %s", full_bios);
        }
        SDL_AtomicSet(&g_task_mgr.work_completed, processed);
        g_task_mgr.progress = (float)processed / (float)selected_cnt;
    }

    if (g_task_mgr.cancel_requested) return -1;
    if (failures) {
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Uninstallation completed with %d error(s).", failures);
        log_add(LOG_LEVEL_ERROR, "=== Uninstallation Finished with %d Error(s) ===", failures);
        return 1;
    }
    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Uninstallation completed!");
    log_add(LOG_LEVEL_INFO, "=== Uninstallation Completed ===");
    return 0;
}

static int do_task_thumbnails(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Thumbnail Download ===");
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Checking playlists for thumbnails...");

    int processed = 0;
    int selected_cnt = get_selected_count();
    if (selected_cnt == 0) selected_cnt = 1;

    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_task_mgr.cancel_requested) break;
        if (!g_platforms[i].selected) continue;

        PlatformInfo* p = &g_platforms[i];
        processed++;
        g_task_mgr.progress = (float)processed / (float)selected_cnt;

        log_add(LOG_LEVEL_INFO, "Checking thumbnails for %s (%d/%d)...", p->name, processed, selected_cnt);
        char thumb_dir[MAX_PATH_LEN];
        snprintf(thumb_dir, sizeof(thumb_dir), "%.1000s/thumbnails/%.500s/Named_Boxarts", g_config.ra_dir, p->name);
        fs_mkdir_p(thumb_dir);
    }

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Thumbnails sync completed!");
    log_add(LOG_LEVEL_INFO, "=== Thumbnails Completed ===");
    return 0;
}

static int do_task_implode(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Reset Configuration ===");

    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    if (fs_exists(g_config.config_dir)) {
        log_add(LOG_LEVEL_INFO, "Removing configuration directory: %s", g_config.config_dir);
        fs_remove_dir_recursive(g_config.config_dir, home);
    }

    reset_all_selections(false);

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Reset configuration complete.");
    log_add(LOG_LEVEL_INFO, "=== Reset Complete ===");
    return 0;
}

#include "diagnostic.h"

static int do_task_status(void) {
    log_add(LOG_LEVEL_INFO, "=== Checking System Diagnostic Status ===");
    SystemDiagnosticReport rep;
    diagnostic_run_scan(&rep);

    log_add(LOG_LEVEL_INFO, "OS Distribution       : %s", rep.os_distro);
    log_add(LOG_LEVEL_INFO, "Architecture          : %s", rep.os_arch);
    log_add(LOG_LEVEL_INFO, "Retro Setup Version   : %s", rep.app_version);
    log_add(LOG_LEVEL_INFO, "RetroArch Binary      : %s (%s)", rep.retroarch_binary_found ? "FOUND" : "NOT FOUND", rep.retroarch_binary_path);
    log_add(LOG_LEVEL_INFO, "RetroArch Mode        : %s", (g_config.mode == MODE_STEAM) ? "Steam RetroArch" : "Standalone");
    log_add(LOG_LEVEL_INFO, "Target Directory      : %s", rep.retroarch_target_dir);

    char free_str[128], total_str[128];
    diagnostic_format_size(rep.disk_free_bytes, free_str, sizeof(free_str));
    diagnostic_format_size(rep.disk_total_bytes, total_str, sizeof(total_str));
    log_add(LOG_LEVEL_INFO, "Free Disk Space       : %s free / %s total", free_str, total_str);

    log_add(LOG_LEVEL_INFO, "Cores Installed       : %d (%d info files)", rep.cores_installed_count, rep.core_info_count);
    log_add(LOG_LEVEL_INFO, "System BIOS Files     : %d found, %d missing", rep.bios_found_total, rep.bios_missing_total);
    log_add(LOG_LEVEL_INFO, "ROM Library Files     : %d files", rep.rom_files_total);
    log_add(LOG_LEVEL_INFO, "Selected Platforms    : %d / %d (%d Ready, %d Incomplete)", rep.platforms_selected, TOTAL_PLATFORMS, rep.platforms_ready, rep.platforms_incomplete);

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Status check complete.");
    return 0;
}

static bool catalog_has_playlist_name(const char* filename) {
    for (int i = 0; i < TOTAL_PLATFORMS; ++i) {
        char expected[256];
        snprintf(expected, sizeof(expected), "%s.lpl", g_platforms[i].name);
        if (!strcmp(filename, expected)) return true;
    }
    return false;
}

static bool catalog_has_platform_id(const char* id) {
    for (int i = 0; i < TOTAL_PLATFORMS; ++i) if (!strcmp(id, g_platforms[i].id)) return true;
    return false;
}

static int do_task_installation_diagnostic(void) {
    log_add(LOG_LEVEL_INFO, "=== Installation Diagnostic: %s ===",
            g_config.mode == MODE_STEAM ? "STEAM" : "STANDALONE");
    url_config_load(g_config.url_config_file);
    int url_total = 0;
    for (int i = 0; i < url_config_get_entry_count(); ++i) {
        const UrlArrayEntry* entry = url_config_get_entry(i);
        if (entry) url_total += entry->url_count;
    }
    int total = TOTAL_PLATFORMS + url_total + 1;
    SDL_AtomicSet(&g_task_mgr.work_total, total);
    SDL_AtomicSet(&g_task_mgr.work_completed, 0);
    int done = 0, warnings = 0, errors = 0;
    int obsolete_count = 0, incomplete_count = 0, missing_core_count = 0;

    char playlists[MAX_PATH_LEN];
    fs_join_path(playlists, sizeof(playlists), g_config.ra_dir, "playlists");
    DIR* directory = opendir(playlists);
    if (directory) {
        struct dirent* item;
        while ((item = readdir(directory)) != NULL) {
            size_t len = strlen(item->d_name);
            if (len > 4 && !strcmp(item->d_name + len - 4, ".lpl") && !catalog_has_playlist_name(item->d_name)) {
                log_add(LOG_LEVEL_WARN, "[OBSOLETE] Playlist is not in the current catalog: %s", item->d_name);
                log_add(LOG_LEVEL_INFO, "[ACTION] Review and remove this playlist from the active %s installation.",
                        g_config.mode == MODE_STEAM ? "Steam" : "standalone");
                warnings++;
                obsolete_count++;
            }
        }
        closedir(directory);
    }
    directory = opendir(g_config.rom_dir);
    if (directory) {
        struct dirent* item;
        while ((item = readdir(directory)) != NULL) {
            if (item->d_name[0] == '.') continue;
            char path[MAX_PATH_LEN];
            fs_join_path(path, sizeof(path), g_config.rom_dir, item->d_name);
            if (fs_is_dir(path) && !catalog_has_platform_id(item->d_name)) {
                log_add(LOG_LEVEL_WARN, "[OBSOLETE] ROM directory is not in the current catalog: %s", item->d_name);
                log_add(LOG_LEVEL_INFO, "[ACTION] Back up wanted ROMs, then remove or migrate this directory.");
                warnings++;
                obsolete_count++;
            }
        }
        closedir(directory);
    }

    for (int i = 0; i < TOTAL_PLATFORMS && !g_task_mgr.cancel_requested; ++i) {
        PlatformInfo* p = &g_platforms[i];
        char core[MAX_PATH_LEN], roms[MAX_PATH_LEN], playlist[MAX_PATH_LEN];
        snprintf(core, sizeof(core), "%.1000s/cores/%.500s", g_config.ra_dir, p->core_file);
        fs_join_path(roms, sizeof(roms), g_config.rom_dir, p->id);
        snprintf(playlist, sizeof(playlist), "%.1000s/playlists/%.500s.lpl", g_config.ra_dir, p->name);
        bool installed = fs_exists(core) || fs_is_dir(roms) || fs_exists(playlist) || p->selected;
        if (installed) {
            bool healthy = p->core_file[0] && fs_is_file(core) && fs_file_size(core) > 0;
            if (healthy && fs_is_dir(roms) && !fs_exists(playlist)) healthy = false;
            log_add(healthy ? LOG_LEVEL_INFO : LOG_LEVEL_WARN, "[%s] %s | core:%s roms:%s playlist:%s",
                    healthy ? "HEALTHY" : "INCOMPLETE", p->name,
                    fs_exists(core) ? "OK" : "MISSING", fs_is_dir(roms) ? "OK" : "NONE",
                    fs_exists(playlist) ? "OK" : "MISSING");
            if (!healthy) {
                warnings++;
                incomplete_count++;
                if (!fs_exists(core)) {
                    missing_core_count++;
                    log_add(LOG_LEVEL_INFO, "[ACTION] Select %s and run INSTALL PLATFORMS & ASSETS to install its core.", p->name);
                } else if (fs_is_dir(roms) && !fs_exists(playlist)) {
                    log_add(LOG_LEVEL_INFO, "[ACTION] Run INSTALL PLATFORMS & ASSETS again to regenerate the playlist.");
                }
            }
        } else log_add(LOG_LEVEL_INFO, "[NOT INSTALLED] %s", p->name);
        done++;
        SDL_AtomicSet(&g_task_mgr.work_completed, done);
        g_task_mgr.progress = (float)done / (float)total;
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Scanning platform %d/%d: %s", i + 1, TOTAL_PLATFORMS, p->name);
    }

    for (int i = 0; i < url_config_get_entry_count() && !g_task_mgr.cancel_requested; ++i) {
        const UrlArrayEntry* entry = url_config_get_entry(i);
        if (!entry) continue;
        for (int u = 0; u < entry->url_count && !g_task_mgr.cancel_requested; ++u) {
            long http = 0; curl_off_t size = -1; char error[256] = {0};
            snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Testing URL %d/%d: %s", done - TOTAL_PLATFORMS + 1, url_total, entry->key);
            bool ok = download_check_url(entry->urls[u], &http, &size, error, sizeof(error));
            if (ok) log_add(LOG_LEVEL_INFO, "[URL OK] %s | HTTP %ld | %s", entry->key, http, size >= 0 ? "SIZE KNOWN" : "SIZE UNKNOWN");
            else {
                log_add(LOG_LEVEL_ERROR, "[URL FAILED] %s | HTTP %ld | %s", entry->key, http, error);
                log_add(LOG_LEVEL_INFO, http == 404
                        ? "[ACTION] Replace or remove the unavailable URL in retro_url.config."
                        : "[ACTION] Check connectivity and retry; if persistent, update this entry in retro_url.config.");
                errors++;
            }
            done++;
            SDL_AtomicSet(&g_task_mgr.work_completed, done);
            g_task_mgr.progress = (float)done / (float)total;
        }
    }
    done++;
    SDL_AtomicSet(&g_task_mgr.work_completed, done);
    if (g_task_mgr.cancel_requested) return -1;
    g_task_mgr.progress = 1.0f;
    log_add(LOG_LEVEL_INFO, "=== DIAGNOSTIC REPORT ===");
    log_add(LOG_LEVEL_INFO, "Mode: %s", g_config.mode == MODE_STEAM ? "Steam" : "Standalone");
    log_add(LOG_LEVEL_INFO, "Target: %s", g_config.ra_dir);
    log_add(LOG_LEVEL_INFO, "Platforms: %d catalogued | %d incomplete | %d missing core", TOTAL_PLATFORMS, incomplete_count, missing_core_count);
    log_add(LOG_LEVEL_INFO, "Obsolete artifacts: %d | URL failures: %d", obsolete_count, errors);
    if (!warnings && !errors) log_add(LOG_LEVEL_INFO, "[ACTION] No corrective action is required.");
    else log_add(LOG_LEVEL_WARN, "[ACTION] Resolve URL failures first, run INSTALL for incomplete platforms, then review obsolete artifacts.");
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
             "Diagnostic complete: %d warning(s), %d failed URL(s).", warnings, errors);
    log_add(errors ? LOG_LEVEL_ERROR : (warnings ? LOG_LEVEL_WARN : LOG_LEVEL_INFO),
            "=== Diagnostic Complete: %d warning(s), %d failed URL(s) ===", warnings, errors);
    return errors ? 1 : 0;
}

static int SDLCALL task_worker_thread(void* data) {
    TaskType task = (TaskType)(uintptr_t)data;
    int res = 0;

    switch (task) {
        case TASK_PREPARE:   res = do_task_prepare(); break;
        case TASK_INSTALL:   res = do_task_install(); break;
        case TASK_UNINSTALL: res = do_task_uninstall(); break;
        case TASK_THUMBNAILS:res = do_task_thumbnails(); break;
        case TASK_IMPLODE:   res = do_task_implode(); break;
        case TASK_STATUS:    res = do_task_status(); break;
        case TASK_INSTALLATION_DIAGNOSTIC: res = do_task_installation_diagnostic(); break;
        default: break;
    }

    g_task_mgr.exit_code = res;
    g_task_mgr.is_running = false;
    g_task_mgr.is_finished = true;
    return res;
}

bool task_start_async(TaskType task, const char* extra_args) {
    (void)extra_args;
    if (g_task_mgr.is_running) return false;

    if (g_task_mgr.thread) {
        SDL_WaitThread(g_task_mgr.thread, NULL);
        g_task_mgr.thread = NULL;
    }

    log_clear();
    g_task_mgr.task = task;
    g_task_mgr.is_running = true;
    g_task_mgr.is_finished = false;
    g_task_mgr.cancel_requested = false;
    g_task_mgr.pause_requested = false;
    g_task_mgr.exit_code = 0;
    g_task_mgr.progress = 0.0f;
    SDL_AtomicSet(&g_task_mgr.work_completed, 0);
    SDL_AtomicSet(&g_task_mgr.work_total, 1);
    download_manager_reset();
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Starting %s...", task_get_title(task));

    g_task_mgr.thread = SDL_CreateThread(task_worker_thread, "TaskWorker", (void*)(uintptr_t)task);
    if (!g_task_mgr.thread) {
        log_add(LOG_LEVEL_ERROR, "Failed to create SDL task thread.");
        g_task_mgr.is_running = false;
        g_task_mgr.is_finished = true;
        g_task_mgr.exit_code = 1;
        return false;
    }
    return true;
}

void task_cancel(void) {
    if (g_task_mgr.is_running && !g_task_mgr.cancel_requested) {
        g_task_mgr.cancel_requested = true;
        log_add(LOG_LEVEL_WARN, "Task cancellation requested by user.");
    }
}

void task_toggle_pause(void) {
    if (g_task_mgr.is_running && !g_task_mgr.is_finished) {
        g_task_mgr.pause_requested = !g_task_mgr.pause_requested;
        log_add(LOG_LEVEL_INFO, "Task %s by user.", g_task_mgr.pause_requested ? "PAUSED" : "RESUMED");
    }
}

bool task_is_paused(void) {
    return g_task_mgr.pause_requested;
}

bool task_is_running(void) { return g_task_mgr.is_running; }
bool task_is_finished(void) { return g_task_mgr.is_finished; }
int task_get_exit_code(void) { return g_task_mgr.exit_code; }
float task_get_progress(void) { return g_task_mgr.progress; }
const char* task_get_status_message(void) { return g_task_mgr.status_message; }
void task_get_work_counts(int* completed, int* total) {
    if (completed) *completed = SDL_AtomicGet(&g_task_mgr.work_completed);
    if (total) *total = SDL_AtomicGet(&g_task_mgr.work_total);
}
void task_get_download_snapshot(DownloadManagerSnapshot* snapshot) {
    download_manager_snapshot(snapshot);
}

int task_run_sync(TaskType task) {
    log_clear();
    g_task_mgr.task = task;
    g_task_mgr.is_running = true;
    g_task_mgr.is_finished = false;
    g_task_mgr.cancel_requested = false;
    g_task_mgr.pause_requested = false;

    int res = 0;
    switch (task) {
        case TASK_PREPARE:   res = do_task_prepare(); break;
        case TASK_INSTALL:   res = do_task_install(); break;
        case TASK_UNINSTALL: res = do_task_uninstall(); break;
        case TASK_THUMBNAILS:res = do_task_thumbnails(); break;
        case TASK_IMPLODE:   res = do_task_implode(); break;
        case TASK_STATUS:    res = do_task_status(); break;
        case TASK_INSTALLATION_DIAGNOSTIC: res = do_task_installation_diagnostic(); break;
        default: break;
    }

    g_task_mgr.is_running = false;
    g_task_mgr.is_finished = true;
    g_task_mgr.exit_code = res;
    return res;
}
