#include "config_url_parser.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ENTRIES 128
static UrlArrayEntry g_entries[MAX_ENTRIES];
static int g_entry_count = 0;

static char* trim(char* text) {
    while (*text && isspace((unsigned char)*text)) text++;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}

static void ini_key(const char* section, const char* name, char* out, size_t out_size) {
    if (strcmp(section, "global") == 0) {
        if (strcmp(name, "core_info_url") == 0) snprintf(out, out_size, "CORE_INFO_URL");
        else if (strcmp(name, "libretro_core_base_url") == 0) snprintf(out, out_size, "LIBRETRO_CORE_BASE_URL");
        else if (strcmp(name, "database_rdb_url") == 0) snprintf(out, out_size, "DATABASE_RDB_URL");
        else if (strcmp(name, "database_cursors_url") == 0) snprintf(out, out_size, "DATABASE_CURSORS_URL");
        else if (strcmp(name, "thumbnails_base_url") == 0) snprintf(out, out_size, "THUMBNAILS_BASE_URL");
        else if (strcmp(name, "archive_download_base_url") == 0) snprintf(out, out_size, "ARCHIVE_DOWNLOAD_BASE_URL");
        else out[0] = 0;
        return;
    }
    if (strcmp(name, "bios_url") == 0) snprintf(out, out_size, "BIOS_URLS_%s", section);
    else if (strcmp(name, "rom_url") == 0) snprintf(out, out_size, "ROM_URLS_%s", section);
    else if (strcmp(name, "rom_directory_url") == 0) snprintf(out, out_size, "ROM_DIR_URLS_%s", section);
    else if (strcmp(name, "rom_catalog_url") == 0) snprintf(out, out_size, "ROM_CATALOG_URLS_%s", section);
    else if (strcmp(name, "rom_catalog_include") == 0) snprintf(out, out_size, "ROM_CATALOG_INCLUDES_%s", section);
    else if (strcmp(name, "archive_item") == 0) snprintf(out, out_size, "ARCHIVE_COMPRESS_URLS_%s", section);
    else out[0] = 0;
}

static void extract_urls_for_key(const char* key, const char* str) {
    if (!key || !*key || !str || !*str) return;

    // Find existing key entry or create new
    int entry_idx = -1;
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0) {
            entry_idx = i;
            break;
        }
    }
    if (entry_idx < 0) {
        if (g_entry_count >= MAX_ENTRIES) return;
        entry_idx = g_entry_count++;
        snprintf(g_entries[entry_idx].key, sizeof(g_entries[entry_idx].key), "%s", key);
        g_entries[entry_idx].url_count = 0;
    }

    const char* p = str;
    while (*p) {
        const char* http = strstr(p, "http");
        if (!http) break;

        char clean_url[MAX_URL_LEN];
        size_t ulen = 0;
        const char* q = http;
        while (*q && *q != '"' && *q != '\'' && *q != ' ' && *q != '\t' && *q != ')' && *q != '\r' && *q != '\n') {
            if (ulen < MAX_URL_LEN - 1) {
                clean_url[ulen++] = *q;
            }
            q++;
        }
        clean_url[ulen] = 0;

        if (clean_url[0] && g_entries[entry_idx].url_count < MAX_URLS_PER_KEY) {
            bool dup = false;
            for (int k = 0; k < g_entries[entry_idx].url_count; k++) {
                if (strcmp(g_entries[entry_idx].urls[k], clean_url) == 0) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                snprintf(g_entries[entry_idx].urls[g_entries[entry_idx].url_count++], MAX_URL_LEN, "%s", clean_url);
            }
        }
        p = q;
    }
}

