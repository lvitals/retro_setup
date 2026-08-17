#include "platform_data.h"
#include "config.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>

PlatformInfo g_platforms[MAX_PLATFORMS];
int g_platform_count = 0;

CategoryInfo g_categories[MAX_CATEGORIES];
int g_category_count = 0;

static unsigned int parse_color_name_or_hex(const char* str) {
    if (!str || !*str) return 0;

    if (str[0] == '#') {
        return (unsigned int)strtoul(str + 1, NULL, 16);
    }
    if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0) {
        return (unsigned int)strtoul(str + 2, NULL, 16);
    }

    char lower[32];
    size_t len = strlen(str);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; i++) lower[i] = (char)tolower((unsigned char)str[i]);
    lower[len] = 0;

    if (strcmp(lower, "red") == 0) return 0xE60012;
    if (strcmp(lower, "blue") == 0) return 0x0050FF;
    if (strcmp(lower, "lightblue") == 0 || strcmp(lower, "cyan") == 0) return 0x0080FF;
    if (strcmp(lower, "yellow") == 0 || strcmp(lower, "gold") == 0) return 0xFFB703;
    if (strcmp(lower, "green") == 0 || strcmp(lower, "lime") == 0) return 0x39FF14;
    if (strcmp(lower, "purple") == 0 || strcmp(lower, "violet") == 0) return 0x9D4EDD;
    if (strcmp(lower, "orange") == 0) return 0xFF7000;
    if (strcmp(lower, "pink") == 0 || strcmp(lower, "magenta") == 0) return 0xFF007F;
    if (strcmp(lower, "gray") == 0 || strcmp(lower, "grey") == 0) return 0x6C757D;
    if (strcmp(lower, "darkred") == 0) return 0xD90429;
    if (strcmp(lower, "navy") == 0) return 0x001F3F;

    if (strlen(lower) == 6) {
        return (unsigned int)strtoul(lower, NULL, 16);
    }

    return 0;
}

