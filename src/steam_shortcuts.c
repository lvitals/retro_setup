#include "steam_shortcuts.h"
#include "config.h"
#include "fs.h"
#include "log.h"
#include "platform_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_STEAM_SHORTCUTS 2048
#define MAX_TAGS_PER_SHORTCUT 8

typedef struct {
    uint32_t appid;
    char app_name[256];
    char exe[MAX_PATH_LEN];
    char start_dir[MAX_PATH_LEN];
    char icon[MAX_PATH_LEN];
    char shortcut_path[MAX_PATH_LEN];
    char launch_options[MAX_PATH_LEN * 2];
    int32_t is_hidden;
    int32_t allow_desktop_config;
    int32_t allow_overlay;
    int32_t open_vr;
    int32_t devkit;
    char devkit_game_id[128];
    int32_t devkit_override_appid;
    int32_t last_play_time;
    char flatpak_appid[128];
    char tags[MAX_TAGS_PER_SHORTCUT][128];
    int tag_count;
    bool is_retro_setup;
    bool keep;
    char rom_path[MAX_PATH_LEN];
} SteamShortcut;

// Standard CRC32 table & function for computing Steam shortcut appids
static uint32_t crc32_buffer(const void* data, size_t length) {
    static uint32_t table[256];
    static bool table_init = false;
    if (!table_init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) c = 0xEDB88320L ^ (c >> 1);
                else c = c >> 1;
            }
            table[i] = c;
        }
        table_init = true;
    }

    uint32_t crc = 0xFFFFFFFFL;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFL;
}

static uint32_t compute_steam_appid(const char* exe, const char* app_name) {
    char combined[MAX_PATH_LEN * 2];
    snprintf(combined, sizeof(combined), "%s%s", exe ? exe : "", app_name ? app_name : "");
    uint32_t crc = crc32_buffer(combined, strlen(combined));
    return (crc | 0x80000000);
}

static bool find_executable_in_path(const char* name, char* out, size_t out_size) {
    const char* path_env = getenv("PATH");
    if (!name || !name[0] || !path_env) return false;
    char* paths = strdup(path_env);
    if (!paths) return false;
    bool found = false;
    char* save = NULL;
    for (char* dir = strtok_r(paths, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char candidate[MAX_PATH_LEN];
        fs_join_path(candidate, sizeof(candidate), dir[0] ? dir : ".", name);
        if (access(candidate, X_OK) == 0) {
            if (out && out_size) snprintf(out, out_size, "%s", candidate);
            found = true;
            break;
        }
    }
    free(paths);
    return found;
}

bool steam_has_gamemode(void) {
    return find_executable_in_path("gamemoderun", NULL, 0);
}

bool steam_find_retroarch_bin(char* out, size_t size, const char* ra_dir) {
    if (!out || size == 0) return false;

    if (ra_dir && *ra_dir) {
        char exe[MAX_PATH_LEN];
        snprintf(exe, sizeof(exe), "%s/retroarch", ra_dir);
        if (access(exe, X_OK) == 0) {
            snprintf(out, size, "%s", exe);
            return true;
        }
        snprintf(exe, sizeof(exe), "%s/retroarch.sh", ra_dir);
        if (access(exe, X_OK) == 0) {
            snprintf(out, size, "%s", exe);
            return true;
        }
    }

    if (find_executable_in_path("retroarch", out, size)) return true;

    // Default fallback
    snprintf(out, size, "retroarch");
    return true;
}

static bool find_file_below(const char* directory, const char* filename, int depth,
                            char* out, size_t out_size) {
    if (depth < 0) return false;
    DIR* dir = opendir(directory);
    if (!dir) return false;
    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[MAX_PATH_LEN];
        fs_join_path(path, sizeof(path), directory, entry->d_name);
        if (strcmp(entry->d_name, filename) == 0 && fs_is_file(path)) {
            snprintf(out, out_size, "%s", path);
            found = true;
        } else if (depth > 0 && fs_is_dir(path)) {
            found = find_file_below(path, filename, depth - 1, out, out_size);
        }
    }
    closedir(dir);
    return found;
}

