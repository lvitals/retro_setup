#include "tasks.h"
#include "download.h"
#include "fs.h"
#include "playlist.h"
#include "rom_catalog.h"
#include "steam_shortcuts.h"
#include "platform_data.h"
#include "config_url_parser.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/utsname.h>
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

static bool ensure_retroarch_databases(void) {
    char database_dir[MAX_PATH_LEN];
    fs_join_path(database_dir, sizeof(database_dir), g_config.ra_dir, "database/rdb");
    fs_mkdir_p(database_dir);
    DIR* dir = opendir(database_dir);
    bool found = false;
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir))) {
            size_t length = strlen(entry->d_name);
            if (length > 4 && !strcasecmp(entry->d_name + length - 4, ".rdb")) { found = true; break; }
        }
        closedir(dir);
    }
    if (found) return true;

    const char* url = url_config_get_string("DATABASE_RDB_URL",
        "https://buildbot.libretro.com/assets/frontend/database-rdb.zip");
    char archive[MAX_PATH_LEN];
    fs_join_path(archive, sizeof(archive), g_config.config_dir, "database-rdb.zip");
    DownloadResult result;
    log_add(LOG_LEVEL_INFO, "[DOWNLOAD] RetroArch game-name databases");
    if (!download_file(url, archive, download_progress_cb, NULL,
                       &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &result)) return false;
    log_add(LOG_LEVEL_INFO, "[EXTRACT] RetroArch game-name databases");
    if (!fs_extract_archive(archive, database_dir)) return false;
    fs_remove_file(archive);
    return true;
}

static void cleanup_obsolete_managed_playlists(void) {
    char playlist_dir[MAX_PATH_LEN];
    fs_join_path(playlist_dir, sizeof(playlist_dir), g_config.ra_dir, "playlists");
    DIR* dir = opendir(playlist_dir);
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir))) {
        size_t name_length = strlen(entry->d_name);
        if (name_length <= 4 || strcasecmp(entry->d_name + name_length - 4, ".lpl")) continue;
        char playlist[MAX_PATH_LEN];
        fs_join_path(playlist, sizeof(playlist), playlist_dir, entry->d_name);
        FILE* file = fopen(playlist, "r");
        if (!file) continue;
        char line[4096], marker[MAX_PATH_LEN];
        snprintf(marker, sizeof(marker), "%.1800s/", g_config.rom_dir);
        char obsolete_id[128] = {0};
        while (fgets(line, sizeof(line), file)) {
            char* path = strstr(line, marker);
            if (!path) continue;
            path += strlen(marker);
            size_t length = strcspn(path, "/\\\"");
            if (!length || length >= sizeof(obsolete_id)) break;
            memcpy(obsolete_id, path, length); obsolete_id[length] = 0;
            if (get_platform_index_by_id(obsolete_id) >= 0) obsolete_id[0] = 0;
            break;
        }
        fclose(file);
        if (!obsolete_id[0]) continue;

        char playlist_name[512];
        size_t copy = name_length - 4;
        if (copy >= sizeof(playlist_name)) copy = sizeof(playlist_name) - 1;
        memcpy(playlist_name, entry->d_name, copy); playlist_name[copy] = 0;
        if (fs_remove_file(playlist))
            log_add(LOG_LEVEL_INFO, "Removed obsolete managed playlist: %s (platform id: %s)", playlist, obsolete_id);
        char thumbnail_root[MAX_PATH_LEN], thumbnail_dir[MAX_PATH_LEN];
        fs_join_path(thumbnail_root, sizeof(thumbnail_root), g_config.ra_dir, "thumbnails");
        fs_join_path(thumbnail_dir, sizeof(thumbnail_dir), thumbnail_root, playlist_name);
        if (fs_is_dir(thumbnail_dir) && fs_remove_dir_recursive(thumbnail_dir, thumbnail_root))
            log_add(LOG_LEVEL_INFO, "Removed obsolete thumbnail directory: %s", thumbnail_dir);
    }
    closedir(dir);
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

