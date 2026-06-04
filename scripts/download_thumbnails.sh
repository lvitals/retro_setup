#!/bin/bash

# Download RetroArch thumbnails only for selected platforms.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

selected_platforms_or_all
load_url_config

THUMB_DIR="$RA_DIR/thumbnails"
PLAYLIST_DIR="$RA_DIR/playlists"
BASE_URL="${THUMBNAILS_BASE_URL:-https://thumbnails.libretro.com}"
TYPES=("Named_Boxarts" "Named_Snaps" "Named_Titles")

UA="Mozilla/5.0 (X11; Linux) AppleWebKit/537.36 (KHTML, like Gecko) Chrome Safari"
WGET_OPTS=(
    --user-agent="$UA"
    --continue
    --tries=2
    --waitretry=2
    --timeout=15
    --retry-connrefused
    --show-progress
)

if command -v wget >/dev/null 2>&1 && wget --help 2>/dev/null | grep -q -- '--retry-on-http-error'; then
    WGET_OPTS+=(--retry-on-http-error=429,500,502,503,504)
fi

mkdir -p "$THUMB_DIR"

url_encode_component() {
    local value="$1"
    if command -v python3 >/dev/null 2>&1; then
        python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1], safe=""))' "$value"
    else
        printf '%s' "$value" | sed 's/%/%25/g; s/ /%20/g; s/#/%23/g; s/&/%26/g; s/+/%2B/g'
    fi
}

playlist_item_count() {
    local playlist="$1"
    grep -c '^[[:space:]]*"label": ' "$playlist" || true
}

download_thumbnail_type() {
    local system="$1"
    local type="$2"
    local system_encoded url dest before after

    system_encoded="$(url_encode_component "$system")"
    url="${BASE_URL%/}/$system_encoded/$type/"
    dest="$THUMB_DIR/$system/$type"

    mkdir -p "$dest"
    before="$(find "$dest" -type f -name '*.png' 2>/dev/null | wc -l)"

    echo "Downloading $type for $system..."
    echo "Source: $url"

    if wget \
        "${WGET_OPTS[@]}" \
        --recursive \
        --level=1 \
        --no-parent \
        --no-directories \
        --accept='*.png' \
        --reject='index.html*' \
        --execute robots=off \
        --directory-prefix="$dest" \
        "$url"; then
        after="$(find "$dest" -type f -name '*.png' 2>/dev/null | wc -l)"
        echo "$type: before=$before after=$after"
    else
        echo "WARNING: thumbnail download failed for $system / $type"
    fi
}

if ! command -v wget >/dev/null 2>&1; then
    install_tool_package wget wget wget wget wget wget
fi

echo "=== Starting Thumbnail Download ==="
echo "Base URL: ${BASE_URL%/}"

for platform in "${SELECTED_PLATFORMS[@]}"; do
    system="${PLATFORM_NAME[$platform]}"
    playlist="$PLAYLIST_DIR/$system.lpl"

    if [ ! -f "$playlist" ]; then
        echo "Playlist not found for $platform: $playlist"
        echo "Run generate_playlists.sh first if you want thumbnails for this system."
        continue
    fi

    item_count="$(playlist_item_count "$playlist")"
    echo "--- Processing System: $system ($item_count playlist items) ---"

    for type in "${TYPES[@]}"; do
        download_thumbnail_type "$system" "$type"
    done
done

echo "=== Thumbnail Download Complete ==="
