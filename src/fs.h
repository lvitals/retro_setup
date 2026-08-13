#ifndef FS_H
#define FS_H

#include <stdbool.h>
#include <stddef.h>

bool fs_exists(const char* path);
bool fs_is_dir(const char* path);
bool fs_is_file(const char* path);
long long fs_file_size(const char* path);

bool fs_mkdir_p(const char* path);
bool fs_copy_file(const char* src_path, const char* dst_path);
bool fs_remove_file(const char* path);

// Safe recursive directory removal (only within allowed_root)
bool fs_remove_dir_recursive(const char* path, const char* allowed_root);

// Extract ZIP, TAR, 7Z archive using libarchive
bool fs_extract_archive(const char* archive_path, const char* dest_dir);
bool fs_validate_archive(const char* archive_path, char* error, size_t error_size);

void fs_join_path(char* out, size_t out_size, const char* p1, const char* p2);
void fs_get_filename(char* out, size_t out_size, const char* path);
void fs_get_basename_without_ext(char* out, size_t out_size, const char* filename);

#endif // FS_H
