#include "playlist.h"
#include "fs.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

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

    char rom_dir[4096];
    snprintf(rom_dir, sizeof(rom_dir), "%.2000s/%.200s", rom_base_dir, platform->id);

    if (!fs_is_dir(rom_dir)) {
        log_add(LOG_LEVEL_WARN, "ROM directory not found for platform %s: %s", platform->id, rom_dir);
        return false;
    }

    char core_file[256], core_name[256];
    snprintf(core_file, sizeof(core_file), "%s", platform->core_file);
    snprintf(core_name, sizeof(core_name), "%s", platform->core_name);

    char core_path[4096];
    snprintf(core_path, sizeof(core_path), "%.2000s/cores/%.250s", ra_dir, core_file);

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

    PlaylistWriter writer = {f, platform, esc_playlist_name, esc_core_path, esc_core_name, true, 0};
    playlist_add_directory(&writer, rom_dir);

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
    return total;
}
