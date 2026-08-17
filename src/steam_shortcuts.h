#ifndef STEAM_SHORTCUTS_H
#define STEAM_SHORTCUTS_H

#include <stdbool.h>
#include <stddef.h>
#include "platform_data.h"

// Synchronizes Steam shortcuts.vdf across all detected Steam userdata directories.
// Adds/updates shortcuts for all installed ROMs of selected platforms,
// sets GameMode if available, sets boxart icons, removes stale ROM shortcuts,
// and preserves all existing user/non-RetroSetup shortcuts.
bool steam_shortcuts_sync(const char* ra_dir, const char* rom_base_dir);

// Returns true if gamemoderun is available on the system
bool steam_has_gamemode(void);

// Detects the best RetroArch executable to use
bool steam_find_retroarch_bin(char* out, size_t size, const char* ra_dir);

#endif // STEAM_SHORTCUTS_H
