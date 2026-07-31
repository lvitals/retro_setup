#!/bin/bash

# Uninstall selected platforms and remove installed components.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

if [ $# -gt 0 ]; then
    uninstall_platforms "$@"
else
    interactive_uninstall_platforms
fi