int get_platform_index_by_id(const char* id) {
    if (!id) return -1;
    for (int i = 0; i < g_platform_count; i++) {
        if (strcmp(g_platforms[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void add_or_update_platform(const char* id, const char* name, const char* category,
                                   const char* core_file, const char* core_name,
                                   const char* extensions, const char* bios_files,
                                   unsigned int color_hex) {
    if (!id || !*id) return;

    int idx = get_platform_index_by_id(id);
    if (idx < 0) {
        if (g_platform_count >= MAX_PLATFORMS) return;
        idx = g_platform_count++;
        memset(&g_platforms[idx], 0, sizeof(PlatformInfo));
    }

    snprintf(g_platforms[idx].id, sizeof(g_platforms[idx].id), "%s", id);
    if (name && *name) snprintf(g_platforms[idx].name, sizeof(g_platforms[idx].name), "%s", name);
    if (category && *category) snprintf(g_platforms[idx].category, sizeof(g_platforms[idx].category), "%s", category);
    if (core_file && *core_file) snprintf(g_platforms[idx].core_file, sizeof(g_platforms[idx].core_file), "%s", core_file);
    if (core_name && *core_name) snprintf(g_platforms[idx].core_name, sizeof(g_platforms[idx].core_name), "%s", core_name);
    if (extensions && *extensions) snprintf(g_platforms[idx].extensions, sizeof(g_platforms[idx].extensions), "%s", extensions);
    if (bios_files) snprintf(g_platforms[idx].bios_files, sizeof(g_platforms[idx].bios_files), "%s", bios_files);

    if (g_platforms[idx].category[0] == 0) {
        snprintf(g_platforms[idx].category, sizeof(g_platforms[idx].category), "General");
    }

    if (color_hex != 0) {
        g_platforms[idx].color_hex = color_hex;
    } else if (g_platforms[idx].color_hex == 0) {
        g_platforms[idx].color_hex = 0x4A4E69;
    }
}

static void remove_platform_by_id(const char* id) {
    int idx = get_platform_index_by_id(id);
    if (idx < 0) return;

    for (int i = idx; i < g_platform_count - 1; i++) {
        g_platforms[i] = g_platforms[i + 1];
    }
    g_platform_count--;
}

static bool extension_is_allowed(const char* filename, const char* extensions) {
    if (!extensions || !extensions[0]) return true;
    const char* dot = strrchr(filename, '.');
    if (!dot || !dot[1]) return false;

    char allowed[sizeof(((PlatformInfo*)0)->bios_extensions)];
    snprintf(allowed, sizeof(allowed), "%s", extensions);
    char* save = NULL;
    for (char* ext = strtok_r(allowed, " ,;", &save); ext; ext = strtok_r(NULL, " ,;", &save)) {
        if (ext[0] == '.') ext++;
        if (strcasecmp(dot + 1, ext) == 0) return true;
    }
    return false;
}

bool platform_bios_path_valid(const PlatformInfo* p, const char* path) {
    if (!p || !path) return false;
    if (fs_is_file(path)) {
        return extension_is_allowed(path, p->bios_extensions) &&
               fs_file_size(path) >= p->bios_min_size;
    }
    if (!fs_is_dir(path)) return false;

    DIR* dir = opendir(path);
    if (!dir) return false;
    bool valid = false;
    struct dirent* entry;
    while (!valid && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char child[MAX_PATH_LEN];
        fs_join_path(child, sizeof(child), path, entry->d_name);
        valid = platform_bios_path_valid(p, child);
    }
    closedir(dir);
    return valid;
}

void platform_data_load_custom(const char* config_path) {
    if (!config_path) return;
    FILE* f = fopen(config_path, "r");
    if (!f) return;

    char line[1024];
    char current_section_id[64] = {0};

    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n' || *p == 0) continue;

        char* nl = strchr(p, '\r');
        if (nl) *nl = 0;
        nl = strchr(p, '\n');
        if (nl) *nl = 0;

        // Check for INI section block [platform_id]
        if (*p == '[') {
            char* end_bracket = strchr(p, ']');
            if (end_bracket) {
                *end_bracket = 0;
                snprintf(current_section_id, sizeof(current_section_id), "%.63s", p + 1);
                add_or_update_platform(current_section_id, NULL, NULL, NULL, NULL, NULL, NULL, 0);
                continue;
            }
        }

        // Check for key = value pair inside INI section or global PLATFORM_ lines
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;

        char key[128];
        snprintf(key, sizeof(key), "%.127s", p);
        // Trim trailing space from key
        size_t klen = strlen(key);
        while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) key[--klen] = 0;

        char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (*val == '"' || *val == '\'') val++;
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen - 1] == '"' || val[vlen - 1] == '\'' || val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) {
            val[--vlen] = 0;
        }

        if (strncmp(key, "PLATFORM_", 9) == 0) {
            const char* var_name = key + 9;
            if (strncmp(var_name, "REMOVE_", 7) == 0) {
                remove_platform_by_id(var_name + 7);
                continue;
            }

            char name[128] = {0}, category[64] = {0}, core_file[128] = {0}, core_name[128] = {0};
            char extensions[128] = {0}, bios_files[256] = {0}, color_str[32] = {0};

            char vcopy[1024];
            snprintf(vcopy, sizeof(vcopy), "%s", val);

            int field_idx = 0;
            char* tok = strtok(vcopy, "|");
            while (tok) {
                switch (field_idx) {
                    case 0: snprintf(name, sizeof(name), "%.127s", tok); break;
                    case 1: snprintf(category, sizeof(category), "%.63s", tok); break;
                    case 2: snprintf(core_file, sizeof(core_file), "%.127s", tok); break;
                    case 3: snprintf(core_name, sizeof(core_name), "%.127s", tok); break;
                    case 4: snprintf(extensions, sizeof(extensions), "%.127s", tok); break;
                    case 5: snprintf(bios_files, sizeof(bios_files), "%.255s", tok); break;
                    case 6: snprintf(color_str, sizeof(color_str), "%.31s", tok); break;
                }
                field_idx++;
                tok = strtok(NULL, "|");
            }

            unsigned int color_hex = parse_color_name_or_hex(color_str);
            add_or_update_platform(var_name, name, category, core_file, core_name, extensions, bios_files, color_hex);
        } else if (current_section_id[0] != 0) {
            // Key inside [platform_id] section
            if (strcmp(key, "name") == 0 || strcmp(key, "title") == 0) {
                add_or_update_platform(current_section_id, val, NULL, NULL, NULL, NULL, NULL, 0);
            } else if (strcmp(key, "category") == 0 || strcmp(key, "group") == 0) {
                add_or_update_platform(current_section_id, NULL, val, NULL, NULL, NULL, NULL, 0);
            } else if (strcmp(key, "core_file") == 0 || strcmp(key, "core") == 0) {
                add_or_update_platform(current_section_id, NULL, NULL, val, NULL, NULL, NULL, 0);
            } else if (strcmp(key, "core_name") == 0) {
                add_or_update_platform(current_section_id, NULL, NULL, NULL, val, NULL, NULL, 0);
            } else if (strcmp(key, "core_config_name") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].core_config_name,
                                       sizeof(g_platforms[idx].core_config_name), "%s", val);
            } else if (strcmp(key, "core_options") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].core_options,
                                       sizeof(g_platforms[idx].core_options), "%s", val);
            } else if (strcmp(key, "frontend_options") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].frontend_options,
                                       sizeof(g_platforms[idx].frontend_options), "%s", val);
            } else if (strcmp(key, "use_gamemode") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) g_platforms[idx].use_gamemode =
                    strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0;
            } else if (strcmp(key, "fallback_core_file") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].fallback_core_file,
                                       sizeof(g_platforms[idx].fallback_core_file), "%s", val);
            } else if (strcmp(key, "fallback_core_name") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].fallback_core_name,
                                       sizeof(g_platforms[idx].fallback_core_name), "%s", val);
            } else if (strcmp(key, "fallback_core_without_bios") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) g_platforms[idx].fallback_core_without_bios =
                    strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0;
            } else if (strcmp(key, "fallback_core_config_name") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].fallback_core_config_name,
                                       sizeof(g_platforms[idx].fallback_core_config_name), "%s", val);
            } else if (strcmp(key, "fallback_core_options") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].fallback_core_options,
                                       sizeof(g_platforms[idx].fallback_core_options), "%s", val);
            } else if (strcmp(key, "fallback_frontend_options") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].fallback_frontend_options,
                                       sizeof(g_platforms[idx].fallback_frontend_options), "%s", val);
            } else if (strcmp(key, "extensions") == 0 || strcmp(key, "ext") == 0) {
                add_or_update_platform(current_section_id, NULL, NULL, NULL, NULL, val, NULL, 0);
            } else if (strcmp(key, "bios_files") == 0 || strcmp(key, "bios") == 0) {
                add_or_update_platform(current_section_id, NULL, NULL, NULL, NULL, NULL, val, 0);
            } else if (strcmp(key, "bios_extensions") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].bios_extensions,
                                       sizeof(g_platforms[idx].bios_extensions), "%s", val);
            } else if (strcmp(key, "bios_copy_extensions") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].bios_copy_extensions,
                                       sizeof(g_platforms[idx].bios_copy_extensions), "%s", val);
            } else if (strcmp(key, "bios_install_directory") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].bios_install_directory,
                                       sizeof(g_platforms[idx].bios_install_directory), "%s", val);
            } else if (strcmp(key, "bios_min_size") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) g_platforms[idx].bios_min_size = strtoll(val, NULL, 10);
            } else if (strcmp(key, "bios_missing_action") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].bios_missing_action,
                                       sizeof(g_platforms[idx].bios_missing_action), "%s", val);
            } else if (strcmp(key, "obsolete_config_files") == 0) {
                int idx = get_platform_index_by_id(current_section_id);
                if (idx >= 0) snprintf(g_platforms[idx].obsolete_config_files,
                                       sizeof(g_platforms[idx].obsolete_config_files), "%s", val);
            } else if (strcmp(key, "color") == 0) {
                unsigned int color_hex = parse_color_name_or_hex(val);
                add_or_update_platform(current_section_id, NULL, NULL, NULL, NULL, NULL, NULL, color_hex);
            }
        }
    }
    fclose(f);
}