static bool steam_find_runtime_launcher(const char* ra_dir, char* steam_bin,
                                        size_t steam_bin_size, char* app_id,
                                        size_t app_id_size) {
    if (!ra_dir || !steam_bin || !app_id) return false;

    char app_id_path[MAX_PATH_LEN];
    if (!find_file_below(ra_dir, "steam_appid.txt", 2, app_id_path, sizeof(app_id_path)))
        return false;
    FILE* file = fopen(app_id_path, "r");
    if (!file) return false;
    char raw_id[64] = {0};
    bool read_ok = fgets(raw_id, sizeof(raw_id), file) != NULL;
    fclose(file);
    if (!read_ok) return false;
    raw_id[strcspn(raw_id, "\r\n")] = 0;
    if (!raw_id[0]) return false;
    for (size_t i = 0; raw_id[i]; ++i)
        if (!isdigit((unsigned char)raw_id[i])) return false;

    if (!find_executable_in_path("steam", steam_bin, steam_bin_size)) return false;
    snprintf(app_id, app_id_size, "%s", raw_id);
    return true;
}

static bool read_vdf_string(const uint8_t* data, size_t size, size_t* offset, char* out, size_t out_size) {
    if (*offset >= size) return false;
    size_t start = *offset;
    while (*offset < size && data[*offset] != 0) {
        (*offset)++;
    }
    if (*offset >= size) return false;
    size_t len = *offset - start;
    (*offset)++; // skip null terminator

    if (out && out_size > 0) {
        size_t copy_len = (len < out_size - 1) ? len : out_size - 1;
        memcpy(out, data + start, copy_len);
        out[copy_len] = '\0';
    }
    return true;
}

static bool read_vdf_uint32(const uint8_t* data, size_t size, size_t* offset, uint32_t* out) {
    if (*offset + 4 > size) return false;
    if (out) {
        *out = (uint32_t)data[*offset] |
               ((uint32_t)data[*offset + 1] << 8) |
               ((uint32_t)data[*offset + 2] << 16) |
               ((uint32_t)data[*offset + 3] << 24);
    }
    *offset += 4;
    return true;
}

static void extract_rom_path_from_launch_options(const char* launch_options, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!launch_options || !*launch_options) return;

    // LaunchOptions typically ends with "<rom_path>" or <rom_path>
    const char* last_quote = strrchr(launch_options, '"');
    if (last_quote && last_quote > launch_options) {
        const char* prev_quote = last_quote - 1;
        while (prev_quote >= launch_options && *prev_quote != '"') {
            prev_quote--;
        }
        if (prev_quote >= launch_options && *prev_quote == '"') {
            size_t len = (size_t)(last_quote - prev_quote - 1);
            if (len < out_size) {
                memcpy(out, prev_quote + 1, len);
                out[len] = '\0';
                return;
            }
        }
    }

    // Space separated fallback
    const char* last_space = strrchr(launch_options, ' ');
    if (last_space) {
        snprintf(out, out_size, "%s", last_space + 1);
    }
}

