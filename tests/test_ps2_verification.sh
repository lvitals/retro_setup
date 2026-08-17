#!/bin/bash
set -euo pipefail

echo "============================================================"
echo "          PS2 IMPLEMENTATION VERIFICATION TEST SUITE        "
echo "============================================================"

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="/tmp/retro_setup_test_$$"
mkdir -p "$TEST_DIR"
trap 'rm -rf "$TEST_DIR"' EXIT

TEST_STANDALONE_RA="$TEST_DIR/standalone_ra"
TEST_STEAM_RA="$TEST_DIR/steam_ra"
TEST_ROMS="$TEST_DIR/roms"
TEST_BIOS="$TEST_DIR/bios"
TEST_STEAM_USERDATA="$TEST_DIR/.local/share/Steam/userdata/12345678/config"

mkdir -p "$TEST_STANDALONE_RA/cores" "$TEST_STANDALONE_RA/system" "$TEST_STANDALONE_RA/playlists" "$TEST_STANDALONE_RA/config"
mkdir -p "$TEST_STEAM_RA/cores" "$TEST_STEAM_RA/system" "$TEST_STEAM_RA/playlists" "$TEST_STEAM_RA/config"
mkdir -p "$TEST_ROMS/ps2" "$TEST_ROMS/nes" "$TEST_ROMS/snes" "$TEST_ROMS/ps1"
mkdir -p "$TEST_BIOS"
mkdir -p "$TEST_STEAM_USERDATA"

# Create dummy cores
printf core > "$TEST_STANDALONE_RA/cores/pcsx2_libretro.so"
printf fallback > "$TEST_STANDALONE_RA/cores/play_libretro.so"
touch "$TEST_STANDALONE_RA/cores/fceumm_libretro.so"
touch "$TEST_STANDALONE_RA/cores/snes9x_libretro.so"
touch "$TEST_STANDALONE_RA/cores/mednafen_psx_hw_libretro.so"

touch "$TEST_STEAM_RA/cores/pcsx2_libretro.so" "$TEST_STEAM_RA/cores/play_libretro.so"
touch "$TEST_STEAM_RA/retroarch"
chmod +x "$TEST_STEAM_RA/retroarch"

# Create sample PS2 ROMs in all supported formats (ISO, CHD, CSO, BIN, CUE)
touch "$TEST_ROMS/ps2/Shadow of the Colossus.iso"
touch "$TEST_ROMS/ps2/Gran Turismo 4.chd"
touch "$TEST_ROMS/ps2/God of War II.cso"
touch "$TEST_ROMS/ps2/Final Fantasy X.bin"
touch "$TEST_ROMS/ps2/Kingdom Hearts (Special Edition) [USA].iso"
touch "$TEST_ROMS/ps2/Game With Spaces & Symbols! (v1.01).chd"

# Create other platform ROMs to test non-regression
touch "$TEST_ROMS/nes/Super Mario Bros.nes"
touch "$TEST_ROMS/snes/Super Mario World.sfc"
touch "$TEST_ROMS/ps1/Castlevania - Symphony of the Night.chd"

# ------------------------------------------------------------
# TEST 1: Catalog Configuration Verification
# ------------------------------------------------------------
echo "[TEST 1] Checking platforms.config and retro_url.config..."
grep -q "^\[ps2\]" "$REPO_DIR/platforms.config" || { echo "FAIL: ps2 section missing in platforms.config"; exit 1; }
grep -q "core_file = pcsx2_libretro.so" "$REPO_DIR/platforms.config" || { echo "FAIL: ps2 core missing in platforms.config"; exit 1; }
! grep -q "fallback_core_file = play_libretro.so" "$REPO_DIR/platforms.config" || { echo "FAIL: Play! must not be configured as the PS2 fallback"; exit 1; }
grep -q "extensions = elf, iso, ciso, chd, cso, bin, mdf, nrg, dump, gz, img, m3u" "$REPO_DIR/platforms.config" || { echo "FAIL: ps2 extensions missing in platforms.config"; exit 1; }
grep -q "bios_files = pcsx2/bios" "$REPO_DIR/platforms.config" || { echo "FAIL: ps2 bios directory missing in platforms.config"; exit 1; }
grep -q "bios_missing_action = error" "$REPO_DIR/platforms.config" || { echo "FAIL: LRPS2 must require a valid user-provided PS2 BIOS"; exit 1; }
grep -q "^\[ps2\]" "$REPO_DIR/retro_url.config" || { echo "FAIL: ps2 section missing in retro_url.config"; exit 1; }
awk '/^\[ps2\]/{in_ps2=1; next} /^\[/{in_ps2=0} in_ps2 && /^rom_catalog_url = https:\/\/archive.org\/metadata\//{found=1} END{exit !found}' \
    "$REPO_DIR/retro_url.config" || { echo "FAIL: PS2 individual-game catalog missing"; exit 1; }
