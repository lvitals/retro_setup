#!/bin/bash
set -e

SET_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$SET_DIR/scripts"

export RETRO_SETUP_MODE="steam"

command="${1:-}"

usage() {
    cat <<EOF
Usage: $0 [command]

Commands:
  --prepare   prepare Steam RetroArch once
  --select    select platforms and install everything they need for Steam RetroArch
  --install   continue/re-run installation for saved platforms
  --uninstall select platforms to remove what was installed for them
  --thumbnails download only thumbnails matching installed ROM names
  --implode   remove retro_setup configuration from Steam RetroArch
  --status    show platforms and configuration files
  --help      show this help

Set STEAM_RA_DIR when RetroArch is installed outside Steam's standard paths.
EOF
}

case "$command" in
    --help|-h|"")
        usage
        exit 0
        ;;
esac

# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

if [ -z "${STEAM_RA_DIR:-}" ]; then
    STEAM_RA_DIR="$(detect_steam_retroarch_dir || true)"
fi

if [ -z "$STEAM_RA_DIR" ] || [ ! -d "$STEAM_RA_DIR" ]; then
    echo "ERROR: RetroArch for Steam was not detected in standard paths."
    echo "Please set STEAM_RA_DIR environment variable to your RetroArch Steam directory."
    echo "Example: STEAM_RA_DIR=\"$HOME/.local/share/Steam/steamapps/common/RetroArch\" $0 --prepare"
    exit 1
fi

export RA_DIR="$STEAM_RA_DIR"
export RETRO_SETUP_CONFIG="${RETRO_SETUP_CONFIG:-$RETRO_SETUP_CONFIG_DIR/retro_setup_steam.conf}"

install_selected_platforms() {
    selected_platforms_or_all
    "$SCRIPT_DIR/download_bios.sh"
    "$SCRIPT_DIR/setup_retroarch.sh"
    "$SCRIPT_DIR/download_roms.sh"
    "$SCRIPT_DIR/generate_playlists.sh"
}

status() {
    detect_linux_distribution
    echo "Distribution: $DISTRO_NAME ($DISTRO_ID)"
    echo "Mode: Steam RetroArch"
    echo "Steam RetroArch Location: $RA_DIR"
    show_selected_platforms
    echo "URLs: $RETRO_URL_CONFIG"
}

case "$command" in
    --prepare)
        "$SCRIPT_DIR/prepare_retroarch.sh"
        ;;
    --select)
        interactive_select_platforms
        install_selected_platforms
        ;;
    --install)
        install_selected_platforms
        ;;
    --uninstall)
        interactive_uninstall_platforms
        ;;
    --thumbnails)
        selected_platforms_or_all
        "$SCRIPT_DIR/generate_playlists.sh"
        "$SCRIPT_DIR/download_thumbnails.sh"
        ;;
    --implode)
        "$SCRIPT_DIR/implode_retroarch.sh"
        ;;
    --status)
        status
        ;;
    *)
        usage
        exit 1
        ;;
esac
