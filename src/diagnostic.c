#include "diagnostic.h"
#include "fs.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>

void diagnostic_format_size(uint64_t bytes, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    double b = (double)bytes;
    if (b >= 1024.0 * 1024.0 * 1024.0) {
        snprintf(out, out_size, "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    } else if (b >= 1024.0 * 1024.0) {
        snprintf(out, out_size, "%.1f MB", b / (1024.0 * 1024.0));
    } else if (b >= 1024.0) {
        snprintf(out, out_size, "%.1f KB", b / 1024.0);
    } else {
        snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
    }
}

void diagnostic_format_path_short(const char* full_path, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!full_path || !*full_path) {
        out[0] = 0;
        return;
    }
    const char* home = getenv("HOME");
    if (home && strncmp(full_path, home, strlen(home)) == 0) {
        snprintf(out, out_size, "~%s", full_path + strlen(home));
    } else {
        snprintf(out, out_size, "%s", full_path);
    }
}

static void calc_dir_stats_recursive(const char* dir_path, int* file_count, uint64_t* total_size) {
    if (!dir_path || !fs_is_dir(dir_path)) return;

    DIR* d = opendir(dir_path);
    if (!d) return;

    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        char sub_path[4096];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, dir->d_name);

        struct stat st;
        if (stat(sub_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                calc_dir_stats_recursive(sub_path, file_count, total_size);
            } else if (S_ISREG(st.st_mode)) {
                if (file_count) (*file_count)++;
                if (total_size) (*total_size) += (uint64_t)st.st_size;
            }
        }
    }
    closedir(d);
}