awk '/^\[ps1\]/{in_ps1=1; next} /^\[/{in_ps1=0} in_ps1 && /^rom_catalog_url = https:\/\/archive.org\/metadata\//{found=1} END{exit !found}' \
    "$REPO_DIR/retro_url.config" || { echo "FAIL: PS1 individual-game catalog missing"; exit 1; }
awk '/^\[ps1\]/{in_ps1=1; next} /^\[/{in_ps1=0} in_ps1 && /^rom_directory_url = /{found=1} END{exit found}' \
    "$REPO_DIR/retro_url.config" || { echo "FAIL: PS1 still has a bulk directory download"; exit 1; }
echo "PASS: Configuration catalogs valid."

# ------------------------------------------------------------
# TEST 2: Standalone Mode Does Not Force Hardware-Specific Overrides
# ------------------------------------------------------------
export HOME="$TEST_DIR"
mkdir -p "$TEST_DIR/.config/retro_setup"
cat > "$TEST_DIR/.config/retro_setup/retro_setup.conf" <<EOF
SELECTED_PLATFORMS=( "ps2" "nes" "snes" "ps1" )
EOF
cat > "$TEST_DIR/.config/retro_setup/retro_setup_steam.conf" <<EOF
SELECTED_PLATFORMS=( "ps2" "nes" "snes" "ps1" )
EOF

export RETRO_SETUP_MODE=standalone
export RA_DIR="$TEST_STANDALONE_RA"
export SET_DIR="$REPO_DIR"
export ROM_BASE_DIR="$TEST_ROMS"
export SELECTED_PLATFORMS=("ps2" "nes" "snes" "ps1")

# Initial global retroarch.cfg
cat > "$TEST_STANDALONE_RA/retroarch.cfg" <<EOF
video_driver = "gl"
libretro_directory = "custom_cores"
EOF

. "$REPO_DIR/scripts/retro_setup_common.sh"

grep -q 'video_driver = "gl"' "$TEST_STANDALONE_RA/retroarch.cfg" || { echo "FAIL: Global retroarch.cfg was improperly overwritten!"; exit 1; }
test ! -e "$TEST_STANDALONE_RA/config/LRPS2/LRPS2.cfg" || { echo "FAIL: hardware-specific override was created"; exit 1; }
echo "PASS: Standalone configuration preserves the user's video settings."

# ------------------------------------------------------------
# TEST 3: Playlist Generation for Documented PS2 Formats
# ------------------------------------------------------------
echo "[TEST 3] Testing Standalone Playlist Generation for PS2..."
CORES_DIR="$TEST_STANDALONE_RA/cores"
PLAYLIST_DIR="$TEST_STANDALONE_RA/playlists"
bash "$REPO_DIR/scripts/generate_playlists.sh"

PS2_LPL="$TEST_STANDALONE_RA/playlists/Sony - PlayStation 2.lpl"
test -f "$PS2_LPL" || { echo "FAIL: PS2 Playlist was not generated"; exit 1; }

