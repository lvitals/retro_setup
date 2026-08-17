#include "rom_catalog.h"
#include "config_url_parser.h"
#include "platform_data.h"
#include "log.h"
#include "fs.h"
#include "config.h"
#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define ROM_CATALOG_CACHE_MAX_AGE_SECONDS (24 * 60 * 60)

typedef struct {
    char* data;
    size_t size;
} MemoryBuffer;

static RomCatalogGame g_games[ROM_CATALOG_MAX_GAMES];
static int g_game_count;

static bool load_cached_response(const char* path, MemoryBuffer* response) {
    struct stat info;
    if (stat(path, &info) != 0 || time(NULL) - info.st_mtime > ROM_CATALOG_CACHE_MAX_AGE_SECONDS) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return false; }
    response->data = malloc((size_t)length + 1);
    if (!response->data) { fclose(file); return false; }
    response->size = fread(response->data, 1, (size_t)length, file);
    fclose(file);
    response->data[response->size] = 0;
    if (response->size != (size_t)length) {
        free(response->data);
        response->data = NULL;
        response->size = 0;
        return false;
    }
    return true;
}

static void save_cached_response(const char* path, const MemoryBuffer* response) {
    FILE* file = fopen(path, "wb");
    if (!file) return;
    fwrite(response->data, 1, response->size, file);
    fclose(file);
}

static void catalog_cache_path(char* out, size_t out_size, const char* selection_file,
                               const char* metadata_url) {
    char config_dir[MAX_PATH_LEN];
    snprintf(config_dir, sizeof(config_dir), "%s", selection_file);
    char* slash = strrchr(config_dir, '/');
    if (slash) *slash = 0;
    else snprintf(config_dir, sizeof(config_dir), ".");
    char cache_dir[MAX_PATH_LEN];
    fs_join_path(cache_dir, sizeof(cache_dir), config_dir, "catalog_cache");
    fs_mkdir_p(cache_dir);
    const char* identifier = strrchr(metadata_url, '/');
    identifier = identifier ? identifier + 1 : metadata_url;
    char filename[256];
    snprintf(filename, sizeof(filename), "%.240s.json", identifier);
    fs_join_path(out, out_size, cache_dir, filename);
}

static size_t append_response(void* contents, size_t size, size_t count, void* user_data) {
    size_t bytes = size * count;
    MemoryBuffer* buffer = user_data;
    char* expanded = realloc(buffer->data, buffer->size + bytes + 1);
    if (!expanded) return 0;
    buffer->data = expanded;
    memcpy(buffer->data + buffer->size, contents, bytes);
    buffer->size += bytes;
    buffer->data[buffer->size] = 0;
    return bytes;
}

static bool has_platform_extension(const PlatformInfo* platform, const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || !dot[1]) return false;
    char extensions[sizeof(platform->extensions)];
    snprintf(extensions, sizeof(extensions), "%s", platform->extensions);
    char* save = NULL;
    for (char* ext = strtok_r(extensions, " ,;", &save); ext; ext = strtok_r(NULL, " ,;", &save)) {
        if (strcasecmp(dot + 1, ext) == 0) return true;
    }
    return false;
}

static bool matches_any_filter(const char* name,
                               char filters[MAX_URLS_PER_KEY][MAX_URL_LEN], int filter_count) {
    if (filter_count == 0) return true;
    for (int i = 0; i < filter_count; ++i)
        if (filters[i][0] && strcasestr(name, filters[i])) return true;
    return false;
}

