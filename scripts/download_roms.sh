#!/bin/bash

# Download ROMs for selected platforms.
# Sources live in the repository's retro_url.config by default.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

selected_platforms_or_all
load_url_config

mkdir -p "$ROM_BASE_DIR"

UA="Mozilla/5.0 (X11; Linux) AppleWebKit/537.36 (KHTML, like Gecko) Chrome Safari"
WGET_OPTS=(
    --user-agent="$UA"
    --header="Accept-Encoding: identity"
    --continue
    --tries=10
    --waitretry=5
    --timeout=60
    --retry-connrefused
    --show-progress
)

ensure_dependency() {
    local bin="$1"
    if ! command -v "$bin" >/dev/null 2>&1; then
        case "$bin" in
            wget) install_tool_package wget wget wget wget wget wget ;;
            unzip) install_tool_package unzip unzip unzip unzip unzip unzip ;;
            7z) install_tool_package 7z p7zip-full p7zip p7zip p7zip p7zip ;;
        esac
    fi
}

extract_archive_once() {
    local archive="$1"
    local dest_dir="$2"
    local marker="$dest_dir/.extracted.$(basename "$archive").ok"

    if [ -f "$marker" ] && [ "$marker" -nt "$archive" ]; then
        echo "Archive already extracted: $(basename "$archive")"
        return
    fi

    case "$archive" in
        *.zip)
            unzip -o "$archive" -d "$dest_dir" && touch "$marker"
            ;;
        *.7z|*.rar)
            7z x "$archive" -o"$dest_dir" -y && touch "$marker"
            ;;
        *)
            echo "No automatic extraction for: $archive"
            ;;
    esac
}

download_file() {
    local platform="$1"
    local url="$2"
    local dest_dir="$ROM_BASE_DIR/$platform"
    local filename

    mkdir -p "$dest_dir"
    filename="$(basename "${url%%\?*}")"
    [ -n "$filename" ] || filename="$platform.download"

    echo "Downloading $platform: $filename"
    if wget "${WGET_OPTS[@]}" "$url" -O "$dest_dir/$filename"; then
        extract_archive_once "$dest_dir/$filename" "$dest_dir"
    else
        echo "Download interrupted for $platform. Run again to continue."
    fi
}

download_archive_item() {
    local platform="$1"
    local item_id="$2"
    local dest_dir="$ROM_BASE_DIR/$platform"
    local filename="$item_id.zip"
    local url="https://archive.org/compress/$item_id/formats=ZIP&file=/$item_id.zip"

    mkdir -p "$dest_dir"
    echo "Downloading Archive.org item $platform: $item_id"
    if wget "${WGET_OPTS[@]}" "$url" -O "$dest_dir/$filename"; then
        extract_archive_once "$dest_dir/$filename" "$dest_dir"
    else
        echo "Archive.org item download interrupted for $platform. Run again to continue."
    fi
}

download_directory() {
    local platform="$1"
    local url="$2"
    local dest_dir="$ROM_BASE_DIR/$platform"

    mkdir -p "$dest_dir"
    echo "Downloading directory $platform: $url"
    wget \
        "${WGET_OPTS[@]}" \
        --recursive \
        --no-parent \
        --no-directories \
        --accept "zip,7z,iso,chd,bin,cue,rvz,gcm,n64,z64,v64,nes,sfc,smc,gb,gbc,gba,a26,a52,a78,lnx,md,gen,sms,gg,32x,sg,pce,ws,wsc,col,int,rom,mx1,mx2,dsk,j64" \
        "$url" \
        -P "$dest_dir"
}

get_array_values() {
    local array_name="$1"
    if declare -p "$array_name" >/dev/null 2>&1; then
        local -n ref="$array_name"
        printf '%s\n' "${ref[@]}"
    fi
}

ensure_dependency wget
ensure_dependency unzip
ensure_dependency 7z

if wget --help 2>/dev/null | grep -q -- '--retry-on-http-error'; then
    WGET_OPTS+=(--retry-on-http-error=429,500,502,503,504)
fi

echo "=== ROM Download ==="
echo "Platform config: $RETRO_SETUP_CONFIG"
echo "URLs: $RETRO_URL_CONFIG"

for platform in "${SELECTED_PLATFORMS[@]}"; do
    echo "------------------------------------------"
    echo "SYSTEM: $platform - ${PLATFORM_NAME[$platform]}"

    found_source=false
    while IFS= read -r url; do
        [ -z "$url" ] && continue
        found_source=true
        download_file "$platform" "$url"
    done < <(get_array_values "ROM_URLS_$platform")

    while IFS= read -r url; do
        [ -z "$url" ] && continue
        found_source=true
        download_directory "$platform" "$url"
    done < <(get_array_values "ROM_DIR_URLS_$platform")

    while IFS= read -r item_id; do
        [ -z "$item_id" ] && continue
        found_source=true
        download_archive_item "$platform" "$item_id"
    done < <(get_array_values "ARCHIVE_COMPRESS_URLS_$platform")

    if [ "$found_source" = false ]; then
        echo "No source configured for $platform."
        echo "Edit $RETRO_URL_CONFIG and add ROM_URLS_$platform=(\"https://...\")"
    fi
done

echo "=========================================="
echo "ROM processing complete."
