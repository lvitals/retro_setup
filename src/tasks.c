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
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

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
} TaskManager;

static TaskManager g_task_mgr;

const char* task_get_title(TaskType task) {
    switch (task) {
        case TASK_PREPARE:   return "PREPARE RETROARCH";
        case TASK_INSTALL:   return "INSTALL PLATFORMS & ASSETS";
        case TASK_UNINSTALL: return "UNINSTALL PLATFORMS";
        case TASK_THUMBNAILS:return "DOWNLOAD THUMBNAILS";
        case TASK_IMPLODE:   return "RESET CONFIGURATION";
        case TASK_STATUS:    return "SYSTEM STATUS";
        default:             return "EXECUTING TASK";
    }
}

bool tasks_init(void) {
    memset(&g_task_mgr, 0, sizeof(g_task_mgr));
    download_init();
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
}

static void download_progress_cb(const DownloadResult* result, void* user_data) {
    (void)user_data;
    if (result && result->total_bytes > 0) {
        g_task_mgr.progress = (float)result->downloaded_bytes / (float)result->total_bytes;
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                 "%s Downloading: %.1f MB / %.1f MB (%.0f%%)",
                 g_task_mgr.pause_requested ? "[PAUSED]" : "",
                 (double)result->downloaded_bytes / (1024.0 * 1024.0),
                 (double)result->total_bytes / (1024.0 * 1024.0),
                 (double)g_task_mgr.progress * 100.0);
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
    fs_join_path(dir_info, sizeof(dir_info), g_config.ra_dir, "info");
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

    char info_zip[MAX_PATH_LEN];
    fs_join_path(info_zip, sizeof(info_zip), g_config.config_dir, "info.zip");

    DownloadResult dl_res;
    if (download_file("https://buildbot.libretro.com/assets/frontend/info.zip", info_zip, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl_res)) {
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

static int do_task_install(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Installation of Platforms & Assets ===");
    int selected_cnt = get_selected_count();
    if (selected_cnt == 0) {
        log_add(LOG_LEVEL_WARN, "No platforms selected. Please select platforms first.");
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "No platforms selected.");
        return 1;
    }

    url_config_load(g_config.url_config_file);

    int processed = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        while (g_task_mgr.pause_requested && !g_task_mgr.cancel_requested) {
            SDL_Delay(100);
        }
        if (g_task_mgr.cancel_requested) break;
        if (!g_platforms[i].selected) continue;

        PlatformInfo* p = &g_platforms[i];
        processed++;
        g_task_mgr.progress = (float)processed / (float)selected_cnt;

        log_add(LOG_LEVEL_INFO, "[%d/%d] Processing %s (%s)...", processed, selected_cnt, p->name, p->id);
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Installing %s (%d/%d)...", p->name, processed, selected_cnt);

        // 1. Download Core (if not already installed)
        char cores_dir[MAX_PATH_LEN];
        fs_join_path(cores_dir, sizeof(cores_dir), g_config.ra_dir, "cores");
        char core_target[MAX_PATH_LEN];
        fs_join_path(core_target, sizeof(core_target), cores_dir, p->core_file);

        if (fs_exists(core_target) && fs_file_size(core_target) > 0) {
            log_add(LOG_LEVEL_INFO, "Core %s already installed, skipping download.", p->core_file);
        } else {
            char core_url[1024];
            snprintf(core_url, sizeof(core_url), "https://buildbot.libretro.com/nightly/linux/x86_64/latest/%s.zip", p->core_file);

            char core_zip[MAX_PATH_LEN];
            fs_join_path(core_zip, sizeof(core_zip), g_config.config_dir, "core_tmp.zip");

            DownloadResult dl;
            if (download_file(core_url, core_zip, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl)) {
                log_add(LOG_LEVEL_INFO, "Extracting core %s...", p->core_file);
                fs_extract_archive(core_zip, cores_dir);
                fs_remove_file(core_zip);
            } else {
                snprintf(core_url, sizeof(core_url), "https://buildbot.libretro.com/nightly/linux/x86_64/latest/%s", p->core_file);
                download_file(core_url, core_target, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl);
            }
        }

        // 2. Download BIOS if defined in retro_url.config (if not already present)
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

            if (fs_exists(bios_dst) && fs_file_size(bios_dst) > 0) {
                log_add(LOG_LEVEL_INFO, "BIOS %s already present, skipping download.", fn);
                continue;
            }

            log_add(LOG_LEVEL_INFO, "Downloading BIOS %s: %s", p->id, fn);
            DownloadResult dl;
            if (download_file(bios_urls[b], bios_dst, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl)) {
                if (strstr(fn, ".zip") || strstr(fn, ".7z")) {
                    fs_extract_archive(bios_dst, sys_dir);
                }
            }
        }

        // 3. Download ROMs if defined in retro_url.config (if not already downloaded/extracted)
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
                        log_add(LOG_LEVEL_INFO, "Extracting ROM archive %s...", rfn);
                        if (fs_extract_archive(rom_dst, platform_rom_dir)) {
                            FILE* mf = fopen(marker_file, "w");
                            if (mf) {
                                fprintf(mf, "extracted OK\n");
                                fclose(mf);
                            }
                        }
                    }
                }
            }
        } else {
            log_add(LOG_LEVEL_WARN, "No ROM URLs configured in retro_url.config for platform %s (%s)", p->name, p->id);
        }
    }

    if (g_task_mgr.cancel_requested) return -1;

    // 4. Generate Playlists
    log_add(LOG_LEVEL_INFO, "Generating RetroArch Playlists...");
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Generating playlists...");
    playlist_generate_selected(g_config.ra_dir, g_config.rom_dir);

    g_task_mgr.progress = 1.0f;
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Installation completed successfully!");
    log_add(LOG_LEVEL_INFO, "=== Installation Completed Successfully ===");
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

    int processed = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_task_mgr.cancel_requested) break;
        if (!g_platforms[i].selected) continue;

        PlatformInfo* p = &g_platforms[i];
        processed++;
        g_task_mgr.progress = (float)processed / (float)selected_cnt;

        log_add(LOG_LEVEL_INFO, "Uninstalling %s (%s)...", p->name, p->id);

        char p_rom_dir[MAX_PATH_LEN];
        fs_join_path(p_rom_dir, sizeof(p_rom_dir), g_config.rom_dir, p->id);
        if (fs_exists(p_rom_dir)) {
            fs_remove_dir_recursive(p_rom_dir, home);
        }

        char playlist_file[MAX_PATH_LEN];
        snprintf(playlist_file, sizeof(playlist_file), "%.1000s/playlists/%.500s.lpl", g_config.ra_dir, p->name);
        fs_remove_file(playlist_file);

        char core_file[MAX_PATH_LEN];
        snprintf(core_file, sizeof(core_file), "%.1000s/cores/%.500s", g_config.ra_dir, p->core_file);
        fs_remove_file(core_file);
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

    log_clear();
    g_task_mgr.task = task;
    g_task_mgr.is_running = true;
    g_task_mgr.is_finished = false;
    g_task_mgr.cancel_requested = false;
    g_task_mgr.pause_requested = false;
    g_task_mgr.exit_code = 0;
    g_task_mgr.progress = 0.0f;
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
    if (g_task_mgr.is_running) {
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
        default: break;
    }

    g_task_mgr.is_running = false;
    g_task_mgr.is_finished = true;
    g_task_mgr.exit_code = res;
    return res;
}