static bool was_selected(const char* selection_file, const char* platform_id, const char* url) {
    FILE* file = fopen(selection_file, "r");
    if (!file) return false;
    char line[4096];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        char* newline = strpbrk(line, "\r\n");
        if (newline) *newline = 0;
        char* separator = strchr(line, '|');
        if (!separator) continue;
        *separator++ = 0;
        if (strcmp(line, platform_id) == 0 && strcmp(separator, url) == 0) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static bool load_archive_catalog(const PlatformInfo* platform, const char* metadata_url,
                                 const char* download_base_url, const char* selection_file,
                                 char filters[MAX_URLS_PER_KEY][MAX_URL_LEN], int filter_count) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    MemoryBuffer response = {0};
    char cache_path[MAX_PATH_LEN];
    catalog_cache_path(cache_path, sizeof(cache_path), selection_file, metadata_url);
    CURLcode result = CURLE_OK;
    if (!load_cached_response(cache_path, &response)) {
        curl_easy_setopt(curl, CURLOPT_URL, metadata_url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_response);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        result = curl_easy_perform(curl);
        if (result == CURLE_OK) save_cached_response(cache_path, &response);
    }
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        log_add(LOG_LEVEL_ERROR, "[CATALOG] Failed to load %s: %s", metadata_url, curl_easy_strerror(result));
        free(response.data);
        return false;
    }

    json_object* root = json_tokener_parse(response.data);
    free(response.data);
    if (!root) {
        fs_remove_file(cache_path);
        return false;
    }
    json_object *metadata = NULL, *identifier_value = NULL, *files = NULL;
    if (!json_object_object_get_ex(root, "metadata", &metadata) ||
        !json_object_object_get_ex(metadata, "identifier", &identifier_value) ||
        !json_object_object_get_ex(root, "files", &files) || !json_object_is_type(files, json_type_array)) {
        json_object_put(root);
        return false;
    }

    const char* identifier = json_object_get_string(identifier_value);
    int file_count = (int)json_object_array_length(files);
    for (int i = 0; i < file_count && g_game_count < ROM_CATALOG_MAX_GAMES; ++i) {
        json_object* item = json_object_array_get_idx(files, (size_t)i);
        json_object *name_value = NULL, *size_value = NULL, *private_value = NULL;
        if (!json_object_object_get_ex(item, "name", &name_value)) continue;
        if (json_object_object_get_ex(item, "private", &private_value)) {
            const char* private_text = json_object_get_string(private_value);
            if (json_object_get_boolean(private_value) ||
                (private_text && strcasecmp(private_text, "true") == 0)) continue;
        }
        const char* name = json_object_get_string(name_value);
        if (!name || !has_platform_extension(platform, name) ||
            !matches_any_filter(name, filters, filter_count)) continue;

        CURL* encoder = curl_easy_init();
        if (!encoder) continue;
        char* encoded_name = curl_easy_escape(encoder, name, 0);
        if (!encoded_name) { curl_easy_cleanup(encoder); continue; }

        RomCatalogGame* game = &g_games[g_game_count];
        memset(game, 0, sizeof(*game));
        snprintf(game->platform_id, sizeof(game->platform_id), "%.63s", platform->id);
        snprintf(game->name, sizeof(game->name), "%s", name);
        snprintf(game->url, sizeof(game->url), "%s/%s/%s", download_base_url, identifier, encoded_name);
        if (json_object_object_get_ex(item, "size", &size_value))
            game->size = json_object_get_int64(size_value);
        game->selected = was_selected(selection_file, game->platform_id, game->url);
        g_game_count++;
        curl_free(encoded_name);
        curl_easy_cleanup(encoder);
    }
    json_object_put(root);
    return true;
}

bool rom_catalog_refresh(const char* url_config_file, const char* selection_file) {
    g_game_count = 0;
    if (!url_config_load(url_config_file)) return false;
    const char* download_base_url = url_config_get_string("ARCHIVE_DOWNLOAD_BASE_URL", NULL);
    if (!download_base_url || !*download_base_url) {
        log_add(LOG_LEVEL_ERROR, "[CATALOG] archive_download_base_url is not configured");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < TOTAL_PLATFORMS; ++i) {
        if (!g_platforms[i].selected) continue;
        char key[128];
        snprintf(key, sizeof(key), "ROM_CATALOG_URLS_%.63s", g_platforms[i].id);
        char urls[MAX_URLS_PER_KEY][MAX_URL_LEN];
        int count = url_config_get_urls(key, urls, MAX_URLS_PER_KEY);
        char filter_key[128];
        snprintf(filter_key, sizeof(filter_key), "ROM_CATALOG_INCLUDES_%.63s", g_platforms[i].id);
        char filters[MAX_URLS_PER_KEY][MAX_URL_LEN];
        int filter_count = url_config_get_urls(filter_key, filters, MAX_URLS_PER_KEY);
        for (int u = 0; u < count; ++u)
            if (!load_archive_catalog(&g_platforms[i], urls[u], download_base_url, selection_file,
                                      filters, filter_count)) ok = false;
    }
    return ok;
}

bool rom_catalog_save_selection(const char* selection_file) {
    FILE* file = fopen(selection_file, "w");
    if (!file) return false;
    for (int i = 0; i < g_game_count; ++i)
        if (g_games[i].selected) fprintf(file, "%s|%s\n", g_games[i].platform_id, g_games[i].url);
    return fclose(file) == 0;
}

int rom_catalog_count(void) { return g_game_count; }
RomCatalogGame* rom_catalog_get(int index) { return index >= 0 && index < g_game_count ? &g_games[index] : NULL; }

int rom_catalog_count_for_selected_platforms(void) {
    int count = 0;
    for (int i = 0; i < g_game_count; ++i) {
        int platform_index = get_platform_index_by_id(g_games[i].platform_id);
        if (platform_index >= 0 && g_platforms[platform_index].selected) count++;
    }
    return count;
}