void platform_data_build_categories(void) {
    g_category_count = 0;

    // Index 0 is always ALL
    snprintf(g_categories[0].name, sizeof(g_categories[0].name), "ALL PLATFORMS");
    g_categories[0].color_hex = 0x4A4E69;
    g_category_count = 1;

    for (int i = 0; i < g_platform_count; i++) {
        const char* cat = g_platforms[i].category;
        if (!cat || !*cat) continue;

        bool found = false;
        for (int c = 1; c < g_category_count; c++) {
            if (strcmp(g_categories[c].name, cat) == 0) {
                found = true;
                break;
            }
        }

        if (!found && g_category_count < MAX_CATEGORIES) {
            snprintf(g_categories[g_category_count].name, sizeof(g_categories[g_category_count].name), "%s", cat);
            g_categories[g_category_count].color_hex = g_platforms[i].color_hex;
            g_category_count++;
        }
    }
}

void platform_data_init(void) {
    g_platform_count = 0;
    memset(g_platforms, 0, sizeof(g_platforms));
    memset(g_categories, 0, sizeof(g_categories));
    g_category_count = 0;
}

void reset_all_selections(bool select_state) {
    for (int i = 0; i < g_platform_count; i++) {
        g_platforms[i].selected = select_state;
    }
}

void select_by_category(const char* category_name, bool select_state) {
    if (!category_name) return;
    bool is_all = (strcmp(category_name, "ALL PLATFORMS") == 0 || strcmp(category_name, "ALL") == 0);

    for (int i = 0; i < g_platform_count; i++) {
        if (is_all || strcmp(g_platforms[i].category, category_name) == 0) {
            g_platforms[i].selected = select_state;
        }
    }
}

