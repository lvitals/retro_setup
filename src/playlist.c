#include "playlist.h"
#include "config.h"
#include "steam_shortcuts.h"
#include "fs.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <stdint.h>
#include <ctype.h>

typedef struct { char rom[128]; char title[512]; } ArcadeName;
typedef struct { ArcadeName* items; int count; } ArcadeNames;

static bool msgpack_string(const unsigned char* data, size_t size, size_t pos,
                           char* out, size_t out_size, size_t* next) {
    if (pos >= size) return false;
    unsigned marker = data[pos++]; size_t length = 0;
    if ((marker & 0xe0) == 0xa0) length = marker & 0x1f;
    else if (marker == 0xd9 && pos < size) length = data[pos++];
    else if (marker == 0xda && pos + 1 < size) { length = ((size_t)data[pos] << 8) | data[pos + 1]; pos += 2; }
    else if (marker == 0xdb && pos + 3 < size) { length = ((size_t)data[pos] << 24) | ((size_t)data[pos+1] << 16) | ((size_t)data[pos+2] << 8) | data[pos+3]; pos += 4; }
    else return false;
    if (pos + length > size || !length) return false;
    size_t copy = length < out_size - 1 ? length : out_size - 1;
    memcpy(out, data + pos, copy); out[copy] = 0;
    if (next) *next = pos + length;
    return true;
}

static void load_arcade_names(const char* ra_dir, const char* core_file, ArcadeNames* names) {
    memset(names, 0, sizeof(*names));
    char prefix[MAX_PATH_LEN]; snprintf(prefix, sizeof(prefix), "%s", core_file);
    char* cut = strchr(prefix, '_'); if (cut) *cut = 0;
    for (char* p = prefix; *p; ++p) *p = (char)tolower((unsigned char)*p);
    char rdb_dir[4096]; snprintf(rdb_dir, sizeof(rdb_dir), "%s/database/rdb", ra_dir);
    DIR* dir = opendir(rdb_dir); if (!dir) return;
    char rdb[4096] = {0}; struct dirent* entry;
    while ((entry = readdir(dir))) {
        char lower[512]; snprintf(lower, sizeof(lower), "%s", entry->d_name);
        for (char* p = lower; *p; ++p) *p = (char)tolower((unsigned char)*p);
        if (strstr(lower, prefix) && strstr(lower, ".rdb")) { fs_join_path(rdb, sizeof(rdb), rdb_dir, entry->d_name); break; }
    }
    closedir(dir); if (!rdb[0]) return;
    FILE* file = fopen(rdb, "rb"); if (!file) return;
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    if (length <= 0) { fclose(file); return; }
    unsigned char* data = malloc((size_t)length); if (!data) { fclose(file); return; }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) { free(data); fclose(file); return; }
    fclose(file);
    for (size_t i = 0; i + 10 < (size_t)length; ++i) {
        /* MessagePack fixstr keys: require the complete key marker so the
           trailing "name" inside "rom_name" is never treated as a field. */
        if (i == 0 || data[i - 1] != 0xa4 || memcmp(data + i, "name", 4)) continue;
        char title[512], rom[128]; size_t after_title;
        if (!msgpack_string(data, (size_t)length, i + 4, title, sizeof(title), &after_title)) continue;
        size_t limit = after_title + 512 < (size_t)length ? after_title + 512 : (size_t)length;
        for (size_t j = after_title; j + 8 < limit; ++j) {
            if (j == 0 || data[j - 1] != 0xa8 || memcmp(data + j, "rom_name", 8)) continue;
            if (!msgpack_string(data, (size_t)length, j + 8, rom, sizeof(rom), NULL)) break;
            size_t n = strlen(rom); if (n > 4 && !strcasecmp(rom + n - 4, ".zip")) rom[n - 4] = 0;
            ArcadeName* grown = realloc(names->items, (size_t)(names->count + 1) * sizeof(*names->items));
            if (grown) { names->items = grown; snprintf(grown[names->count].rom, sizeof(grown[names->count].rom), "%s", rom); snprintf(grown[names->count].title, sizeof(grown[names->count].title), "%s", title); names->count++; }
            break;
        }
    }
    free(data);
    if (names->count > 0)
        log_add(LOG_LEVEL_INFO, "Loaded %d arcade game names from %s", names->count, rdb);
}

