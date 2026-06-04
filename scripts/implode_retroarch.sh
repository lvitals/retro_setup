#!/bin/bash

# Remove local RetroArch configuration.
# Does not remove this repository, ROMs, downloaded cores/BIOS, or the saved selection.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

TARGETS=(
    "$HOME/.config/retroarch"
    "$HOME/.cache/retroarch"
    "$HOME/.local/share/retroarch"
)

echo "=== Implode RetroArch ==="
echo "This removes local RetroArch configuration:"
for target in "${TARGETS[@]}"; do
    echo "  - $target"
done
echo
echo "Does not remove:"
echo "  - $SET_DIR"
echo "  - $ROM_BASE_DIR"
echo "  - $RETRO_SETUP_CONFIG"
echo

if [ "${RETRO_SETUP_ASSUME_YES:-0}" != "1" ]; then
    printf "Type IMPLODE to confirm: "
    read -r confirmation
    if [ "$confirmation" != "IMPLODE" ]; then
        echo "Canceled."
        exit 1
    fi
fi

for target in "${TARGETS[@]}"; do
    if [ -e "$target" ]; then
        rm -rf "$target"
        echo "Removed: $target"
    else
        echo "Does not exist: $target"
    fi
done

echo "Implode complete. Run $SET_DIR/retro_setup.sh --prepare to recreate the default configuration."
