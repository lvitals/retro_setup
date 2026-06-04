#!/bin/bash
set -e

SET_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$SET_DIR/scripts"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

usage() {
    cat <<EOF
Usage: ./retro_setup.sh [command]

Commands:
  --prepare   prepare RetroArch once
  --select    select platforms and install everything they need
  --install   continue/re-run installation for saved platforms
  --thumbnails download thumbnails for saved platforms
  --implode   remove local RetroArch configuration
  --status    show platforms and configuration files
  --help      show this help

Files:
  Platforms: $RETRO_SETUP_CONFIG
  URLs:        $RETRO_URL_CONFIG
EOF
}

install_selected_platforms() {
    selected_platforms_or_all
    "$SCRIPT_DIR/setup_retroarch.sh"
    "$SCRIPT_DIR/download_bios.sh"
    "$SCRIPT_DIR/download_roms.sh"
    "$SCRIPT_DIR/generate_playlists.sh"
}

status() {
    detect_linux_distribution
    echo "Distribution: $DISTRO_NAME ($DISTRO_ID)"
    show_selected_platforms
    echo "URLs: $RETRO_URL_CONFIG"
}

case "${1:-}" in
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
    --thumbnails)
        selected_platforms_or_all
        "$SCRIPT_DIR/download_thumbnails.sh"
        ;;
    --implode)
        "$SCRIPT_DIR/implode_retroarch.sh"
        ;;
    --status)
        status
        ;;
    --help|-h|"")
        usage
        ;;
    *)
        usage
        exit 1
        ;;
esac