static const char* arcade_title(const ArcadeNames* names, const char* shortname) {
    for (int i = 0; i < names->count; ++i) if (!strcasecmp(names->items[i].rom, shortname)) return names->items[i].title;
    return NULL;
}

static void json_escape(char* out, size_t out_size, const char* in) {
    if (!out || out_size == 0) return;
    if (!in) {
        out[0] = 0;
        return;
    }
    size_t pos = 0;
    for (const char* p = in; *p && pos < out_size - 2; p++) {
        if (*p == '\\' || *p == '"') {
            if (pos < out_size - 3) {
                out[pos++] = '\\';
                out[pos++] = *p;
            }
        } else if (*p == '\n') {
            out[pos++] = '\\';
            out[pos++] = 'n';
        } else {
            out[pos++] = *p;
        }
    }
    out[pos] = 0;
}

static bool has_valid_extension(const char* filename, const char* extensions) {
    if (!filename) return false;
    char lower_fn[512];
    snprintf(lower_fn, sizeof(lower_fn), "%s", filename);
    for (int i = 0; lower_fn[i]; i++) {
        if (lower_fn[i] >= 'A' && lower_fn[i] <= 'Z') lower_fn[i] += 32;
    }

    if (strstr(lower_fn, ".zip") || strstr(lower_fn, ".7z")) return true;

    if (!extensions || !*extensions) return true;

    char ext_copy[256];
    snprintf(ext_copy, sizeof(ext_copy), "%s", extensions);
    char* token = strtok(ext_copy, " \t,");
    while (token) {
        char dot_ext[64];
        snprintf(dot_ext, sizeof(dot_ext), ".%s", token);
        for (int i = 0; dot_ext[i]; i++) {
            if (dot_ext[i] >= 'A' && dot_ext[i] <= 'Z') dot_ext[i] += 32;
        }
        if (strstr(lower_fn, dot_ext)) return true;
        token = strtok(NULL, " \t,");
    }
    return false;
}

typedef struct {
    FILE* file;
    const PlatformInfo* platform;
    const char* playlist_name;
    const char* core_path;
    const char* core_name;
    bool first;
    int count;
    ArcadeNames arcade_names;
} PlaylistWriter;

static bool is_extracted_source_archive(const char* directory, const char* filename) {
    char marker[4096];
    snprintf(marker, sizeof(marker), "%.2000s/.extracted.%.1000s.ok", directory, filename);
    return fs_exists(marker);
}

static void playlist_add_directory(PlaylistWriter* writer, const char* directory) {
    DIR* d = opendir(directory);
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || entry->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%.3000s/%.1000s", directory, entry->d_name);
        if (fs_is_dir(path)) {
            playlist_add_directory(writer, path);
            continue;
        }
        if (!fs_is_file(path) || strstr(entry->d_name, ".download") || strstr(entry->d_name, ".tmp") ||
            strstr(entry->d_name, ".part") || is_extracted_source_archive(directory, entry->d_name) ||
            !has_valid_extension(entry->d_name, writer->platform->extensions)) continue;

        char label[512], esc_path[4096], esc_label[1024];
        fs_get_basename_without_ext(label, sizeof(label), entry->d_name);
        const char* mapped_title = arcade_title(&writer->arcade_names, label);
        if (mapped_title) snprintf(label, sizeof(label), "%s", mapped_title);
        json_escape(esc_path, sizeof(esc_path), path);
        json_escape(esc_label, sizeof(esc_label), label);
        if (!writer->first) fprintf(writer->file, "    ,\n");
        else writer->first = false;
        fprintf(writer->file, "    {\n");
        fprintf(writer->file, "      \"path\": \"%s\",\n", esc_path);
        fprintf(writer->file, "      \"label\": \"%s\",\n", esc_label);
        fprintf(writer->file, "      \"core_path\": \"%s\",\n", writer->core_path);
        fprintf(writer->file, "      \"core_name\": \"%s\",\n", writer->core_name);
        fprintf(writer->file, "      \"crc32\": \"DETECT\",\n");
        fprintf(writer->file, "      \"db_name\": \"%s.lpl\"\n", writer->playlist_name);
        fprintf(writer->file, "    }\n");
        writer->count++;
    }
    closedir(d);
}

