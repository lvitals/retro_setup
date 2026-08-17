#ifndef ROM_CATALOG_H
#define ROM_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#define ROM_CATALOG_MAX_GAMES 8192

typedef struct {
    char platform_id[64];
    char name[512];
    char url[2048];
    long long size;
    bool selected;
} RomCatalogGame;

bool rom_catalog_refresh(const char* url_config_file, const char* selection_file);
bool rom_catalog_save_selection(const char* selection_file);
int rom_catalog_count(void);
RomCatalogGame* rom_catalog_get(int index);
int rom_catalog_count_for_selected_platforms(void);

#endif
