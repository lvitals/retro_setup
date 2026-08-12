#include "config.h"
#include "platform_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

AppConfig g_config;

void get_home_dir(char* out, size_t size) {
    const char* home = getenv("HOME");
    if (home) {
        snprintf(out, size, "%s", home);
    } else {
        snprintf(out, size, "/tmp");
    }
}

static bool file_exists(const char* path) {
    return (access(path, F_OK) == 0);
}

static bool dir_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

static void ensure_dir_exists(const char* dir) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void detect_steam_retroarch_dir(char* out, size_t size) {
    const char* env_steam = getenv("STEAM_RA_DIR");
    if (env_steam && dir_exists(env_steam)) {
        snprintf(out, size, "%s", env_steam);
        return;
    }

    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    const char* candidates[] = {
        "/.local/share/Steam/steamapps/common/RetroArch",
        "/.steam/steam/steamapps/common/RetroArch",
        "/.steam/root/steamapps/common/RetroArch",
        "/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/RetroArch",
        "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RetroArch",
        NULL
    };

    for (int i = 0; candidates[i]; i++) {
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%.1000s%.500s", home, candidates[i]);
        if (dir_exists(full)) {
            char exe1[MAX_PATH_LEN], exe2[MAX_PATH_LEN];
            snprintf(exe1, sizeof(exe1), "%.1000s/retroarch", full);
            snprintf(exe2, sizeof(exe2), "%.1000s/retroarch.sh", full);
            if (file_exists(exe1) || file_exists(exe2)) {
                snprintf(out, size, "%.1000s", full);
                return;
            }
        }
    }

    // Default fallback
    snprintf(out, size, "%.1000s/.local/share/Steam/steamapps/common/RetroArch", home);
}

void detect_environment(void) {
    // Detect OS distro
    snprintf(g_config.distro_name, sizeof(g_config.distro_name), "Linux");
    snprintf(g_config.distro_id, sizeof(g_config.distro_id), "unknown");

    FILE* f = fopen("/etc/os-release", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char* val = line + 12;
                if (*val == '"') val++;
                char* end = strchr(val, '"');
                if (!end) end = strchr(val, '\n');
                if (end) *end = 0;
                snprintf(g_config.distro_name, sizeof(g_config.distro_name), "%.250s", val);
            } else if (strncmp(line, "ID=", 3) == 0) {
                char* val = line + 3;
                if (*val == '"') val++;
                char* end = strchr(val, '"');
                if (!end) end = strchr(val, '\n');
                if (end) *end = 0;
                snprintf(g_config.distro_id, sizeof(g_config.distro_id), "%.60s", val);
            }
        }
        fclose(f);
    }
}

void init_config(void) {
    memset(&g_config, 0, sizeof(g_config));
    g_config.mode = MODE_STANDALONE;
    g_config.audio_enabled = true;
    g_config.crt_scanlines = false;

    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    // Get current working directory (repo_dir)
    if (getcwd(g_config.repo_dir, sizeof(g_config.repo_dir)) == NULL) {
        snprintf(g_config.repo_dir, sizeof(g_config.repo_dir), ".");
    }

    snprintf(g_config.config_dir, sizeof(g_config.config_dir), "%.1000s/.config/retro_setup", home);
    snprintf(g_config.rom_dir, sizeof(g_config.rom_dir), "%.1000s/roms", g_config.repo_dir);
    snprintf(g_config.url_config_file, sizeof(g_config.url_config_file), "%.1000s/retro_url.config", g_config.repo_dir);

    // Check environment mode variable
    const char* env_mode = getenv("RETRO_SETUP_MODE");
    if (env_mode && strcmp(env_mode, "steam") == 0) {
        g_config.mode = MODE_STEAM;
    }

    set_setup_mode(g_config.mode);
    detect_environment();
    load_selected_platforms_config();
}

void set_setup_mode(RetroSetupMode mode) {
    g_config.mode = mode;
    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    if (mode == MODE_STEAM) {
        detect_steam_retroarch_dir(g_config.ra_dir, sizeof(g_config.ra_dir));
        snprintf(g_config.config_file, sizeof(g_config.config_file), "%.1000s/retro_setup_steam.conf", g_config.config_dir);
    } else {
        snprintf(g_config.ra_dir, sizeof(g_config.ra_dir), "%.1000s/.config/retroarch", home);
        snprintf(g_config.config_file, sizeof(g_config.config_file), "%.1000s/retro_setup.conf", g_config.config_dir);
    }
}

bool load_selected_platforms_config(void) {
    reset_all_selections(false);

    FILE* f = fopen(g_config.config_file, "r");
    if (!f) {
        return false;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "SELECTED_PLATFORMS=", 19) == 0 || strstr(line, "SELECTED_PLATFORMS=(")) {
            char* ptr = strchr(line, '(');
            if (!ptr) ptr = line + 19;
            else ptr++;

            char token[128];
            int token_pos = 0;
            bool in_quote = false;

            for (; *ptr; ptr++) {
                if (*ptr == ')') break;
                if (*ptr == '"' || *ptr == '\'') {
                    in_quote = !in_quote;
                    continue;
                }
                if (!in_quote && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) {
                    if (token_pos > 0) {
                        token[token_pos] = 0;
                        int idx = get_platform_index_by_id(token);
                        if (idx >= 0) {
                            g_platforms[idx].selected = true;
                        }
                        token_pos = 0;
                    }
                } else {
                    if (token_pos < (int)sizeof(token) - 1) {
                        token[token_pos++] = *ptr;
                    }
                }
            }
            if (token_pos > 0) {
                token[token_pos] = 0;
                int idx = get_platform_index_by_id(token);
                if (idx >= 0) {
                    g_platforms[idx].selected = true;
                }
            }
        }
    }
    fclose(f);
    return true;
}

bool save_selected_platforms_config(void) {
    ensure_dir_exists(g_config.config_dir);

    FILE* f = fopen(g_config.config_file, "w");
    if (!f) {
        return false;
    }

    fprintf(f, "# Persistent retro_setup configuration\n");
    fprintf(f, "# Generated by retro_setup GUI / C engine\n");
    fprintf(f, "SELECTED_PLATFORMS=(");
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_platforms[i].selected) {
            fprintf(f, " %s", g_platforms[i].id);
        }
    }
    fprintf(f, " )\n");

    fclose(f);
    return true;
}

int get_selected_count(void) {
    int count = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_platforms[i].selected) count++;
    }
    return count;
}
