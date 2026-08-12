#ifndef PLATFORM_DATA_H
#define PLATFORM_DATA_H

#include <stdbool.h>

typedef enum {
    MFR_ALL = 0,
    MFR_NINTENDO,
    MFR_SONY,
    MFR_SEGA,
    MFR_SNK,
    MFR_ARCADE,
    MFR_ATARI,
    MFR_OTHER,
    MFR_COUNT
} ManufacturerCategory;

typedef struct {
    const char* id;
    const char* name;
    ManufacturerCategory mfr;
    const char* core_file;
    const char* core_name;
    const char* extensions;
    const char* bios_files;
    unsigned int color_hex; // Accent color for UI badge (e.g. 0xE60012 for Nintendo Red)
    bool selected;
} PlatformInfo;

#define TOTAL_PLATFORMS 40

extern PlatformInfo g_platforms[TOTAL_PLATFORMS];

const char* get_mfr_name(ManufacturerCategory mfr);
unsigned int get_mfr_color(ManufacturerCategory mfr);
int get_platform_index_by_id(const char* id);
void reset_all_selections(bool select_state);
void select_by_manufacturer(ManufacturerCategory mfr, bool select_state);

#endif // PLATFORM_DATA_H
