#include "src/rom_catalog.h"
#include "src/platform_data.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    platform_data_init();
    platform_data_load_custom(argv[1]);
    int platform = get_platform_index_by_id(argv[4]);
    if (platform < 0) return 3;
    g_platforms[platform].selected = true;
    if (!rom_catalog_refresh(argv[2], argv[3])) return 4;
    int count = rom_catalog_count();
    if (count < 1) return 5;
    for (int i = 0; i < count; ++i) {
        RomCatalogGame* game = rom_catalog_get(i);
        if (!game || !game->name[0] || !game->url[0] || game->size <= 0) return 6;
    }
    rom_catalog_get(0)->selected = true;
    if (!rom_catalog_save_selection(argv[3])) return 7;
    printf("Loaded %d individually selectable %s games\n", count, argv[4]);
    return 0;
}