static bool get_core_base_url(char* out, size_t out_size) {
    const char* configured = url_config_get_string("LIBRETRO_CORE_BASE_URL", NULL);
    if (configured && *configured) {
        snprintf(out, out_size, "%s", configured);
        return true;
    }

    struct utsname host;
    if (uname(&host) != 0) return false;

    const char* buildbot_arch = NULL;
    if (strcmp(host.machine, "x86_64") == 0 || strcmp(host.machine, "amd64") == 0)
        buildbot_arch = "x86_64";
    else if (strcmp(host.machine, "aarch64") == 0 || strcmp(host.machine, "arm64") == 0)
        buildbot_arch = "aarch64";

    if (!buildbot_arch) return false;
    snprintf(out, out_size, "https://buildbot.libretro.com/nightly/linux/%s/latest", buildbot_arch);
    return true;
}

static bool process_platform_downloads(PlatformInfo* p) {
    char core_base_url[MAX_URL_LEN];
    bool has_errors = false;

    // 1. Download Core (if a compatible libretro implementation exists)
    char cores_dir[MAX_PATH_LEN];
    fs_join_path(cores_dir, sizeof(cores_dir), g_config.ra_dir, "cores");

    char resolved_file[128], resolved_name[128], core_target[MAX_PATH_LEN];
    bool already_installed = platform_resolve_core(p, g_config.ra_dir, resolved_file, sizeof(resolved_file),
                                                   resolved_name, sizeof(resolved_name), core_target, sizeof(core_target));

    SDL_LockMutex(g_shared_asset_mutex);
    if (!p->core_file[0]) {
        log_add(LOG_LEVEL_ERROR,
                "[UNSUPPORTED] %s has no compatible RetroArch/libretro core. "
                "ROMs will be preserved in %s/%s for a compatible external emulator; no fallback core will be used.",
                p->name, g_config.rom_dir, p->id);
        has_errors = true;
    } else if (already_installed) {
        log_add(LOG_LEVEL_INFO, "Core %s already installed, skipping download.", resolved_file);
    } else if (!get_core_base_url(core_base_url, sizeof(core_base_url))) {
        log_add(LOG_LEVEL_ERROR, "[UNSUPPORTED] Configure libretro_core_base_url for this CPU architecture in retro_url.config.");
        has_errors = true;
    } else {
        char core_url[MAX_URL_LEN + sizeof(p->core_file) + sizeof("/.zip")];
        snprintf(core_url, sizeof(core_url), "%s/%s.zip", core_base_url, p->core_file);

        char core_zip[MAX_PATH_LEN];
        char core_zip_name[300];
        snprintf(core_zip_name, sizeof(core_zip_name), "%s.zip", p->core_file);
        fs_join_path(core_zip, sizeof(core_zip), g_config.config_dir, core_zip_name);

        DownloadResult dl;
        bool dl_ok = download_file(core_url, core_zip, download_progress_cb, NULL, &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl);

        if (dl_ok) {
            log_add(LOG_LEVEL_INFO, "[EXTRACT] %s", core_zip_name);
            bool extracted = fs_extract_archive(core_zip, cores_dir);
            bool is_installed = platform_resolve_core(p, g_config.ra_dir, resolved_file, sizeof(resolved_file),
                                                      resolved_name, sizeof(resolved_name), core_target, sizeof(core_target));
            if (!extracted || !is_installed) {
                log_add(LOG_LEVEL_ERROR, "[FAILED] Core archive did not install %s", p->core_file);
                has_errors = true;
            } else {
                fs_remove_file(core_zip);
                log_add(LOG_LEVEL_INFO, "[DONE] Core installed for %s: %s (%s)", p->name, resolved_file, resolved_name);
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

    /* Catalog entries without a filename extension represent required
       directories. This keeps platform-specific layouts in platforms.config. */
    char bios_layout[sizeof(p->bios_files)];
    snprintf(bios_layout, sizeof(bios_layout), "%s", p->bios_files);
    char* layout_save = NULL;
    for (char* entry = strtok_r(bios_layout, " ,;", &layout_save); entry;
         entry = strtok_r(NULL, " ,;", &layout_save)) {
        const char* leaf = strrchr(entry, '/');
        leaf = leaf ? leaf + 1 : entry;
        if (!strchr(leaf, '.')) {
            char required_dir[MAX_PATH_LEN];
            fs_join_path(required_dir, sizeof(required_dir), sys_dir, entry);
            fs_mkdir_p(required_dir);
        }
    }

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

    /* Some directory-based BIOS layouts need content validation (for example,
       an extension and minimum dump size). The rules come from platforms.config. */
    if (p->bios_extensions[0] || p->bios_min_size > 0) {
        char required_copy[sizeof(p->bios_files)];
        snprintf(required_copy, sizeof(required_copy), "%s", p->bios_files);
        char* required_save = NULL;
        for (char* entry = strtok_r(required_copy, " ,;", &required_save); entry;
             entry = strtok_r(NULL, " ,;", &required_save)) {
            char required_path[MAX_PATH_LEN];
            fs_join_path(required_path, sizeof(required_path), sys_dir, entry);
            if (!platform_bios_path_valid(p, required_path)) {
                bool warn_only = strcasecmp(p->bios_missing_action, "warn") == 0;
                log_add(warn_only ? LOG_LEVEL_WARN : LOG_LEVEL_ERROR,
                        "[BIOS REQUIRED] %s needs a valid BIOS in %s (extensions: %s, minimum size: %lld bytes). Installation will %s; copy a dump from your own console before launching games.",
                        p->name, required_path,
                        p->bios_extensions[0] ? p->bios_extensions : "any",
                        p->bios_min_size,
                        warn_only ? "continue" : "stop");
                if (!warn_only) return false;
            }
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
    }

    char catalog_rom_dir[MAX_PATH_LEN];
    fs_join_path(catalog_rom_dir, sizeof(catalog_rom_dir), g_config.rom_dir, p->id);
    bool has_selected_catalog_game = false;
    for (int i = 0; i < rom_catalog_count(); ++i) {
        RomCatalogGame* game = rom_catalog_get(i);
        if (!game || !game->selected || strcmp(game->platform_id, p->id) != 0) continue;
        has_selected_catalog_game = true;
        fs_mkdir_p(catalog_rom_dir);
        char destination[MAX_PATH_LEN];
        fs_join_path(destination, sizeof(destination), catalog_rom_dir, game->name);
        if (fs_exists(destination) && fs_file_size(destination) > 0) {
            log_add(LOG_LEVEL_INFO, "Catalog game already installed: %s", game->name);
            continue;
        }
        DownloadResult dl;
        log_add(LOG_LEVEL_INFO, "Downloading selected game for %s: %s", p->id, game->name);
        if (!download_file(game->url, destination, download_progress_cb, NULL,
                           &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &dl)) {
            log_add(LOG_LEVEL_ERROR, "ERROR: Failed to download %s: %s (HTTP %ld)",
                    game->name, dl.error, dl.http_status);
            return false;
        }
    }
    if (rom_cnt == 0 && !has_selected_catalog_game)
        log_add(LOG_LEVEL_WARN, "No ROM pack or individual game selected for %s (%s)", p->name, p->id);

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
    char game_selection_file[MAX_PATH_LEN];
    fs_join_path(game_selection_file, sizeof(game_selection_file), g_config.config_dir, "selected_roms.conf");
    if (rom_catalog_count() == 0) rom_catalog_refresh(g_config.url_config_file, game_selection_file);
    download_manager_reset();
    cleanup_obsolete_managed_playlists();
    SDL_AtomicSet(&g_task_mgr.work_completed, 0);
    SDL_AtomicSet(&g_task_mgr.work_total, selected_cnt + 1); /* platforms plus playlist generation */

    char install_dir[MAX_PATH_LEN];
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "cores"); fs_mkdir_p(install_dir);
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "system"); fs_mkdir_p(install_dir);
    fs_join_path(install_dir, sizeof(install_dir), g_config.ra_dir, "playlists"); fs_mkdir_p(install_dir);
    if (!ensure_core_info_for_selected())
        log_add(LOG_LEVEL_WARN, "Core information could not be completely installed.");
    if (!ensure_retroarch_databases())
        log_add(LOG_LEVEL_WARN, "Game-name databases could not be installed; arcade shortnames may remain unresolved.");
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
            bool removed = fs_is_dir(full_bios)
                ? fs_remove_dir_recursive(full_bios, bios_path)
                : fs_remove_file(full_bios);
            if (removed) log_add(LOG_LEVEL_INFO, "[REMOVE] BIOS: %s", full_bios);
        }

        SDL_AtomicSet(&g_task_mgr.work_completed, processed);
        g_task_mgr.progress = (float)processed / (float)selected_cnt;
    }

    if (g_config.mode == MODE_STEAM) {
        steam_shortcuts_sync(g_config.ra_dir, g_config.rom_dir);
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

typedef struct { char** items; int count; } ThumbnailLabels;
typedef struct { char** names; char** keys; int count; } ThumbnailCatalog;

static size_t thumbnail_catalog_write(void* data, size_t size, size_t nmemb, void* user_data) {
    ThumbnailLabels* buffer = user_data;
    size_t bytes = size * nmemb;
    char* grown = realloc(buffer->items, (size_t)buffer->count + bytes + 1);
    if (!grown) return 0;
    buffer->items = (char**)grown;
    memcpy((char*)buffer->items + buffer->count, data, bytes);
    buffer->count += (int)bytes;
    ((char*)buffer->items)[buffer->count] = 0;
    return bytes;
}

static void thumbnail_match_key(char* out, size_t out_size, const char* name) {
    const char* end = strstr(name, " (");
    if (!end) end = name + strlen(name);
    const char* alias = strstr(name, " / ");
    if (alias && alias < end) end = alias;
    char words[64][64]; int count = 0;
    for (const char* p = name; p < end && count < 64;) {
        while (p < end && !isalnum((unsigned char)*p)) p++;
        if (p >= end) break;
        size_t length = 0;
        while (p < end && isalnum((unsigned char)*p)) {
            if (length + 1 < sizeof(words[0])) words[count][length++] = (char)tolower((unsigned char)*p);
            p++;
        }
        words[count][length] = 0;
        if (length) count++;
    }
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            if (strcmp(words[i], words[j]) > 0) { char tmp[64]; memcpy(tmp, words[i], 64); memcpy(words[i], words[j], 64); memcpy(words[j], tmp, 64); }
    size_t used = 0;
    for (int i = 0; i < count && used + 1 < out_size; ++i) {
        size_t length = strlen(words[i]);
        if (used + length + 1 >= out_size) break;
        if (used) out[used++] = '|';
        memcpy(out + used, words[i], length); used += length;
    }
    out[used] = 0;
}

static void free_thumbnail_catalog(ThumbnailCatalog* catalog) {
    for (int i = 0; i < catalog->count; ++i) { free(catalog->names[i]); free(catalog->keys[i]); }
    free(catalog->names); free(catalog->keys); memset(catalog, 0, sizeof(*catalog));
}

static bool load_thumbnail_catalog(const char* url, ThumbnailCatalog* catalog) {
    memset(catalog, 0, sizeof(*catalog));
    ThumbnailLabels html = {0};
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, thumbnail_catalog_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RetroSetupGUI/3.0");
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) { curl_easy_cleanup(curl); free(html.items); return false; }
    char* cursor = (char*)html.items;
    while ((cursor = strstr(cursor, "href=\""))) {
        cursor += 6;
        char* end = strchr(cursor, '"');
        if (!end) break;
        int length = (int)(end - cursor), decoded_length = 0;
        char* decoded = curl_easy_unescape(curl, cursor, length, &decoded_length);
        cursor = end + 1;
        if (!decoded || decoded_length < 5 || strcasecmp(decoded + decoded_length - 4, ".png")) {
            if (decoded) curl_free(decoded);
            continue;
        }
        decoded[decoded_length - 4] = 0;
        char** names = realloc(catalog->names, (size_t)(catalog->count + 1) * sizeof(char*));
        char** keys = realloc(catalog->keys, (size_t)(catalog->count + 1) * sizeof(char*));
        if (!names || !keys) { if (names) catalog->names = names; if (keys) catalog->keys = keys; curl_free(decoded); break; }
        catalog->names = names; catalog->keys = keys;
        catalog->names[catalog->count] = strdup(decoded);
        char key[1024]; thumbnail_match_key(key, sizeof(key), decoded);
        catalog->keys[catalog->count] = strdup(key);
        if (catalog->names[catalog->count] && catalog->keys[catalog->count]) catalog->count++;
        curl_free(decoded);
    }
    curl_easy_cleanup(curl); free(html.items);
    return catalog->count > 0;
}

static const char* catalog_match(const ThumbnailCatalog* catalog, const char* label) {
    char key[1024]; thumbnail_match_key(key, sizeof(key), label);
    for (int i = 0; i < catalog->count; ++i) {
        if (!strcmp(key, catalog->keys[i])) return catalog->names[i];
    }
    return NULL;
}

static void free_thumbnail_labels(ThumbnailLabels* labels) {
    for (int i = 0; i < labels->count; ++i) free(labels->items[i]);
    free(labels->items);
    memset(labels, 0, sizeof(*labels));
}

static bool load_thumbnail_labels(const PlatformInfo* platform, ThumbnailLabels* labels) {
    memset(labels, 0, sizeof(*labels));
    char rom_dir[MAX_PATH_LEN];
    fs_join_path(rom_dir, sizeof(rom_dir), g_config.rom_dir, platform->id);
    if (!fs_is_dir(rom_dir)) {
        log_add(LOG_LEVEL_WARN, "ROM directory not found for %s: %s", platform->name, rom_dir);
        return false;
    }
    char playlist[MAX_PATH_LEN];
    snprintf(playlist, sizeof(playlist), "%.1000s/playlists/%.500s.lpl", g_config.ra_dir, platform->name);
    FILE* file = fopen(playlist, "r");
    if (!file) {
        log_add(LOG_LEVEL_WARN, "No ROM playlist found for %s: %s", platform->name, playlist);
        return false;
    }

    char line[4096];
    while (fgets(line, sizeof(line), file)) {
        char* key = strstr(line, "\"label\"");
        if (!key || !(key = strchr(key, ':'))) continue;
        char* begin = strchr(key, '"');
        if (!begin) continue;
        begin++;
        char decoded[2048];
        size_t n = 0;
        for (char* p = begin; *p && n + 1 < sizeof(decoded); ++p) {
            if (*p == '"') break;
            if (*p == '\\' && p[1]) {
                ++p;
                if (*p == 'n') decoded[n++] = '\n';
                else decoded[n++] = *p;
            } else decoded[n++] = *p;
        }
        decoded[n] = 0;
        if (!decoded[0]) continue;
        char** grown = realloc(labels->items, (size_t)(labels->count + 1) * sizeof(*labels->items));
        if (!grown) { fclose(file); free_thumbnail_labels(labels); return false; }
        labels->items = grown;
        labels->items[labels->count] = malloc(n + 1);
        if (!labels->items[labels->count]) { fclose(file); free_thumbnail_labels(labels); return false; }
        memcpy(labels->items[labels->count++], decoded, n + 1);
    }
    fclose(file);
    return labels->count > 0;
}

static void thumbnail_filename(char* out, size_t out_size, const char* label) {
    snprintf(out, out_size, "%s", label);
    for (char* p = out; *p; ++p)
        if (strchr("&*/:`<>?|\\", *p)) *p = '_';
}

static int download_thumbnail_collection(const char* base_url, const PlatformInfo* platform,
                                         const char* type, const ThumbnailLabels* labels,
                                         int* downloaded, int* skipped, int* missing, int* failed,
                                         int* processed, int total) {
    CURL* encoder = curl_easy_init();
    if (!encoder) return 1;
    char* system_encoded = curl_easy_escape(encoder, platform->name, 0);
    if (!system_encoded) { curl_easy_cleanup(encoder); return 1; }

    log_add(LOG_LEVEL_INFO, "[THUMBNAILS] %s / %s (%d ROMs)", platform->name, type, labels->count);

    char collection_url[4096];
    snprintf(collection_url, sizeof(collection_url), "%s/%s/%s/", base_url, system_encoded, type);
    ThumbnailCatalog catalog;
    if (!load_thumbnail_catalog(collection_url, &catalog)) {
        log_add(LOG_LEVEL_ERROR, "Could not read thumbnail name index: %s", collection_url);
        curl_free(system_encoded);
        curl_easy_cleanup(encoder);
        return 1;
    }

    char destination_dir[MAX_PATH_LEN];
    snprintf(destination_dir, sizeof(destination_dir), "%.1000s/thumbnails/%.500s/%s",
             g_config.ra_dir, platform->name, type);
    fs_mkdir_p(destination_dir);

    for (int i = 0; i < labels->count && !g_task_mgr.cancel_requested; ++i) {
        char local_name[2100];
        thumbnail_filename(local_name, sizeof(local_name), labels->items[i]);
        char destination[MAX_PATH_LEN];
        strncat(local_name, ".png", sizeof(local_name) - strlen(local_name) - 1);
        fs_join_path(destination, sizeof(destination), destination_dir, local_name);

        bool existed = fs_is_file(destination) && fs_file_size(destination) > 0;
        bool success = false;
        DownloadResult result;
        memset(&result, 0, sizeof(result));
        if (existed) {
            success = true;
            result.state = DOWNLOAD_SKIPPED;
        } else {
            const char* matched = catalog_match(&catalog, labels->items[i]);
            const char* candidates[] = {matched};
            int candidate_count = 1;
            if (!matched) result.http_status = 404;
            for (int candidate = 0; candidate < candidate_count && !success && !g_task_mgr.cancel_requested; ++candidate) {
                char remote_name[2048], source[6144], attempt_path[MAX_PATH_LEN], attempt_part[MAX_PATH_LEN];
                if (!candidates[candidate] || !candidates[candidate][0]) continue;
                if (candidate > 0 && candidates[candidate - 1] && !strcmp(candidates[candidate], candidates[candidate - 1])) continue;
                thumbnail_filename(remote_name, sizeof(remote_name), candidates[candidate]);
                char* encoded_name = curl_easy_escape(encoder, remote_name, 0);
                if (!encoded_name) continue;
                snprintf(source, sizeof(source), "%s/%s/%s/%s.png", base_url, system_encoded, type, encoded_name);
                curl_free(encoded_name);
                snprintf(attempt_path, sizeof(attempt_path), "%.1900s.lookup%d.png", destination, candidate + 1);
                snprintf(attempt_part, sizeof(attempt_part), "%.1900s.part", attempt_path);
                success = download_file(source, attempt_path, download_progress_cb, NULL,
                                        &g_task_mgr.cancel_requested, &g_task_mgr.pause_requested, &result);
                if (success) {
                    if (rename(attempt_path, destination) != 0) {
                        success = fs_copy_file(attempt_path, destination);
                        if (success) fs_remove_file(attempt_path);
                    }
                    if (!success) {
                        (*failed)++;
                        log_add(LOG_LEVEL_ERROR, "Could not save thumbnail as: %s", destination);
                    }
                } else {
                    fs_remove_file(attempt_path);
                    fs_remove_file(attempt_part);
                    if (result.http_status != 404) break;
                }
            }
        }
        if (success) {
            if (existed || result.state == DOWNLOAD_SKIPPED) (*skipped)++;
            else (*downloaded)++;
        } else if (!g_task_mgr.cancel_requested) {
            if (result.http_status == 404) {
                (*missing)++;
                if (*missing <= 10)
                    log_add(LOG_LEVEL_INFO, "No catalog image: %s / %s / %s", platform->name, type, labels->items[i]);
                else if (*missing == 11)
                    log_add(LOG_LEVEL_INFO, "Additional catalog absences will be counted without individual log lines.");
            } else (*failed)++;
        }
        (*processed)++;
        SDL_AtomicSet(&g_task_mgr.work_completed, *processed);
        g_task_mgr.progress = total > 0 ? (float)(*processed) / (float)total : 1.0f;
        snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                 "%s: %d/%d (%.1f%%)", platform->name, *processed, total,
                 total > 0 ? 100.0 * (double)(*processed) / (double)total : 100.0);
    }

    free_thumbnail_catalog(&catalog);
    curl_free(system_encoded);
    curl_easy_cleanup(encoder);
    return g_task_mgr.cancel_requested ? -1 : 0;
}

