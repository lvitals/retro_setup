#include "config_url_parser.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 128
static UrlArrayEntry g_entries[MAX_ENTRIES];
static int g_entry_count = 0;

static void clean_token(char* out, size_t out_size, const char* in) {
    if (!out || out_size == 0) return;
    if (!in) { out[0] = 0; return; }

    const char* p = in;
    while (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'') p++;

    size_t pos = 0;
    while (*p && pos < out_size - 1) {
        if (*p == '"' || *p == '\'' || *p == '\n' || *p == '\r') break;
        out[pos++] = *p++;
    }
    out[pos] = 0;
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
    char current_array_key[128] = {0};
    bool in_array = false;

    while (fgets(line, sizeof(line), f)) {
        // Strip leading whitespace
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        if (in_array) {
            if (strchr(p, ')')) {
                in_array = false;
                current_array_key[0] = 0;
                continue;
            }

            char clean_url[MAX_URL_LEN];
            clean_token(clean_url, sizeof(clean_url), p);
            if (clean_url[0] && strncmp(clean_url, "http", 4) == 0) {
                for (int i = 0; i < g_entry_count; i++) {
                    if (strcmp(g_entries[i].key, current_array_key) == 0) {
                        if (g_entries[i].url_count < MAX_URLS_PER_KEY) {
                            snprintf(g_entries[i].urls[g_entries[i].url_count++], MAX_URL_LEN, "%s", clean_url);
                        }
                        break;
                    }
                }
            }
            continue;
        }

        char* eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            char key[128];
            clean_token(key, sizeof(key), p);
            char* val = eq + 1;
            while (*val == ' ' || *val == '\t') val++;

            if (*val == '(') {
                // Bash array start
                in_array = true;
                snprintf(current_array_key, sizeof(current_array_key), "%s", key);

                if (g_entry_count < MAX_ENTRIES) {
                    snprintf(g_entries[g_entry_count].key, sizeof(g_entries[g_entry_count].key), "%s", key);
                    g_entries[g_entry_count].url_count = 0;
                    g_entry_count++;
                }

                if (strchr(val, ')')) {
                    in_array = false;
                    current_array_key[0] = 0;
                }
            } else {
                // Key-value pair
                char clean_val[MAX_URL_LEN];
                clean_token(clean_val, sizeof(clean_val), val);

                if (g_entry_count < MAX_ENTRIES) {
                    snprintf(g_entries[g_entry_count].key, sizeof(g_entries[g_entry_count].key), "%s", key);
                    snprintf(g_entries[g_entry_count].urls[0], MAX_URL_LEN, "%s", clean_val);
                    g_entries[g_entry_count].url_count = 1;
                    g_entry_count++;
                }
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
