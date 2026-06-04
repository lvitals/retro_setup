#!/bin/bash

# One-time default preparation.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"
load_url_config

RA_CONFIG="$RA_DIR/retroarch.cfg"

set_config_value() {
    local key="$1"
    local value="$2"
    if grep -q "^$key =" "$RA_CONFIG"; then
        sed -i "s|^$key =.*|$key = \"$value\"|" "$RA_CONFIG"
    else
        echo "$key = \"$value\"" >> "$RA_CONFIG"
    fi
}

echo "=== Default RetroArch Preparation ==="

detect_linux_distribution
echo "Distribution: $DISTRO_NAME ($DISTRO_ID)"
ensure_retroarch_dependencies

mkdir -p \
    "$RA_DIR/cores" \
    "$RA_DIR/system" \
    "$RA_DIR/core_info" \
    "$RA_DIR/playlists" \
    "$RA_DIR/thumbnails" \
    "$RA_DIR/database/rdb" \
    "$RA_DIR/database/cursors" \
    "$SET_DIR/cores" \
    "$SET_DIR/info" \
    "$SET_DIR/bios"

if [ ! -f "$RA_CONFIG" ]; then
    echo "retroarch.cfg not found. Generating initial configuration..."
    timeout 8 retroarch --menu --verbose >/dev/null 2>&1 || true
fi

if [ -f "$RA_CONFIG" ]; then
    cp "$RA_CONFIG" "$RA_CONFIG.bak"
    set_config_value libretro_directory "~/.config/retroarch/cores"
    set_config_value libretro_info_path "~/.config/retroarch/core_info"
    set_config_value system_directory "~/.config/retroarch/system"
    set_config_value playlist_directory "~/.config/retroarch/playlists"
    set_config_value thumbnails_directory "~/.config/retroarch/thumbnails"
    set_config_value network_on_demand_thumbnails "true"
    set_config_value quick_menu_show_download_thumbnails "true"
    set_config_value assets_directory "/usr/share/retroarch/assets"
    set_config_value menu_driver "xmb"
    set_config_value xmb_menu_color_theme "4"
    set_config_value xmb_theme "0"
    set_config_value menu_shader_pipeline "2"
    set_config_value menu_dynamic_wallpaper_enable "true"
    set_config_value menu_show_advanced_settings "true"
else
    echo "WARNING: could not generate $RA_CONFIG now."
fi

echo "Downloading RetroArch databases..."
wget --continue --tries=10 --waitretry=5 --retry-connrefused \
    "${DATABASE_RDB_URL:-https://buildbot.libretro.com/assets/frontend/database-rdb.zip}" \
    -O "$RA_DIR/database/rdb/database.zip" --show-progress
unzip -o "$RA_DIR/database/rdb/database.zip" -d "$RA_DIR/database/rdb"
if [ -d "$RA_DIR/database/rdb/rdb" ]; then
    mv "$RA_DIR/database/rdb/rdb/"* "$RA_DIR/database/rdb/"
    rmdir "$RA_DIR/database/rdb/rdb"
fi

wget --continue --tries=10 --waitretry=5 --retry-connrefused \
    "${DATABASE_CURSORS_URL:-https://buildbot.libretro.com/assets/frontend/database-cursors.zip}" \
    -O "$RA_DIR/database/cursors/cursors.zip" --show-progress
unzip -o "$RA_DIR/database/cursors/cursors.zip" -d "$RA_DIR/database/cursors"
if [ -d "$RA_DIR/database/cursors/cursors" ]; then
    mv "$RA_DIR/database/cursors/cursors/"* "$RA_DIR/database/cursors/"
    rmdir "$RA_DIR/database/cursors/cursors"
fi

echo "Preparation complete."