static int parse_shortcuts_vdf(const char* filepath, SteamShortcut* shortcuts, int max_shortcuts) {
    FILE* f = fopen(filepath, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0 || file_size > 10 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    uint8_t* buffer = malloc((size_t)file_size);
    if (!buffer) {
        fclose(f);
        return 0;
    }

    if (fread(buffer, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(buffer);
        fclose(f);
        return 0;
    }
    fclose(f);

    size_t offset = 0;
    size_t size = (size_t)file_size;

    // 1. Root object marker: 0x00 "shortcuts" \0
    if (offset >= size || buffer[offset++] != 0x00) {
        free(buffer);
        return 0;
    }

    char root_name[64] = {0};
    if (!read_vdf_string(buffer, size, &offset, root_name, sizeof(root_name))) {
        free(buffer);
        return 0;
    }

    int count = 0;

    while (offset < size && count < max_shortcuts) {
        uint8_t type = buffer[offset++];
        if (type == 0x08) {
            // End of root object
            break;
        }
        if (type != 0x00) {
            // Unexpected type at entry level
            break;
        }

        char entry_idx_str[64] = {0};
        if (!read_vdf_string(buffer, size, &offset, entry_idx_str, sizeof(entry_idx_str))) break;

        SteamShortcut* s = &shortcuts[count];
        memset(s, 0, sizeof(SteamShortcut));
        s->allow_desktop_config = 1;
        s->allow_overlay = 1;
        s->keep = true;

        // Parse key-value pairs inside shortcut entry
        while (offset < size) {
            uint8_t field_type = buffer[offset++];
            if (field_type == 0x08) {
                // End of shortcut entry
                break;
            }

            char key[128] = {0};
            if (!read_vdf_string(buffer, size, &offset, key, sizeof(key))) break;

            if (field_type == 0x01) { // String
                char val[MAX_PATH_LEN * 2] = {0};
                if (!read_vdf_string(buffer, size, &offset, val, sizeof(val))) break;

                if (strcasecmp(key, "AppName") == 0) snprintf(s->app_name, sizeof(s->app_name), "%s", val);
                else if (strcasecmp(key, "Exe") == 0) snprintf(s->exe, sizeof(s->exe), "%s", val);
                else if (strcasecmp(key, "StartDir") == 0) snprintf(s->start_dir, sizeof(s->start_dir), "%s", val);
                else if (strcasecmp(key, "icon") == 0) snprintf(s->icon, sizeof(s->icon), "%s", val);
                else if (strcasecmp(key, "ShortcutPath") == 0) snprintf(s->shortcut_path, sizeof(s->shortcut_path), "%s", val);
                else if (strcasecmp(key, "LaunchOptions") == 0) snprintf(s->launch_options, sizeof(s->launch_options), "%s", val);
                else if (strcasecmp(key, "DevkitGameID") == 0) snprintf(s->devkit_game_id, sizeof(s->devkit_game_id), "%s", val);
                else if (strcasecmp(key, "FlatpakAppID") == 0) snprintf(s->flatpak_appid, sizeof(s->flatpak_appid), "%s", val);
            } else if (field_type == 0x02) { // uint32 / int32
                uint32_t val = 0;
                if (!read_vdf_uint32(buffer, size, &offset, &val)) break;

                if (strcasecmp(key, "appid") == 0) s->appid = val;
                else if (strcasecmp(key, "IsHidden") == 0) s->is_hidden = (int32_t)val;
                else if (strcasecmp(key, "AllowDesktopConfig") == 0) s->allow_desktop_config = (int32_t)val;
                else if (strcasecmp(key, "AllowOverlay") == 0) s->allow_overlay = (int32_t)val;
                else if (strcasecmp(key, "OpenVR") == 0) s->open_vr = (int32_t)val;
                else if (strcasecmp(key, "Devkit") == 0) s->devkit = (int32_t)val;
                else if (strcasecmp(key, "DevkitOverrideAppID") == 0) s->devkit_override_appid = (int32_t)val;
                else if (strcasecmp(key, "LastPlayTime") == 0) s->last_play_time = (int32_t)val;
            } else if (field_type == 0x00) { // Subtable (tags)
                if (strcasecmp(key, "tags") == 0) {
                    while (offset < size) {
                        uint8_t tag_field_type = buffer[offset++];
                        if (tag_field_type == 0x08) break; // End of tags
                        char tag_idx_str[32] = {0};
                        if (!read_vdf_string(buffer, size, &offset, tag_idx_str, sizeof(tag_idx_str))) break;
                        char tag_val[128] = {0};
                        if (tag_field_type == 0x01) {
                            if (!read_vdf_string(buffer, size, &offset, tag_val, sizeof(tag_val))) break;
                            if (s->tag_count < MAX_TAGS_PER_SHORTCUT) {
                                snprintf(s->tags[s->tag_count++], sizeof(s->tags[0]), "%s", tag_val);
                            }
                        }
                    }
                } else {
                    // Unknown subtable: skip until end 0x08
                    int depth = 1;
                    while (offset < size && depth > 0) {
                        uint8_t sub = buffer[offset++];
                        if (sub == 0x08) depth--;
                        else if (sub == 0x00) depth++;
                    }
                }
            }
        }

        // Determine if shortcut is managed by Retro Setup
        s->is_retro_setup = false;
        for (int t = 0; t < s->tag_count; t++) {
            if (strcasecmp(s->tags[t], "Retro Setup") == 0) {
                s->is_retro_setup = true;
                break;
            }
            for (int p = 0; p < TOTAL_PLATFORMS; p++) {
                if (strcasecmp(s->tags[t], g_platforms[p].name) == 0) {
                    s->is_retro_setup = true;
                    break;
                }
            }
            if (s->is_retro_setup) break;
        }
        if (!s->is_retro_setup && strstr(s->launch_options, "-L ") && strstr(s->launch_options, ".so")) {
            s->is_retro_setup = true;
        }

        if (s->is_retro_setup) {
            extract_rom_path_from_launch_options(s->launch_options, s->rom_path, sizeof(s->rom_path));
        }

        count++;
    }

    free(buffer);
    return count;
}

static bool write_shortcuts_vdf(const char* filepath, const SteamShortcut* shortcuts, int count) {
    char tmp_path[MAX_PATH_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%.2000s.tmp", filepath);

    FILE* f = fopen(tmp_path, "wb");
    if (!f) return false;

    // Helper macro to write byte
    #define WRITE_BYTE(b) do { uint8_t val = (uint8_t)(b); fwrite(&val, 1, 1, f); } while (0)
    #define WRITE_STR(s) do { const char* str = (s); if (!str) str = ""; fwrite(str, 1, strlen(str) + 1, f); } while (0)
    #define WRITE_FIELD_STR(k, v) do { WRITE_BYTE(0x01); WRITE_STR(k); WRITE_STR(v); } while (0)
    #define WRITE_FIELD_U32(k, v) do { \
        WRITE_BYTE(0x02); WRITE_STR(k); \
        uint32_t val32 = (uint32_t)(v); \
        uint8_t bytes[4] = { (uint8_t)(val32 & 0xFF), (uint8_t)((val32 >> 8) & 0xFF), (uint8_t)((val32 >> 16) & 0xFF), (uint8_t)((val32 >> 24) & 0xFF) }; \
        fwrite(bytes, 1, 4, f); \
    } while (0)

    // Root start
    WRITE_BYTE(0x00);
    WRITE_STR("shortcuts");

    int valid_idx = 0;
    for (int i = 0; i < count; i++) {
        if (!shortcuts[i].keep) continue;

        const SteamShortcut* s = &shortcuts[i];
        char idx_str[32];
        snprintf(idx_str, sizeof(idx_str), "%d", valid_idx++);

        WRITE_BYTE(0x00);
        WRITE_STR(idx_str);

        WRITE_FIELD_U32("appid", s->appid ? s->appid : compute_steam_appid(s->exe, s->app_name));
        WRITE_FIELD_STR("AppName", s->app_name);
        WRITE_FIELD_STR("Exe", s->exe);
        WRITE_FIELD_STR("StartDir", s->start_dir);
        WRITE_FIELD_STR("icon", s->icon);
        WRITE_FIELD_STR("ShortcutPath", s->shortcut_path);
        WRITE_FIELD_STR("LaunchOptions", s->launch_options);
        WRITE_FIELD_U32("IsHidden", s->is_hidden);
        WRITE_FIELD_U32("AllowDesktopConfig", s->allow_desktop_config);
        WRITE_FIELD_U32("AllowOverlay", s->allow_overlay);
        WRITE_FIELD_U32("OpenVR", s->open_vr);
        WRITE_FIELD_U32("Devkit", s->devkit);
        WRITE_FIELD_STR("DevkitGameID", s->devkit_game_id);
        WRITE_FIELD_U32("DevkitOverrideAppID", s->devkit_override_appid);
        WRITE_FIELD_U32("LastPlayTime", s->last_play_time);
        WRITE_FIELD_STR("FlatpakAppID", s->flatpak_appid);

        // Tags
        WRITE_BYTE(0x00);
        WRITE_STR("tags");
        for (int t = 0; t < s->tag_count; t++) {
            char tag_idx[16];
            snprintf(tag_idx, sizeof(tag_idx), "%d", t);
            WRITE_FIELD_STR(tag_idx, s->tags[t]);
        }
        WRITE_BYTE(0x08); // End tags

        WRITE_BYTE(0x08); // End entry
    }

    WRITE_BYTE(0x08); // End shortcuts
    WRITE_BYTE(0x08); // End document

    fclose(f);

    // Backup existing
    char bak_path[MAX_PATH_LEN];
    snprintf(bak_path, sizeof(bak_path), "%.2000s.bak", filepath);
    if (fs_exists(filepath)) {
        fs_copy_file(filepath, bak_path);
    }

    if (rename(tmp_path, filepath) != 0) {
        fs_copy_file(tmp_path, filepath);
        fs_remove_file(tmp_path);
    }
    return true;
}

static bool has_valid_game_ext(const char* file, const char* extensions) {
    if (!file) return false;
    char lower[512];
    snprintf(lower, sizeof(lower), "%s", file);
    for (int i = 0; lower[i]; i++) lower[i] = (char)tolower((unsigned char)lower[i]);

    if (strstr(lower, ".zip") || strstr(lower, ".7z")) return true;
    if (!extensions || !*extensions) return true;

    char ext_copy[256];
    snprintf(ext_copy, sizeof(ext_copy), "%s", extensions);
    char* token = strtok(ext_copy, " \t,");
    while (token) {
        char dot_ext[64];
        snprintf(dot_ext, sizeof(dot_ext), ".%s", token);
        for (int i = 0; dot_ext[i]; i++) dot_ext[i] = (char)tolower((unsigned char)dot_ext[i]);
        if (strstr(lower, dot_ext)) return true;
        token = strtok(NULL, " \t,");
    }
    return false;
}

// Scans ROM directory recursively for a platform
static void scan_roms_for_platform(const PlatformInfo* platform, const char* rom_dir,
                                   const char* ra_bin, const char* core_path,
                                   const char* ra_dir, bool use_gamemode,
                                   const char* steam_runtime_bin, const char* steam_app_id,
                                   bool allow_new,
                                   SteamShortcut* shortcuts, int* shortcut_count, int max_shortcuts) {
    DIR* d = opendir(rom_dir);
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || entry->d_name[0] == '.') continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", rom_dir, entry->d_name);

        if (fs_is_dir(full_path)) {
            scan_roms_for_platform(platform, full_path, ra_bin, core_path, ra_dir, use_gamemode,
                                   steam_runtime_bin, steam_app_id,
                                   allow_new,
                                   shortcuts, shortcut_count, max_shortcuts);
            continue;
        }

        if (!fs_is_file(full_path)) continue;
        if (strstr(entry->d_name, ".download") || strstr(entry->d_name, ".tmp") || strstr(entry->d_name, ".part")) continue;
        if (!has_valid_game_ext(entry->d_name, platform->extensions)) continue;

        char title[256];
        fs_get_basename_without_ext(title, sizeof(title), entry->d_name);

        // Boxart thumbnail icon
        char icon_path[MAX_PATH_LEN];
        snprintf(icon_path, sizeof(icon_path), "%s/thumbnails/%s/Named_Boxarts/%s.png", ra_dir, platform->name, title);
        if (!fs_exists(icon_path)) {
            icon_path[0] = '\0';
        }

        // Build command
        char exe_str[MAX_PATH_LEN];
        char launch_opts[MAX_PATH_LEN * 2];

        if (steam_runtime_bin && steam_runtime_bin[0] && steam_app_id && steam_app_id[0]) {
            snprintf(exe_str, sizeof(exe_str), "\"%s\"", steam_runtime_bin);
            snprintf(launch_opts, sizeof(launch_opts), "-applaunch %s -- -L \"%s\" \"%s\"",
                     steam_app_id, core_path, full_path);
        } else if (use_gamemode) {
            snprintf(exe_str, sizeof(exe_str), "gamemoderun");
            snprintf(launch_opts, sizeof(launch_opts), "\"%s\" -L \"%s\" \"%s\"", ra_bin, core_path, full_path);
        } else {
            snprintf(exe_str, sizeof(exe_str), "\"%s\"", ra_bin);
            snprintf(launch_opts, sizeof(launch_opts), "-L \"%s\" \"%s\"", core_path, full_path);
        }

        // Check if shortcut already exists (idempotency check)
        bool found = false;
        for (int i = 0; i < *shortcut_count; i++) {
            if (shortcuts[i].is_retro_setup) {
                // Match by exact ROM path or (AppName + Platform Tag)
                if ((shortcuts[i].rom_path[0] && strcmp(shortcuts[i].rom_path, full_path) == 0) ||
                    (strcmp(shortcuts[i].app_name, title) == 0 && strstr(shortcuts[i].launch_options, platform->core_file))) {
                    // Update existing shortcut
                    snprintf(shortcuts[i].exe, sizeof(shortcuts[i].exe), "%s", exe_str);
                    snprintf(shortcuts[i].start_dir, sizeof(shortcuts[i].start_dir), "\"%s\"", ra_dir);
                    snprintf(shortcuts[i].launch_options, sizeof(shortcuts[i].launch_options), "%s", launch_opts);
                    if (icon_path[0]) snprintf(shortcuts[i].icon, sizeof(shortcuts[i].icon), "%s", icon_path);
                    snprintf(shortcuts[i].rom_path, sizeof(shortcuts[i].rom_path), "%s", full_path);
                    shortcuts[i].appid = compute_steam_appid(exe_str, title);
                    shortcuts[i].keep = true;
                    found = true;
                    break;
                }
            }
        }

        if (!found && allow_new && *shortcut_count < max_shortcuts) {
            SteamShortcut* s = &shortcuts[*shortcut_count];
            memset(s, 0, sizeof(SteamShortcut));
            snprintf(s->app_name, sizeof(s->app_name), "%s", title);
            snprintf(s->exe, sizeof(s->exe), "%s", exe_str);
            snprintf(s->start_dir, sizeof(s->start_dir), "\"%s\"", ra_dir);
            snprintf(s->launch_options, sizeof(s->launch_options), "%s", launch_opts);
            if (icon_path[0]) snprintf(s->icon, sizeof(s->icon), "%s", icon_path);
            snprintf(s->rom_path, sizeof(s->rom_path), "%s", full_path);
            s->allow_desktop_config = 1;
            s->allow_overlay = 1;
            s->is_retro_setup = true;
            s->keep = true;
            s->appid = compute_steam_appid(exe_str, title);

            // Tags
            snprintf(s->tags[0], sizeof(s->tags[0]), "%s", platform->name);
            snprintf(s->tags[1], sizeof(s->tags[1]), "Retro Setup");
            s->tag_count = 2;

            (*shortcut_count)++;
        }
    }
    closedir(d);
}