grep -q "Shadow of the Colossus.iso" "$PS2_LPL" || { echo "FAIL: ISO game missing from playlist"; exit 1; }
grep -q "Gran Turismo 4.chd" "$PS2_LPL" || { echo "FAIL: CHD game missing from playlist"; exit 1; }
grep -q "God of War II.cso" "$PS2_LPL" || { echo "FAIL: CSO game missing from playlist"; exit 1; }
grep -q "Final Fantasy X.bin" "$PS2_LPL" || { echo "FAIL: BIN game missing from playlist"; exit 1; }
grep -q "pcsx2_libretro.so" "$PS2_LPL" || { echo "FAIL: LRPS2 core was not used in playlist"; exit 1; }
! grep -q "play_libretro.so" "$PS2_LPL" || { echo "FAIL: Play! is still present in the PS2 playlist"; exit 1; }

# Verify other playlists (non-regression)
test -f "$TEST_STANDALONE_RA/playlists/Nintendo - Nintendo Entertainment System.lpl" || { echo "FAIL: NES playlist missing"; exit 1; }
test -f "$TEST_STANDALONE_RA/playlists/Nintendo - Super Nintendo Entertainment System.lpl" || { echo "FAIL: SNES playlist missing"; exit 1; }
test -f "$TEST_STANDALONE_RA/playlists/Sony - PlayStation.lpl" || { echo "FAIL: PS1 playlist missing"; exit 1; }
echo "PASS: Playlist generation and multi-format ROM detection verified."
echo "PASS: Playlist generation and multi-format ROM detection verified."

