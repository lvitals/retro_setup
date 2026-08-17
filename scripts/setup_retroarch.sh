#!/bin/bash

# Script to configure and link RetroArch folders
# Ensures cores, info files, and BIOS files are installed in the configured RetroArch paths.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"
selected_platforms_or_all

RA_CONFIG="$RA_DIR/retroarch.cfg"

echo "=== Starting RetroArch Configuration ==="

# 1. Install RetroArch if missing
if [ "${RETRO_SETUP_MODE:-}" = "steam" ]; then
    if [ ! -d "$RA_DIR" ] || ( [ ! -f "$RA_DIR/retroarch" ] && [ ! -f "$RA_DIR/retroarch.sh" ] ); then
        echo "ERROR: RetroArch for Steam not found at: $RA_DIR"
        exit 1
    fi
    echo "Using Steam RetroArch at: $RA_DIR"
else
    if ! command -v retroarch &> /dev/null; then
        echo "RetroArch not found. Installing according to the distribution..."
        install_tool_package retroarch retroarch retroarch retroarch retroarch retroarch
    fi
fi

# 2. Create required directory structure
mkdir -p "$RA_DIR/cores" "$RA_DIR/system" "$RA_DIR/core_info" "$RA_DIR/playlists"

# Import user-provided firmware before selecting a BIOS-dependent core.
copy_selected_bios "$SET_DIR/bios" "$RA_DIR/system"

# 2.1 Download missing cores/info files for selected platforms
download_selected_core_assets

# 3. Install cores (.so) for selected platforms
if [ -d "$SET_DIR/cores" ]; then
    echo "Installing cores..."
    for platform in "${SELECTED_PLATFORMS[@]}"; do
        IFS='|' read -r core_file core_name <<< "$(platform_selected_core "$platform")"
        if [ -f "$SET_DIR/cores/$core_file" ]; then
            cp -v "$SET_DIR/cores/$core_file" "$RA_DIR/cores/"
        else
            echo "WARNING: core not found for $platform: $core_file ($core_name)"
        fi
    done
fi

# 4. Install core info (.info) for selected platforms
if [ -d "$SET_DIR/info" ]; then
    echo "Installing core info files..."
    for platform in "${SELECTED_PLATFORMS[@]}"; do
        IFS='|' read -r core_file _ <<< "$(platform_selected_core "$platform")"
        info_file="${core_file%.so}.info"
        if [ -f "$SET_DIR/info/$info_file" ]; then
            cp -v "$SET_DIR/info/$info_file" "$RA_DIR/core_info/"
            if [ "${RETRO_SETUP_MODE:-}" = "steam" ]; then
                cp -v "$SET_DIR/info/$info_file" "$RA_DIR/cores/" 2>/dev/null || true
            fi
        else
            echo "WARNING: info not found for $platform: $info_file"
        fi
    done
fi

# 5. Update retroarch.cfg with paths and enable thumbnails
echo "Adjusting paths in retroarch.cfg..."
if [ -f "$RA_CONFIG" ]; then
    # Safety backup
    cp "$RA_CONFIG" "$RA_CONFIG.bak"
    
    if [ "${RETRO_SETUP_MODE:-}" = "steam" ]; then
        sed -i "s|^libretro_directory =.*|libretro_directory = \"$RA_DIR/cores\"|" "$RA_CONFIG"
        sed -i "s|^libretro_info_path =.*|libretro_info_path = \"$RA_DIR/core_info\"|" "$RA_CONFIG"
        sed -i "s|^system_directory =.*|system_directory = \"$RA_DIR/system\"|" "$RA_CONFIG"
        sed -i "s|^playlist_directory =.*|playlist_directory = \"$RA_DIR/playlists\"|" "$RA_CONFIG"
        sed -i "s|^thumbnails_directory =.*|thumbnails_directory = \"$RA_DIR/thumbnails\"|" "$RA_CONFIG"
        grep -q "assets_directory =" "$RA_CONFIG" && sed -i "s|^assets_directory =.*|assets_directory = \"$RA_DIR/assets\"|" "$RA_CONFIG" || echo "assets_directory = \"$RA_DIR/assets\"" >> "$RA_CONFIG"
    else
        sed -i "s|^libretro_directory =.*|libretro_directory = \"~/.config/retroarch/cores\"|" "$RA_CONFIG"
        sed -i "s|^libretro_info_path =.*|libretro_info_path = \"~/.config/retroarch/core_info\"|" "$RA_CONFIG"
        sed -i "s|^system_directory =.*|system_directory = \"~/.config/retroarch/system\"|" "$RA_CONFIG"
        sed -i "s|^playlist_directory =.*|playlist_directory = \"~/.config/retroarch/playlists\"|" "$RA_CONFIG"
        sed -i "s|^thumbnails_directory =.*|thumbnails_directory = \"~/.config/retroarch/thumbnails\"|" "$RA_CONFIG"
    fi
    
    # Add or update thumbnail settings
    grep -q "network_on_demand_thumbnails =" "$RA_CONFIG" && sed -i 's|^network_on_demand_thumbnails =.*|network_on_demand_thumbnails = "true"|' "$RA_CONFIG" || echo 'network_on_demand_thumbnails = "true"' >> "$RA_CONFIG"
    grep -q "quick_menu_show_download_thumbnails =" "$RA_CONFIG" && sed -i 's|^quick_menu_show_download_thumbnails =.*|quick_menu_show_download_thumbnails = "true"|' "$RA_CONFIG" || echo 'quick_menu_show_download_thumbnails = "true"' >> "$RA_CONFIG"

    echo "Paths and thumbnail settings updated successfully."
else
    echo "WARNING: retroarch.cfg not found. Open RetroArch once to generate it, then run this script again."
fi

configure_selected_core_profiles

echo "=== Configuration Complete! ==="
echo "BIOS files, cores, and paths are ready to use."