bool platform_has_valid_bios(const PlatformInfo* p, const char* ra_dir) {
    if (!p || !ra_dir || !p->bios_files[0]) return true;
    char system_dir[MAX_PATH_LEN];
    fs_join_path(system_dir, sizeof(system_dir), ra_dir, "system");
    char requirements[sizeof(p->bios_files)];
    snprintf(requirements, sizeof(requirements), "%s", p->bios_files);
    char* save = NULL;
    for (char* entry = strtok_r(requirements, " ,;", &save); entry;
         entry = strtok_r(NULL, " ,;", &save)) {
        char path[MAX_PATH_LEN];
        fs_join_path(path, sizeof(path), system_dir, entry);
        if (!platform_bios_path_valid(p, path)) return false;
    }
    return true;
}

void platform_get_preferred_core(const PlatformInfo* p, const char* ra_dir,
                                 char* out_file, size_t file_size,
                                 char* out_name, size_t name_size) {
    if (!p) return;
    bool fallback = p->fallback_core_without_bios && p->fallback_core_file[0] &&
                    !platform_has_valid_bios(p, ra_dir);
    if (out_file && file_size)
        snprintf(out_file, file_size, "%s", fallback ? p->fallback_core_file : p->core_file);
    if (out_name && name_size)
        snprintf(out_name, name_size, "%s", fallback ? p->fallback_core_name : p->core_name);
}

bool platform_resolve_core(const PlatformInfo* p, const char* ra_dir, char* out_file, size_t file_size, char* out_name, size_t name_size, char* out_path, size_t path_size) {
    if (!p || !ra_dir) return false;

    char core_file[128] = {0}, core_name[128] = {0};
    platform_get_preferred_core(p, ra_dir, core_file, sizeof(core_file), core_name, sizeof(core_name));

    char cores_dir[MAX_PATH_LEN];
    fs_join_path(cores_dir, sizeof(cores_dir), ra_dir, "cores");
    char full_path[MAX_PATH_LEN];
    fs_join_path(full_path, sizeof(full_path), cores_dir, core_file);

    bool found = fs_exists(full_path) && fs_file_size(full_path) > 0;

    if (out_file && file_size > 0) snprintf(out_file, file_size, "%s", core_file);
    if (out_name && name_size > 0) snprintf(out_name, name_size, "%s", core_name);
    if (out_path && path_size > 0) snprintf(out_path, path_size, "%s", full_path);

    return found;
}
