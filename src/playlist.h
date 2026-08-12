#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "platform_data.h"
#include <stdbool.h>

bool playlist_generate_for_platform(const PlatformInfo* platform, const char* ra_dir, const char* rom_base_dir);
int playlist_generate_selected(const char* ra_dir, const char* rom_base_dir);

#endif // PLAYLIST_H
