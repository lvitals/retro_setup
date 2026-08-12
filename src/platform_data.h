#ifndef PLATFORM_DATA_H
#define PLATFORM_DATA_H

#include <stdbool.h>

#define MAX_CATEGORIES 32

typedef struct {
    char name[64];
    unsigned int color_hex;
} CategoryInfo;

extern CategoryInfo g_categories[MAX_CATEGORIES];
extern int g_category_count;

typedef struct {
    char id[64];
    char name[128];
    char category[64];
    char core_file[128];
    char core_name[128];
    char extensions[128];
    char bios_files[256];
    unsigned int color_hex;
    bool selected;
} PlatformInfo;

#define MAX_PLATFORMS 128

extern PlatformInfo g_platforms[MAX_PLATFORMS];
extern int g_platform_count;

#define TOTAL_PLATFORMS g_platform_count

void platform_data_init(void);
void platform_data_load_custom(const char* config_path);
void platform_data_build_categories(void);
int get_platform_index_by_id(const char* id);
void reset_all_selections(bool select_state);
void select_by_category(const char* category_name, bool select_state);

#endif // PLATFORM_DATA_H
