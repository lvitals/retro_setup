#!/bin/bash

# Download, sync, and verify BIOS files for selected platforms.
# URLs live in the repository's retro_url.config by default.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

selected_platforms_or_all
load_url_config

RA_SYSTEM_DIR="$RA_DIR/system"
SET_BIOS_DIR="$SET_DIR/bios"

mkdir -p "$SET_BIOS_DIR" "$RA_SYSTEM_DIR"

WGET_OPTS=(
    --continue
    --tries=10
    --waitretry=5
    --timeout=60
    --retry-connrefused
    --show-progress
)

if command -v wget >/dev/null 2>&1 && wget --help 2>/dev/null | grep -q -- '--retry-on-http-error'; then
    WGET_OPTS+=(--retry-on-http-error=429,500,502,503,504)
fi

get_array_values() {
    local array_name="$1"
    if declare -p "$array_name" >/dev/null 2>&1; then
        local -n ref="$array_name"
        printf '%s\n' "${ref[@]}"
    fi
}

download_bios_url() {
    local platform="$1"
    local url="$2"
    local filename dest install_relative set_target ra_target

    filename="$(basename "${url%%\?*}")"
    [ -n "$filename" ] || filename="$platform.bios"
    install_relative="${PLATFORM_BIOS_INSTALL_DIRECTORY[$platform]:-}"
    if [ -n "$install_relative" ] && [[ "$install_relative" != /* ]] && [[ "$install_relative" != *..* ]]; then
        set_target="$SET_BIOS_DIR/$install_relative"
        ra_target="$RA_SYSTEM_DIR/$install_relative"
    else
        set_target="$SET_BIOS_DIR"
        ra_target="$RA_SYSTEM_DIR"
    fi
    mkdir -p "$set_target" "$ra_target"
    dest="$set_target/$filename"

    echo "Downloading BIOS $platform: $filename"
    if wget "${WGET_OPTS[@]}" "$url" -O "$dest"; then
        case "$dest" in
            *.zip)
                unzip -o "$dest" -d "$set_target"
                unzip -o "$dest" -d "$ra_target"
                ;;
            *.7z)
                7z x "$dest" -o"$set_target" -y
                7z x "$dest" -o"$ra_target" -y
                ;;
            *)
                cp "$dest" "$ra_target/"
                ;;
        esac
    else
        echo "WARNING: BIOS download failed for $platform"
    fi
}

echo "------------------------------------------"
echo "BIOS"
echo "URLs:    $RETRO_URL_CONFIG"
echo "Source:  $SET_BIOS_DIR"
echo "Target:  $RA_SYSTEM_DIR"
echo "------------------------------------------"

for platform in "${SELECTED_PLATFORMS[@]}"; do
    while IFS= read -r url; do
        [ -z "$url" ] && continue
        download_bios_url "$platform" "$url"
    done < <(get_array_values "BIOS_URLS_$platform")
done

if [ -z "$(find "$SET_BIOS_DIR" -mindepth 1 -print -quit 2>/dev/null)" ]; then
    echo "No local file found in $SET_BIOS_DIR."
else
    copy_selected_bios "$SET_BIOS_DIR" "$RA_SYSTEM_DIR"
fi

echo "------------------------------------------"
echo "Checking expected BIOS files"

if ! check_selected_bios "$RA_SYSTEM_DIR"; then
    echo "Some expected BIOS files were not found. Adjust bios_url in the platform section of $RETRO_URL_CONFIG or copy the files to $SET_BIOS_DIR."
else
    echo "Expected BIOS files found for selected platforms."
fi

missing_required=false
for platform in "${SELECTED_PLATFORMS[@]}"; do
    if [ "${PLATFORM_BIOS_REQUIRED[$platform]:-false}" = true ] &&
       ! platform_bios_valid "$platform" "$RA_SYSTEM_DIR"; then
        echo "ERROR: ${PLATFORM_NAME[$platform]} requires a valid user-provided BIOS before installation can continue."
        missing_required=true
    fi
done
[ "$missing_required" = false ]