void diagnostic_run_scan(SystemDiagnosticReport* report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));

    snprintf(report->app_version, sizeof(report->app_version), "%s", RETRO_SETUP_VERSION);
    snprintf(report->config_dir, sizeof(report->config_dir), "%s", g_config.config_dir);
    snprintf(report->rom_base_dir, sizeof(report->rom_base_dir), "%s", g_config.rom_dir);
    snprintf(report->retroarch_target_dir, sizeof(report->retroarch_target_dir), "%s", g_config.ra_dir);

    // 1. OS & Architecture
    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(report->os_arch, sizeof(report->os_arch), "%.60s", uts.machine);
    } else {
        snprintf(report->os_arch, sizeof(report->os_arch), "unknown");
    }

    snprintf(report->os_distro, sizeof(report->os_distro), "%.120s (%.120s)", g_config.distro_name, g_config.distro_id);
    get_home_dir(report->home_dir, sizeof(report->home_dir));

    // 2. RetroArch Binary & Config Detection
    const char* ra_candidates[] = {
        "/usr/bin/retroarch",
        "/usr/local/bin/retroarch",
        "~/.local/bin/retroarch",
        "~/.var/app/org.libretro.RetroArch/current/active/export/bin/org.libretro.RetroArch",
        NULL
    };
    for (int i = 0; ra_candidates[i]; i++) {
        char full[MAX_PATH_LEN];
        if (ra_candidates[i][0] == '~') {
            snprintf(full, sizeof(full), "%s%s", report->home_dir, ra_candidates[i] + 1);
        } else {
            snprintf(full, sizeof(full), "%s", ra_candidates[i]);
        }
        if (fs_exists(full)) {
            report->retroarch_binary_found = true;
            snprintf(report->retroarch_binary_path, sizeof(report->retroarch_binary_path), "%s", full);
            break;
        }
    }

    char ra_cfg[MAX_PATH_LEN];
    snprintf(ra_cfg, sizeof(ra_cfg), "%.1000s/retroarch.cfg", g_config.ra_dir);
    report->retroarch_config_found = fs_exists(ra_cfg) || fs_exists(g_config.ra_dir);

    // 3. Permissions Check
    report->dir_ra_readable = (access(g_config.ra_dir, R_OK) == 0);
    report->dir_ra_writable = (access(g_config.ra_dir, W_OK) == 0);
    report->dir_rom_writable = (access(g_config.rom_dir, W_OK) == 0);

    // 4. Disk Space via statvfs
    struct statvfs svfs;
    if (statvfs(g_config.ra_dir, &svfs) == 0) {
        report->disk_free_bytes = (uint64_t)svfs.f_bavail * (uint64_t)svfs.f_frsize;
        report->disk_total_bytes = (uint64_t)svfs.f_blocks * (uint64_t)svfs.f_frsize;
    }

    // 5. Cores & Core Info count
    char cores_dir[MAX_PATH_LEN], info_dir[MAX_PATH_LEN];
    fs_join_path(cores_dir, sizeof(cores_dir), g_config.ra_dir, "cores");
    fs_join_path(info_dir, sizeof(info_dir), g_config.ra_dir, "info");

    DIR* cd = opendir(cores_dir);
    if (cd) {
        struct dirent* de;
        while ((de = readdir(cd)) != NULL) {
            if (strstr(de->d_name, "_libretro.so") || strstr(de->d_name, ".so")) {
                report->cores_installed_count++;
            }
        }
        closedir(cd);
    }

    DIR* id = opendir(info_dir);
    if (id) {
        struct dirent* de;
        while ((de = readdir(id)) != NULL) {
            if (strstr(de->d_name, ".info")) {
                report->core_info_count++;
            }
        }
        closedir(id);
    }

    // 6. Thumbnails stats
    char thumbs_dir[MAX_PATH_LEN];
    fs_join_path(thumbs_dir, sizeof(thumbs_dir), g_config.ra_dir, "thumbnails");
    calc_dir_stats_recursive(thumbs_dir, &report->thumbnails_count_total, &report->thumbnails_size_total);

    // 7. Per-Platform Inventory Scanning
    report->platform_diag_count = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (!g_platforms[i].selected) continue;

        PlatformDiagnostic* pd = &report->platform_diags[report->platform_diag_count++];
        memset(pd, 0, sizeof(*pd));
        pd->platform = &g_platforms[i];

        report->platforms_selected++;

        // Core check
        char resolved_core_file[128], resolved_core_name[128], resolved_core_path[MAX_PATH_LEN];
        pd->core_installed = platform_resolve_core(&g_platforms[i], g_config.ra_dir,
                                                   resolved_core_file, sizeof(resolved_core_file),
                                                   resolved_core_name, sizeof(resolved_core_name),
                                                   resolved_core_path, sizeof(resolved_core_path));

        char info_name[256];
        snprintf(info_name, sizeof(info_name), "%.250s", resolved_core_file);
        char* dot_so = strstr(info_name, ".so");
        if (dot_so) snprintf(dot_so, 6, ".info");
        char info_path[MAX_PATH_LEN];
        snprintf(info_path, sizeof(info_path), "%.1000s/%.128s", info_dir, info_name);
        pd->core_info_found = fs_exists(info_path);

        // BIOS check
        char sys_dir[MAX_PATH_LEN];
        fs_join_path(sys_dir, sizeof(sys_dir), g_config.ra_dir, "system");
        if (g_platforms[i].bios_files[0]) {
            char bcopy[512];
            snprintf(bcopy, sizeof(bcopy), "%.500s", g_platforms[i].bios_files);
            char* token = strtok(bcopy, " ,;");
            while (token) {
                pd->bios_required_count++;
                char bpath[MAX_PATH_LEN];
                fs_join_path(bpath, sizeof(bpath), sys_dir, token);

                bool token_found = false;
                if (fs_exists(bpath)) {
                    token_found = platform_bios_path_valid(&g_platforms[i], bpath);
                } else {
                    const char* slash = strrchr(token, '/');
                    char bname[MAX_PATH_LEN];
                    snprintf(bname, sizeof(bname), "%s", slash ? slash + 1 : token);
                    char alt_path[MAX_PATH_LEN];
                    fs_join_path(alt_path, sizeof(alt_path), sys_dir, bname);
                    if (fs_exists(alt_path)) {
                        token_found = platform_bios_path_valid(&g_platforms[i], alt_path);
                    }
                }

                if (token_found) {
                    pd->bios_found_count++;
                    report->bios_found_total++;
                } else {
                    pd->bios_missing_count++;
                    report->bios_missing_total++;
                    if (pd->bios_missing_names[0]) {
                        strcat(pd->bios_missing_names, ", ");
                    }
                    strcat(pd->bios_missing_names, token);
                }
                token = strtok(NULL, " ,;");
            }
        }

        // ROMs count & size
        char prom_dir[MAX_PATH_LEN];
        fs_join_path(prom_dir, sizeof(prom_dir), g_config.rom_dir, g_platforms[i].id);
        calc_dir_stats_recursive(prom_dir, &pd->rom_count, &pd->rom_size);
        report->rom_files_total += pd->rom_count;
        report->rom_size_total += pd->rom_size;

        // Playlist check
        char pl_path[MAX_PATH_LEN];
        snprintf(pl_path, sizeof(pl_path), "%.1000s/playlists/%.128s.lpl", g_config.ra_dir, g_platforms[i].name);
        pd->playlist_found = fs_exists(pl_path);
        if (pd->playlist_found) report->playlists_found_total++;

        // Thumbnails check
        char pthumb_dir[MAX_PATH_LEN];
        snprintf(pthumb_dir, sizeof(pthumb_dir), "%.1000s/thumbnails/%.128s", g_config.ra_dir, g_platforms[i].name);
        uint64_t pthumb_sz = 0;
        calc_dir_stats_recursive(pthumb_dir, &pd->thumbnail_count, &pthumb_sz);

        // Status Determination
        if (!pd->core_installed || pd->bios_missing_count > 0) {
            pd->status = PLATFORM_STATUS_INCOMPLETE;
            report->platforms_incomplete++;
        } else {
            pd->status = PLATFORM_STATUS_READY;
            report->platforms_ready++;
        }
    }

    // 8. Overall Health Summary
    if (report->bios_missing_total > 0 || !report->retroarch_binary_found || report->platforms_incomplete > 0) {
        report->overall_health = HEALTH_STATUS_WARNING;
        report->health_warning_count++;
    } else {
        report->overall_health = HEALTH_STATUS_HEALTHY;
    }
}
