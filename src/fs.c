#include "fs.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>

bool fs_exists(const char* path) {
    if (!path || !*path) return false;
    struct stat st;
    return (stat(path, &st) == 0);
}

bool fs_is_dir(const char* path) {
    if (!path || !*path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool fs_is_file(const char* path) {
    if (!path || !*path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

long long fs_file_size(const char* path) {
    if (!path || !*path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

bool fs_mkdir_p(const char* path) {
    if (!path || !*path) return false;
    if (fs_is_dir(path)) return true;

    char temp[4096];
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);

    if (len > 0 && temp[len - 1] == '/') {
        temp[len - 1] = 0;
    }

    for (char* p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!fs_exists(temp)) {
                if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            *p = '/';
        }
    }

    if (!fs_exists(temp)) {
        if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

bool fs_copy_file(const char* src_path, const char* dst_path) {
    if (!src_path || !dst_path) return false;

    FILE* in = fopen(src_path, "rb");
    if (!in) return false;

    // Ensure parent dir of dst exists
    char dst_copy[4096];
    snprintf(dst_copy, sizeof(dst_copy), "%s", dst_path);
    char* slash = strrchr(dst_copy, '/');
    if (slash) {
        *slash = 0;
        fs_mkdir_p(dst_copy);
    }

    FILE* out = fopen(dst_path, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buf[65536];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, bytes, out) != bytes) {
            fclose(in);
            fclose(out);
            return false;
        }
    }

    fclose(in);
    fclose(out);
    return true;
}

bool fs_remove_file(const char* path) {
    if (!path || !*path) return false;
    if (!fs_exists(path)) return true;
    return (unlink(path) == 0);
}

// Strict root check safety guard before recursive directory deletion
static bool is_safe_delete_path(const char* path, const char* allowed_root) {
    if (!path || !allowed_root || !*path || !*allowed_root) return false;

    // Refuse forbidden system paths
    if (strcmp(path, "/") == 0 || strcmp(path, "/usr") == 0 || strcmp(path, "/etc") == 0 ||
        strcmp(path, "/home") == 0 || strcmp(path, "/var") == 0 || strcmp(path, "/opt") == 0) {
        return false;
    }

    const char* home = getenv("HOME");
    if (home && strcmp(path, home) == 0) return false;

    // Check if path is strictly inside allowed_root
    size_t root_len = strlen(allowed_root);
    if (strncmp(path, allowed_root, root_len) != 0) return false;

    return true;
}

bool fs_remove_dir_recursive(const char* path, const char* allowed_root) {
    if (!path || !*path || !fs_exists(path)) return true;

    if (!is_safe_delete_path(path, allowed_root)) {
        log_add(LOG_LEVEL_ERROR, "SAFETY VIOLATION: Refusing to delete directory '%s' outside '%s'", path, allowed_root);
        return false;
    }

    DIR* d = opendir(path);
    if (!d) return false;

    struct dirent* dir;
    bool ok = true;

    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        char sub_path[4096];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", path, dir->d_name);

        if (fs_is_dir(sub_path)) {
            if (!fs_remove_dir_recursive(sub_path, allowed_root)) ok = false;
        } else {
            if (unlink(sub_path) != 0) ok = false;
        }
    }
    closedir(d);

    if (rmdir(path) != 0) ok = false;
    return ok;
}

bool fs_extract_archive(const char* archive_path, const char* dest_dir) {
    if (!archive_path || !dest_dir) return false;
    if (!fs_exists(archive_path)) return false;

    fs_mkdir_p(dest_dir);

    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    struct archive_entry* entry;

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS;
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    int r = archive_read_open_filename(a, archive_path, 10240);
    if (r != ARCHIVE_OK) {
        log_add(LOG_LEVEL_ERROR, "Failed to open archive %s: %s", archive_path, archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    bool success = true;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* current_file = archive_entry_pathname(entry);
        char full_dest[4096];
        snprintf(full_dest, sizeof(full_dest), "%s/%s", dest_dir, current_file);

        archive_entry_set_pathname(entry, full_dest);

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK) {
            log_add(LOG_LEVEL_WARN, "Archive write header warning: %s", archive_error_string(ext));
        } else {
            if (archive_entry_size(entry) > 0) {
                const void* buff;
                size_t size;
                int64_t offset;
                while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK) {
                    if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                        log_add(LOG_LEVEL_ERROR, "Archive write data error: %s", archive_error_string(ext));
                        success = false;
                        break;
                    }
                }
                if (r != ARCHIVE_EOF && r != ARCHIVE_OK) {
                    success = false;
                    break;
                }
            }
        }
        archive_write_finish_entry(ext);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return success;
}

bool fs_validate_archive(const char* archive_path, char* error, size_t error_size) {
    if (!archive_path || !fs_is_file(archive_path) || fs_file_size(archive_path) <= 0) {
        if (error) snprintf(error, error_size, "Archive is missing or empty");
        return false;
    }
    struct archive* archive = archive_read_new();
    archive_read_support_format_all(archive);
    archive_read_support_filter_all(archive);
    if (archive_read_open_filename(archive, archive_path, 10240) != ARCHIVE_OK) {
        if (error) snprintf(error, error_size, "%s", archive_error_string(archive));
        archive_read_free(archive);
        return false;
    }
    struct archive_entry* entry = NULL;
    int rc;
    int entries = 0;
    char buffer[65536];
    while ((rc = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
        entries++;
        la_ssize_t bytes;
        while ((bytes = archive_read_data(archive, buffer, sizeof(buffer))) > 0) {}
        if (bytes < 0) { rc = ARCHIVE_FATAL; break; }
    }
    if (rc != ARCHIVE_EOF || entries == 0) {
        if (error) snprintf(error, error_size, "%s", archive_error_string(archive) ? archive_error_string(archive) : "Archive has no entries");
        archive_read_close(archive);
        archive_read_free(archive);
        return false;
    }
    archive_read_close(archive);
    archive_read_free(archive);
    return true;
}

void fs_join_path(char* out, size_t out_size, const char* p1, const char* p2) {
    if (!out || out_size == 0) return;
    if (!p1) p1 = "";
    if (!p2) p2 = "";

    size_t len1 = strlen(p1);
    if (len1 > 0 && p1[len1 - 1] == '/') {
        snprintf(out, out_size, "%.1000s%.1000s", p1, (p2[0] == '/') ? p2 + 1 : p2);
    } else {
        snprintf(out, out_size, "%.1000s/%.1000s", p1, (p2[0] == '/') ? p2 + 1 : p2);
    }
}

void fs_get_filename(char* out, size_t out_size, const char* path) {
    if (!out || out_size == 0) return;
    if (!path) {
        out[0] = 0;
        return;
    }
    const char* slash = strrchr(path, '/');
    if (slash) {
        snprintf(out, out_size, "%s", slash + 1);
    } else {
        snprintf(out, out_size, "%s", path);
    }
}

void fs_get_basename_without_ext(char* out, size_t out_size, const char* filename) {
    if (!out || out_size == 0) return;
    if (!filename) {
        out[0] = 0;
        return;
    }
    char temp[1024];
    fs_get_filename(temp, sizeof(temp), filename);
    char* dot = strrchr(temp, '.');
    if (dot) *dot = 0;
    snprintf(out, out_size, "%s", temp);
}