static int do_task_thumbnails(void) {
    log_add(LOG_LEVEL_INFO, "=== Starting Native Thumbnail Download (libcurl) ===");
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message), "Downloading thumbnail collections...");
    int selected_count = get_selected_count();
    if (selected_count == 0) {
        log_add(LOG_LEVEL_ERROR, "No platforms selected for thumbnail download.");
        return 1;
    }

    url_config_load(g_config.url_config_file);
    cleanup_obsolete_managed_playlists();
    if (!ensure_retroarch_databases())
        log_add(LOG_LEVEL_WARN, "Game-name databases are unavailable; some arcade names may remain unresolved.");

    /* Refresh labels so deleted or newly added ROMs cannot leave thumbnail
       downloads based on a stale playlist. */
    playlist_generate_selected(g_config.ra_dir, g_config.rom_dir);

    const char* configured_base = url_config_get_string("THUMBNAILS_BASE_URL", "https://thumbnails.libretro.com");
    char base_url[2048];
    snprintf(base_url, sizeof(base_url), "%s", configured_base);
    size_t base_length = strlen(base_url);
    while (base_length > 0 && base_url[base_length - 1] == '/') base_url[--base_length] = 0;

    const char* types[] = {"Named_Boxarts"};
    const int type_count = (int)(sizeof(types) / sizeof(types[0]));
    int downloaded = 0, skipped = 0, missing = 0, failed = 0, processed = 0;
    int platforms_without_roms = 0;
    int total = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; ++i) {
        if (!g_platforms[i].selected) continue;
        ThumbnailLabels count_labels;
        if (load_thumbnail_labels(&g_platforms[i], &count_labels)) {
            total += count_labels.count * type_count;
            free_thumbnail_labels(&count_labels);
        }
    }
    SDL_AtomicSet(&g_task_mgr.work_total, total);
    log_add(LOG_LEVEL_INFO, "Thumbnail range: 0/%d (0.0%%) to %d/%d (100.0%%)", total, total, total);
    for (int i = 0; i < TOTAL_PLATFORMS && !g_task_mgr.cancel_requested; ++i) {
        if (!g_platforms[i].selected) continue;
        ThumbnailLabels labels;
        if (!load_thumbnail_labels(&g_platforms[i], &labels)) {
            log_add(LOG_LEVEL_WARN, "NO ROMS FOUND: %s; no thumbnails will be downloaded.", g_platforms[i].name);
            platforms_without_roms++;
            continue;
        }
        for (int type = 0; type < type_count && !g_task_mgr.cancel_requested; ++type) {
            snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
                     "%s: %s", g_platforms[i].name, types[type]);
            int result = download_thumbnail_collection(base_url, &g_platforms[i], types[type], &labels,
                                                       &downloaded, &skipped, &missing, &failed,
                                                       &processed, total);
            if (result > 0) failed++;
        }
        free_thumbnail_labels(&labels);
    }

    if (g_task_mgr.cancel_requested) {
        log_add(LOG_LEVEL_WARN, "=== Thumbnail Download Cancelled (%d new, %d existing) ===", downloaded, skipped);
        return -1;
    }
    log_add(failed ? LOG_LEVEL_WARN : LOG_LEVEL_INFO,
            "=== Thumbnails Completed: %d downloaded, %d existing, %d unavailable in catalog, %d systems without ROMs, %d errors ===",
            downloaded, skipped, missing, platforms_without_roms, failed);
    snprintf(g_task_mgr.status_message, sizeof(g_task_mgr.status_message),
             "Thumbnails: %d downloaded, %d existing, %d unavailable, %d without ROMs, %d errors.",
             downloaded, skipped, missing, platforms_without_roms, failed);
    g_task_mgr.progress = 1.0f;
    return failed ? 1 : 0;
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
