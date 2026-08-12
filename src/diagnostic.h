#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include "config.h"
#include "platform_data.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RETRO_SETUP_VERSION "2.0.0"

typedef enum {
    PLATFORM_STATUS_READY = 0,
    PLATFORM_STATUS_INCOMPLETE,
    PLATFORM_STATUS_ERROR
} PlatformStatusState;

typedef enum {
    HEALTH_STATUS_HEALTHY = 0,
    HEALTH_STATUS_WARNING,
    HEALTH_STATUS_ERROR
} HealthStatusState;

typedef struct {
    const PlatformInfo* platform;
    bool core_installed;
    bool core_info_found;
    int bios_required_count;
    int bios_found_count;
    int bios_missing_count;
    char bios_missing_names[256];
    int rom_count;
    uint64_t rom_size;
    bool playlist_found;
    int thumbnail_count;
    PlatformStatusState status;
} PlatformDiagnostic;

typedef struct {
    char os_distro[256];
    char os_arch[64];
    char home_dir[MAX_PATH_LEN];

    char app_version[32];
    char config_dir[MAX_PATH_LEN];
    char rom_base_dir[MAX_PATH_LEN];

    bool retroarch_binary_found;
    char retroarch_binary_path[MAX_PATH_LEN];
    bool retroarch_config_found;
    char retroarch_target_dir[MAX_PATH_LEN];

    bool dir_ra_readable;
    bool dir_ra_writable;
    bool dir_rom_writable;

    int cores_installed_count;
    int core_info_count;
    int bios_found_total;
    int bios_missing_total;
    int rom_files_total;
    uint64_t rom_size_total;
    int playlists_found_total;
    int thumbnails_count_total;
    uint64_t thumbnails_size_total;

    uint64_t disk_free_bytes;
    uint64_t disk_total_bytes;

    int platforms_selected;
    int platforms_ready;
    int platforms_incomplete;
    int platforms_error;

    PlatformDiagnostic platform_diags[MAX_PLATFORMS];
    int platform_diag_count;

    HealthStatusState overall_health;
    int health_warning_count;
    int health_error_count;
} SystemDiagnosticReport;

void diagnostic_run_scan(SystemDiagnosticReport* report);
void diagnostic_format_size(uint64_t bytes, char* out, size_t out_size);
void diagnostic_format_path_short(const char* full_path, char* out, size_t out_size);

#endif // DIAGNOSTIC_H
