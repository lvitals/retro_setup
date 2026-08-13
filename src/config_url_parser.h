#ifndef CONFIG_URL_PARSER_H
#define CONFIG_URL_PARSER_H

#include <stdbool.h>

#define MAX_URLS_PER_KEY 32
#define MAX_URL_LEN 1024

typedef struct {
    char key[128];
    char urls[MAX_URLS_PER_KEY][MAX_URL_LEN];
    int url_count;
} UrlArrayEntry;

bool url_config_load(const char* filepath);
int url_config_get_urls(const char* key, char out_urls[][MAX_URL_LEN], int max_urls);
const char* url_config_get_string(const char* key, const char* default_val);
int url_config_get_entry_count(void);
const UrlArrayEntry* url_config_get_entry(int index);

#endif // CONFIG_URL_PARSER_H