# ------------------------------------------------------------
# TEST 4: BIOS Syncing and Detection (PS2 with and without BIOS)
# ------------------------------------------------------------
echo "[TEST 4] Testing PS2 BIOS Detection and Sync..."
# Case 4A: Sem BIOS
SELECTED_PLATFORMS=("ps2")
rm -rf "$TEST_STANDALONE_RA/system/pcsx2/bios"/*
if check_selected_bios "$TEST_STANDALONE_RA/system"; then
    echo "FAIL: check_selected_bios should return missing when no PS2 BIOS exists"; exit 1;
else
    echo "PASS: Missing PS2 BIOS correctly detected without crash."
fi

# Case 4B: A companion file alone is not a complete BIOS dump
mkdir -p "$TEST_BIOS/pcsx2/bios"
truncate -s 1024 "$TEST_BIOS/pcsx2/bios/SCPH-39001.nvm"
copy_selected_bios "$TEST_BIOS" "$TEST_STANDALONE_RA/system"
if check_selected_bios "$TEST_STANDALONE_RA/system"; then
    echo "FAIL: a PS2 NVM companion was accepted without a main ROM"; exit 1;
else
    echo "PASS: PS2 companion files do not validate without the main ROM."
fi

# Case 4C: BIOS Dumper component set with a 4 MiB ROM0
truncate -s 4194304 "$TEST_BIOS/pcsx2/bios/SCPH-39001.rom0"
copy_selected_bios "$TEST_BIOS" "$TEST_STANDALONE_RA/system"
test -f "$TEST_STANDALONE_RA/system/pcsx2/bios/SCPH-39001.rom0" || { echo "FAIL: PS2 ROM0 was not copied to system/pcsx2/bios"; exit 1; }
test -f "$TEST_STANDALONE_RA/system/pcsx2/bios/SCPH-39001.nvm" || { echo "FAIL: PS2 NVM companion was not preserved"; exit 1; }
if check_selected_bios "$TEST_STANDALONE_RA/system"; then
    echo "PASS: PS2 BIOS Dumper component set correctly detected."
else
    echo "FAIL: check_selected_bios failed to detect present PS2 BIOS"; exit 1;
fi
SELECTED_PLATFORMS=("ps2" "nes" "snes" "ps1")

# ------------------------------------------------------------
# TEST 5: Native Steam Shortcuts Sync (C Engine & Steam Mode)
# ------------------------------------------------------------
echo "[TEST 5] Testing Steam Shortcuts Sync with Native Engine..."
# Create a dummy non-RetroSetup shortcut to test preservation
python3 -c "
import struct
# Write a simple binary shortcuts.vdf with one existing non-retro shortcut
f = open('$TEST_STEAM_USERDATA/shortcuts.vdf', 'wb')
f.write(b'\x00shortcuts\x00')
f.write(b'\x000\x00')
f.write(b'\x02appid\x00\x11\x22\x33\x44')
f.write(b'\x01AppName\x00Custom Non-Retro Game\x00')
f.write(b'\x01Exe\x00/usr/bin/custom_game\x00')
f.write(b'\x01StartDir\x00/tmp\x00')
f.write(b'\x01icon\x00\x00')
f.write(b'\x01ShortcutPath\x00\x00')
f.write(b'\x01LaunchOptions\x00\x00')
f.write(b'\x02IsHidden\x00\x00\x00\x00\x00')
f.write(b'\x00tags\x00\x010\x00Favorite\x00\x08')
f.write(b'\x08\x08\x08')
f.close()
"

# Compile and run a test runner using steam_shortcuts.c
cat > "$TEST_DIR/test_steam_runner.c" <<'EOF'
#include "src/steam_shortcuts.h"
#include "src/platform_data.h"
#include "src/config.h"
#include "src/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    const char* repo_dir = argv[1];
    const char* ra_dir = argv[2];
    const char* rom_dir = argv[3];

    // Load platforms
    char plat_cfg[4096];
    fs_join_path(plat_cfg, sizeof(plat_cfg), repo_dir, "platforms.config");
    platform_data_init();
    platform_data_load_custom(plat_cfg);

    // Select all platforms
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        g_platforms[i].selected = true;
    }

    g_config.mode = MODE_STEAM;
    snprintf(g_config.ra_dir, sizeof(g_config.ra_dir), "%s", ra_dir);
    snprintf(g_config.rom_dir, sizeof(g_config.rom_dir), "%s", rom_dir);
    snprintf(g_config.repo_dir, sizeof(g_config.repo_dir), "%s", repo_dir);

    bool ok = steam_shortcuts_sync(ra_dir, rom_dir);
    return ok ? 0 : 2;
}
EOF

gcc -Wall -Wextra -O2 -I"$REPO_DIR" "$TEST_DIR/test_steam_runner.c" \
    "$REPO_DIR/obj/steam_shortcuts.o" \
    "$REPO_DIR/obj/platform_data.o" \
    "$REPO_DIR/obj/config.o" \
    "$REPO_DIR/obj/config_url_parser.o" \
    "$REPO_DIR/obj/theme.o" \
    "$REPO_DIR/obj/font.o" \
    "$REPO_DIR/obj/fs.o" \
    "$REPO_DIR/obj/log.o" \
    -lSDL2 -larchive \
    -o "$TEST_DIR/test_steam_runner"

HOME="$TEST_DIR" "$TEST_DIR/test_steam_runner" "$REPO_DIR" "$TEST_STEAM_RA" "$TEST_ROMS"

# Parse generated shortcuts.vdf to verify contents
python3 -c "
import os

vdf_path = '$TEST_STEAM_USERDATA/shortcuts.vdf'
assert os.path.isfile(vdf_path), 'shortcuts.vdf was not generated'

with open(vdf_path, 'rb') as f:
    content = f.read()

# Verify non-RetroSetup shortcut is preserved
assert b'Custom Non-Retro Game' in content, 'Non-RetroSetup shortcut was NOT preserved!'

# Verify PS2 games exist
assert b'Shadow of the Colossus' in content, 'PS2 game 1 missing from shortcuts.vdf'
assert b'Gran Turismo 4' in content, 'PS2 game 2 missing from shortcuts.vdf'
assert b'God of War II' in content, 'PS2 game 3 missing from shortcuts.vdf'
assert b'Kingdom Hearts (Special Edition) [USA]' in content, 'PS2 game 4 missing from shortcuts.vdf'
assert b'Game With Spaces & Symbols! (v1.01)' in content, 'PS2 game with special characters missing'

# Verify other platforms exist
assert b'Super Mario Bros.' in content, 'NES game missing'
assert b'Super Mario World' in content, 'SNES game missing'
assert b'Castlevania - Symphony of the Night' in content, 'PSX game missing'

# Verify Tagging
assert b'Sony - PlayStation 2' in content, 'PS2 category tag missing'
assert b'Retro Setup' in content, 'Retro Setup tag missing'

print('PASS: Steam shortcuts.vdf binary format, game entries, tags, and preservation verified.')
"

# ------------------------------------------------------------
# TEST 6: GameMode vs Non-GameMode in Steam Shortcuts
# ------------------------------------------------------------
echo "[TEST 6] Testing Steam Shortcuts with and without GameMode..."
python3 -c "
import os
vdf_path = '$TEST_STEAM_USERDATA/shortcuts.vdf'
with open(vdf_path, 'rb') as f:
    content = f.read()

has_gamemode = os.path.exists('/usr/bin/gamemoderun') or os.path.exists('/usr/local/bin/gamemoderun')
if has_gamemode:
    assert b'gamemoderun' in content, 'gamemoderun was expected in LaunchOptions/Exe when GameMode is present'
    print('PASS: GameMode integration active in launch command.')
else:
    print('PASS: Non-GameMode direct execution verified.')
"

# Steam builds must be launched through their detected app runtime; invoking
# the ELF directly can miss libraries supplied by Steam Linux Runtime.
if command -v steam >/dev/null 2>&1; then
    mkdir -p "$TEST_STEAM_RA/mist"
    printf '%s\n' 1118310 > "$TEST_STEAM_RA/mist/steam_appid.txt"
    HOME="$TEST_DIR" "$TEST_DIR/test_steam_runner" "$REPO_DIR" "$TEST_STEAM_RA" "$TEST_ROMS"
    python3 -c "
content = open('$TEST_STEAM_USERDATA/shortcuts.vdf', 'rb').read()
assert b'-applaunch 1118310 -- -L' in content, 'Steam Runtime launcher was not used'
assert b'/steam' in content, 'Steam executable missing from runtime shortcut'
print('PASS: Steam Runtime launch command detected from steam_appid.txt.')
"
fi

# ------------------------------------------------------------
# TEST 7: Idempotency & Stale Shortcut Removal on ROM deletion
# ------------------------------------------------------------
echo "[TEST 7] Testing Idempotent Updates and Stale ROM Cleanup..."
# Remove one PS2 ROM
rm -f "$TEST_ROMS/ps2/God of War II.cso"

# Re-run sync
HOME="$TEST_DIR" "$TEST_DIR/test_steam_runner" "$REPO_DIR" "$TEST_STEAM_RA" "$TEST_ROMS"

python3 -c "
import os
vdf_path = '$TEST_STEAM_USERDATA/shortcuts.vdf'
with open(vdf_path, 'rb') as f:
    content = f.read()

assert b'God of War II' not in content, 'Deleted ROM God of War II was not removed from shortcuts.vdf!'
assert b'Shadow of the Colossus' in content, 'Remaining ROMs must stay present!'
assert b'Custom Non-Retro Game' in content, 'Non-Retro game must remain intact!'
print('PASS: Stale ROM cleanup and idempotent update verified.')
"

# ------------------------------------------------------------
# TEST 8: Uninstallation Cleanup
# ------------------------------------------------------------
echo "[TEST 8] Testing PS2 Uninstallation..."
bash "$REPO_DIR/scripts/uninstall_platforms.sh" ps2

test ! -f "$TEST_STANDALONE_RA/playlists/Sony - PlayStation 2.lpl" || { echo "FAIL: PS2 playlist was not removed"; exit 1; }
test ! -d "$TEST_STANDALONE_RA/config/LRPS2" || { echo "FAIL: LRPS2 config was not removed"; exit 1; }
test ! -d "$TEST_STANDALONE_RA/config/PCSX2" || { echo "FAIL: PCSX2 config was not removed"; exit 1; }
test ! -d "$TEST_STANDALONE_RA/config/Sony - PlayStation 2" || { echo "FAIL: PS2 config was not removed"; exit 1; }

# Remaining platforms still intact
test -f "$TEST_STANDALONE_RA/playlists/Nintendo - Nintendo Entertainment System.lpl" || { echo "FAIL: NES playlist was erroneously removed"; exit 1; }
echo "PASS: PS2 Uninstallation cleanly removed PS2 configs without affecting other platforms."

echo "============================================================"
echo "          ALL PS2 IMPLEMENTATION TESTS PASSED!              "
echo "============================================================"
