#!/bin/bash

# Generate RetroArch playlists (.lpl) for selected platforms.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$SCRIPT_DIR/retro_setup_common.sh"

selected_platforms_or_all

PLAYLIST_DIR="$RA_DIR/playlists"
CORES_DIR="$RA_DIR/cores"
mkdir -p "$PLAYLIST_DIR"

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

has_valid_extension() {
    local file="$1"
    local extensions="$2"
    local lower ext
    lower="$(printf '%s' "$file" | tr '[:upper:]' '[:lower:]')"

    case "$lower" in
        *.zip|*.7z) return 0 ;;
    esac

    for ext in $extensions; do
        case "$lower" in
            *."$ext") return 0 ;;
        esac
    done
    return 1
}

generate_playlist() {
    local platform="$1"
    local playlist_name="${PLATFORM_NAME[$platform]}"
    local rom_dir="$ROM_BASE_DIR/$platform"
    local extensions="${PLATFORM_EXTENSIONS[$platform]}"
    local core_file core_name core_path playlist_file

    IFS='|' read -r core_file core_name <<< "${PLATFORM_CORE[$platform]}"
    core_path="$CORES_DIR/$core_file"
    playlist_file="$PLAYLIST_DIR/$playlist_name.lpl"

    if [ ! -d "$rom_dir" ]; then
        echo "Warning: directory not found for $platform: $rom_dir"
        return
    fi

    echo "------------------------------------------"
    echo "Generating playlist: $playlist_name"

    {
        printf '{\n'
        printf '  "version": "1.5",\n'
        printf '  "default_core_path": "%s",\n' "$(json_escape "$core_path")"
        printf '  "default_core_name": "%s",\n' "$(json_escape "$core_name")"
        printf '  "label_display_mode": 0,\n'
        printf '  "right_thumbnail_mode": 0,\n'
        printf '  "left_thumbnail_mode": 0,\n'
        printf '  "sort_mode": 0,\n'
        printf '  "items": [\n'
    } > "$playlist_file"

    local first=true count=0 rom_path filename label
    while IFS= read -r rom_path; do
        filename="$(basename "$rom_path")"
        case "$filename" in
            pack.7z|pack.zip|Champion\ Collection*) continue ;;
        esac
        has_valid_extension "$filename" "$extensions" || continue

        if [ "$first" = true ]; then
            first=false
        else
            printf '    ,\n' >> "$playlist_file"
        fi

        label="${filename%.*}"
        cat >> "$playlist_file" <<EOF
    {
      "path": "$(json_escape "$rom_path")",
      "label": "$(json_escape "$label")",
      "core_path": "$(json_escape "$core_path")",
      "core_name": "$(json_escape "$core_name")",
      "crc32": "DETECT",
      "db_name": "$(json_escape "$playlist_name.lpl")"
    }
EOF
        count=$((count + 1))
    done < <(find "$rom_dir" -type f | sort)

    {
        printf '  ]\n'
        printf '}\n'
    } >> "$playlist_file"

    echo "Done: $count items in $playlist_file"
}

echo "=== Generating playlists ==="
for platform in "${SELECTED_PLATFORMS[@]}"; do
    generate_playlist "$platform"
done

echo "------------------------------------------"
echo "Playlists generated."