static void append_literal_for_key(const char* key, const char* value) {
    if (!key || !key[0] || !value || !value[0]) return;
    int entry_idx = -1;
    for (int i = 0; i < g_entry_count; ++i) {
        if (strcmp(g_entries[i].key, key) == 0) { entry_idx = i; break; }
    }
    if (entry_idx < 0) {
        if (g_entry_count >= MAX_ENTRIES) return;
        entry_idx = g_entry_count++;
        snprintf(g_entries[entry_idx].key, sizeof(g_entries[entry_idx].key), "%s", key);
    }
    if (g_entries[entry_idx].url_count >= MAX_URLS_PER_KEY) return;
    for (int i = 0; i < g_entries[entry_idx].url_count; ++i)
        if (strcmp(g_entries[entry_idx].urls[i], value) == 0) return;
    snprintf(g_entries[entry_idx].urls[g_entries[entry_idx].url_count++], MAX_URL_LEN, "%s", value);
}

bool url_config_load(const char* filepath) {
    g_entry_count = 0;
    if (!filepath || !*filepath) return false;

    FILE* f = fopen(filepath, "r");
    if (!f) {
        log_add(LOG_LEVEL_WARN, "Could not open URL config file: %s", filepath);
        return false;
    }

    char line[4096];
    char current_key[128] = {0};
    char current_section[128] = {0};
    bool in_array = false;

    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        if (*p == '[') {
            char* close = strchr(p + 1, ']');
            if (close) {
                *close = 0;
                snprintf(current_section, sizeof(current_section), "%s", trim(p + 1));
            }
            continue;
        }

        if (in_array) {
            extract_urls_for_key(current_key, p);
            if (strchr(p, ')')) {
                in_array = false;
                current_key[0] = 0;
            }
            continue;
        }

        char* eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            char key[128];
            snprintf(key, sizeof(key), "%.127s", p);

            // Trim trailing space from key
            size_t klen = strlen(key);
            while (klen > 0 && (key[klen - 1] == ' ' || key[klen - 1] == '\t')) key[--klen] = 0;

            char* val = eq + 1;
            val = trim(val);

            if (current_section[0]) {
                char mapped_key[128];
                char* comment = strchr(val, '#');
                if (comment && (comment == val || isspace((unsigned char)comment[-1]))) *comment = 0;
                val = trim(val);
                size_t vlen = strlen(val);
                if (vlen >= 2 && ((val[0] == '"' && val[vlen - 1] == '"') ||
                                  (val[0] == '\'' && val[vlen - 1] == '\''))) {
                    val[vlen - 1] = 0;
                    val++;
                }
                ini_key(current_section, key, mapped_key, sizeof(mapped_key));
                if (mapped_key[0] && val[0]) {
                    if (strncmp(mapped_key, "ROM_CATALOG_INCLUDES_", 21) == 0)
                        append_literal_for_key(mapped_key, val);
                    else
                        extract_urls_for_key(mapped_key, val);
                }
                continue;
            }

            if (*val == '(') {
                // Bash array syntax: key=( ... )
                snprintf(current_key, sizeof(current_key), "%s", key);
                extract_urls_for_key(current_key, val);

                if (strchr(val, ')')) {
                    in_array = false;
                    current_key[0] = 0;
                } else {
                    in_array = true;
                }
            } else {
                // Key-value pair
                extract_urls_for_key(key, val);
            }
        }
    }

    fclose(f);
    log_add(LOG_LEVEL_INFO, "Loaded %d config key entries from %s", g_entry_count, filepath);
    return true;
}

int url_config_get_urls(const char* key, char out_urls[][MAX_URL_LEN], int max_urls) {
    if (!key || !out_urls || max_urls <= 0) return 0;

    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0) {
            int count = (g_entries[i].url_count < max_urls) ? g_entries[i].url_count : max_urls;
            for (int k = 0; k < count; k++) {
                snprintf(out_urls[k], MAX_URL_LEN, "%s", g_entries[i].urls[k]);
            }
            return count;
        }
    }
    return 0;
}

const char* url_config_get_string(const char* key, const char* default_val) {
    if (!key) return default_val;
    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].key, key) == 0 && g_entries[i].url_count > 0) {
            return g_entries[i].urls[0];
        }
    }
    return default_val;
}

int url_config_get_entry_count(void) { return g_entry_count; }
const UrlArrayEntry* url_config_get_entry(int index) {
    return (index >= 0 && index < g_entry_count) ? &g_entries[index] : NULL;
}