// Find all userdata directories and sync shortcuts.vdf in each
bool steam_shortcuts_sync(const char* ra_dir, const char* rom_base_dir) {
    char home[MAX_PATH_LEN];
    get_home_dir(home, sizeof(home));

    char ra_bin[MAX_PATH_LEN];
    steam_find_retroarch_bin(ra_bin, sizeof(ra_bin), ra_dir);
    bool use_gamemode = steam_has_gamemode();
    char steam_runtime_bin[MAX_PATH_LEN] = {0};
    char steam_app_id[64] = {0};
    bool use_steam_runtime = steam_find_runtime_launcher(ra_dir, steam_runtime_bin,
                                                         sizeof(steam_runtime_bin),
                                                         steam_app_id, sizeof(steam_app_id));

    const char* steam_roots[] = {
        "/.local/share/Steam/userdata",
        "/.steam/steam/userdata",
        "/.steam/root/userdata",
        "/.var/app/com.valvesoftware.Steam/data/Steam/userdata",
        "/.var/app/com.valvesoftware.Steam/.local/share/Steam/userdata",
        NULL
    };

    bool any_synced = false;
    char target_dirs[16][MAX_PATH_LEN];
    int target_dir_count = 0;

    for (int r = 0; steam_roots[r]; r++) {
        char uroot[MAX_PATH_LEN];
        fs_join_path(uroot, sizeof(uroot), home, steam_roots[r]);
        if (!fs_is_dir(uroot)) continue;

        DIR* ud = opendir(uroot);
        if (!ud) continue;

        struct dirent* ue;
        while ((ue = readdir(ud)) != NULL) {
            if (ue->d_name[0] == '.') continue;
            // Check if numeric user ID directory
            bool is_num = true;
            for (int i = 0; ue->d_name[i]; i++) {
                if (!isdigit((unsigned char)ue->d_name[i])) { is_num = false; break; }
            }
            if (!is_num) continue;

            char user_dir[MAX_PATH_LEN], cfg_dir[MAX_PATH_LEN];
            fs_join_path(user_dir, sizeof(user_dir), uroot, ue->d_name);
            fs_join_path(cfg_dir, sizeof(cfg_dir), user_dir, "config");
            fs_mkdir_p(cfg_dir);

            // Add unique target dir
            bool already_added = false;
            for (int i = 0; i < target_dir_count; i++) {
                if (strcmp(target_dirs[i], cfg_dir) == 0) { already_added = true; break; }
            }
            if (!already_added && target_dir_count < 16) {
                snprintf(target_dirs[target_dir_count++], sizeof(target_dirs[0]), "%s", cfg_dir);
            }
        }
        closedir(ud);
    }

    if (target_dir_count == 0) {
        // Fallback default userdata directory
        char default_cfg[MAX_PATH_LEN];
        fs_join_path(default_cfg, sizeof(default_cfg), home, ".local/share/Steam/userdata/0/config");
        fs_mkdir_p(default_cfg);
        snprintf(target_dirs[0], sizeof(target_dirs[0]), "%s", default_cfg);
        target_dir_count = 1;
    }

    for (int td = 0; td < target_dir_count; td++) {
        char vdf_path[MAX_PATH_LEN];
        fs_join_path(vdf_path, sizeof(vdf_path), target_dirs[td], "shortcuts.vdf");

        SteamShortcut* shortcuts = calloc(MAX_STEAM_SHORTCUTS, sizeof(SteamShortcut));
        if (!shortcuts) continue;

        int shortcut_count = 0;
        if (fs_exists(vdf_path)) {
            shortcut_count = parse_shortcuts_vdf(vdf_path, shortcuts, MAX_STEAM_SHORTCUTS);
        }

        // For existing Retro Setup shortcuts: check if ROM still exists on disk
        for (int i = 0; i < shortcut_count; i++) {
            if (shortcuts[i].is_retro_setup) {
                if (shortcuts[i].rom_path[0] && !fs_exists(shortcuts[i].rom_path)) {
                    shortcuts[i].keep = false; // Remove stale ROM shortcut cleanly
                }
            }
        }

        // Scan every installed ROM directory. Selection controls installation,
        // not maintenance of shortcuts that are already part of the library.
        int games_added_or_updated = 0;
        for (int p = 0; p < TOTAL_PLATFORMS; p++) {
            char plat_rom_dir[MAX_PATH_LEN];
            fs_join_path(plat_rom_dir, sizeof(plat_rom_dir), rom_base_dir, g_platforms[p].id);
            if (!fs_is_dir(plat_rom_dir)) continue;

            char core_path[MAX_PATH_LEN];
            platform_resolve_core(&g_platforms[p], ra_dir, NULL, 0, NULL, 0, core_path, sizeof(core_path));

            int prev_count = shortcut_count;
            scan_roms_for_platform(&g_platforms[p], plat_rom_dir, ra_bin, core_path, ra_dir,
                                   use_gamemode, use_steam_runtime ? steam_runtime_bin : NULL,
                                   use_steam_runtime ? steam_app_id : NULL,
                                   g_platforms[p].selected,
                                   shortcuts, &shortcut_count, MAX_STEAM_SHORTCUTS);
            games_added_or_updated += (shortcut_count - prev_count);
        }

        if (write_shortcuts_vdf(vdf_path, shortcuts, shortcut_count)) {
            log_add(LOG_LEVEL_INFO, "[STEAM] Synchronized shortcuts in %s (%d total shortcuts, %d added/updated, launcher: %s)",
                    vdf_path, shortcut_count, games_added_or_updated,
                    use_steam_runtime ? "Steam Runtime" : (use_gamemode ? "GameMode" : "direct"));
            any_synced = true;
        }

        free(shortcuts);
    }

    return any_synced;
}