bool playlist_generate_for_platform(const PlatformInfo* platform, const char* ra_dir, const char* rom_base_dir) {
    if (!platform || !ra_dir || !rom_base_dir) return false;

    char playlist_dir[4096];
    snprintf(playlist_dir, sizeof(playlist_dir), "%s/playlists", ra_dir);
    fs_mkdir_p(playlist_dir);

    char playlist_file[4096];
    snprintf(playlist_file, sizeof(playlist_file), "%.2000s/%.500s.lpl", playlist_dir, platform->name);

    if (!platform->core_file[0]) {
        if (fs_exists(playlist_file)) {
            fs_remove_file(playlist_file);
            log_add(LOG_LEVEL_WARN, "Removed incompatible playlist association: %s", playlist_file);
        }
        log_add(LOG_LEVEL_WARN,
                "Playlist not generated for %s: no compatible libretro core is configured; external emulator required.",
                platform->name);
        return false;
    }

    char rom_dir[4096];
    snprintf(rom_dir, sizeof(rom_dir), "%.2000s/%.200s", rom_base_dir, platform->id);

    if (!fs_is_dir(rom_dir)) {
        log_add(LOG_LEVEL_WARN, "ROM directory not found for platform %s: %s", platform->id, rom_dir);
        return false;
    }

    char core_file[128], core_name[128], core_path[MAX_PATH_LEN];
    platform_resolve_core(platform, ra_dir, core_file, sizeof(core_file),
                          core_name, sizeof(core_name), core_path, sizeof(core_path));

    DIR* d = opendir(rom_dir);
    if (!d) return false;
    closedir(d);

    log_add(LOG_LEVEL_INFO, "Generating playlist: %s.lpl", platform->name);

    FILE* f = fopen(playlist_file, "w");
    if (!f) return false;

    char esc_core_path[4096], esc_core_name[512], esc_playlist_name[512];
    json_escape(esc_core_path, sizeof(esc_core_path), core_path);
    json_escape(esc_core_name, sizeof(esc_core_name), core_name);
    json_escape(esc_playlist_name, sizeof(esc_playlist_name), platform->name);

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": \"1.5\",\n");
    fprintf(f, "  \"default_core_path\": \"%s\",\n", esc_core_path);
    fprintf(f, "  \"default_core_name\": \"%s\",\n", esc_core_name);
    fprintf(f, "  \"label_display_mode\": 0,\n");
    fprintf(f, "  \"right_thumbnail_mode\": 0,\n");
    fprintf(f, "  \"left_thumbnail_mode\": 0,\n");
    fprintf(f, "  \"sort_mode\": 0,\n");
    fprintf(f, "  \"items\": [\n");

    PlaylistWriter writer = {f, platform, esc_playlist_name, esc_core_path, esc_core_name, true, 0, {0}};
    load_arcade_names(ra_dir, core_file, &writer.arcade_names);
    playlist_add_directory(&writer, rom_dir);
    free(writer.arcade_names.items);

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);

    log_add(LOG_LEVEL_INFO, "Created %s.lpl with %d items", platform->name, writer.count);
    return true;
}

int playlist_generate_selected(const char* ra_dir, const char* rom_base_dir) {
    int total = 0;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_platforms[i].selected) {
            if (playlist_generate_for_platform(&g_platforms[i], ra_dir, rom_base_dir)) {
                total++;
            }
        }
    }

    // In Steam mode, also synchronize non-Steam game shortcuts in Steam's library
    if (g_config.mode == MODE_STEAM) {
        steam_shortcuts_sync(ra_dir, rom_base_dir);
    }

    return total;
}
