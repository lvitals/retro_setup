#!/bin/bash

RETRO_SETUP_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$RETRO_SETUP_COMMON_DIR"
SET_DIR="${RETRO_SETUP_DIR:-$(cd "$RETRO_SETUP_COMMON_DIR/.." && pwd)}"
RA_DIR="${RA_DIR:-$HOME/.config/retroarch}"
RETRO_SETUP_CONFIG_DIR="${RETRO_SETUP_CONFIG_DIR:-$HOME/.config/retro_setup}"
RETRO_SETUP_CONFIG="${RETRO_SETUP_CONFIG:-$RETRO_SETUP_CONFIG_DIR/retro_setup.conf}"
RETRO_URL_CONFIG="${RETRO_URL_CONFIG:-$SET_DIR/retro_url.config}"
ROM_SOURCES_FILE="${ROM_SOURCES_FILE:-$RETRO_URL_CONFIG}"
ROM_BASE_DIR="${ROM_BASE_DIR:-$SET_DIR/roms}"

# shellcheck source=/dev/null
. "$SCRIPT_DIR/libretro_platforms.sh"

SELECTED_PLATFORMS=()

load_url_config() {
    create_rom_sources_file
    # shellcheck source=/dev/null
    . "$RETRO_URL_CONFIG"
}

detect_libretro_arch() {
    local arch
    arch="$(uname -m)"
    case "$arch" in
        x86_64) echo "x86_64" ;;
        aarch64) echo "aarch64" ;;
        armv7l) echo "armhf" ;;
        i686|i386) echo "x86" ;;
        *) echo "" ;;
    esac
}

detect_linux_distribution() {
    DISTRO_ID="unknown"
    DISTRO_NAME="Linux"
    DISTRO_ID_LIKE=""

    if [ -f /etc/os-release ]; then
        # shellcheck source=/dev/null
        . /etc/os-release
        DISTRO_ID="${ID:-unknown}"
        DISTRO_NAME="${PRETTY_NAME:-${NAME:-Linux}}"
        DISTRO_ID_LIKE="${ID_LIKE:-}"
    fi
}

is_distro_like() {
    local expected="$1"
    [ "${DISTRO_ID:-}" = "$expected" ] && return 0
    case " ${DISTRO_ID_LIKE:-} " in
        *" $expected "*) return 0 ;;
    esac
    return 1
}

install_system_packages() {
    detect_linux_distribution

    echo "Distribution detected: $DISTRO_NAME ($DISTRO_ID)"

    if is_distro_like arch; then
        sudo pacman -Sy --needed --noconfirm "$@"
    elif is_distro_like debian || is_distro_like ubuntu; then
        sudo apt-get update
        sudo apt-get install -y "$@"
    elif is_distro_like fedora; then
        sudo dnf install -y "$@"
    elif is_distro_like rhel; then
        sudo dnf install -y "$@" || sudo yum install -y "$@"
    elif is_distro_like suse || is_distro_like opensuse; then
        sudo zypper --non-interactive install "$@"
    elif is_distro_like alpine; then
        sudo apk add "$@"
    else
        echo "Distribution is not automatically supported. Install manually: $*"
        return 1
    fi
}

install_tool_package() {
    local tool="$1"
    local pkg_debian="$2"
    local pkg_arch="$3"
    local pkg_fedora="$4"
    local pkg_suse="$5"
    local pkg_alpine="$6"
    local package="$pkg_debian"

    command -v "$tool" >/dev/null 2>&1 && return 0

    detect_linux_distribution
    if is_distro_like arch; then
        package="$pkg_arch"
    elif is_distro_like fedora || is_distro_like rhel; then
        package="$pkg_fedora"
    elif is_distro_like suse || is_distro_like opensuse; then
        package="$pkg_suse"
    elif is_distro_like alpine; then
        package="$pkg_alpine"
    fi

    install_system_packages "$package"
}

ensure_retroarch_dependencies() {
    install_tool_package retroarch retroarch retroarch retroarch retroarch retroarch
    install_tool_package wget wget wget wget wget wget
    install_tool_package unzip unzip unzip unzip unzip unzip
    install_tool_package 7z p7zip-full p7zip p7zip p7zip p7zip
}

ensure_config_dir() {
    mkdir -p "$RETRO_SETUP_CONFIG_DIR"
}

write_selected_platforms() {
    ensure_config_dir
    {
        echo "# Persistent retro_setup configuration"
        echo "# Edit SELECTED_PLATFORMS or run $SET_DIR/retro_setup.sh --select"
        printf "SELECTED_PLATFORMS=("
        local id
        for id in "${SELECTED_PLATFORMS[@]}"; do
            printf " %q" "$id"
        done
        echo " )"
    } > "$RETRO_SETUP_CONFIG"
}

load_selected_platforms() {
    ensure_config_dir
    SELECTED_PLATFORMS=()
    if [ -f "$RETRO_SETUP_CONFIG" ]; then
        # shellcheck source=/dev/null
        . "$RETRO_SETUP_CONFIG"
    fi
}

selected_platforms_or_all() {
    load_selected_platforms
    if [ "${#SELECTED_PLATFORMS[@]}" -eq 0 ]; then
        echo "No platform selected."
        echo "Run: $SET_DIR/retro_setup.sh --select"
        exit 1
    fi
}

show_selected_platforms() {
    load_selected_platforms
    if [ "${#SELECTED_PLATFORMS[@]}" -eq 0 ]; then
        echo "No platform selected yet."
        return
    fi

    echo "Selected platforms in $RETRO_SETUP_CONFIG:"
    local id
    for id in "${SELECTED_PLATFORMS[@]}"; do
        printf "  - %-16s %s\n" "$id" "${PLATFORM_NAME[$id]}"
    done
}

create_rom_sources_file() {
    ensure_config_dir
    [ -f "$RETRO_URL_CONFIG" ] && return

    {
        echo "# Central retro_setup URLs."
        echo "# The installer reads this file automatically."
        echo
        echo 'CORE_INFO_URL="https://buildbot.libretro.com/assets/frontend/bundle/retroarch-core-info.zip"'
        echo 'DATABASE_RDB_URL="https://buildbot.libretro.com/assets/frontend/database-rdb.zip"'
        echo 'DATABASE_CURSORS_URL="https://buildbot.libretro.com/assets/frontend/database-cursors.zip"'
        echo 'THUMBNAILS_BASE_URL="https://thumbnails.libretro.com"'
        echo
        local id
        for id in "${PLATFORM_IDS[@]}"; do
            echo "# ${PLATFORM_NAME[$id]}"
            echo "# ROM_URLS_$id=()"
            echo "# ROM_DIR_URLS_$id=()"
            echo
        done
    } > "$RETRO_URL_CONFIG"
}

copy_selected_bios() {
    local src_dir="${1:-$SET_DIR/bios}"
    local dst_dir="${2:-$RA_DIR/system}"
    local platform bios source found_path

    mkdir -p "$src_dir" "$dst_dir"

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        [ -n "${PLATFORM_BIOS[$platform]:-}" ] || continue
        for bios in ${PLATFORM_BIOS[$platform]}; do
            source="$src_dir/$bios"
            found_path=""

            if [ -e "$source" ]; then
                found_path="$source"
            else
                found_path="$(find "$src_dir" -name "$(basename "$bios")" -print -quit 2>/dev/null)"
            fi

            if [ -n "$found_path" ]; then
                cp -R "$found_path" "$dst_dir/"
                echo "BIOS synced: $platform -> $(basename "$found_path")"
            fi
        done
    done
}

check_selected_bios() {
    local dst_dir="${1:-$RA_DIR/system}"
    local missing=false
    local platform bios

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        [ -n "${PLATFORM_BIOS[$platform]:-}" ] || continue

        echo "$platform - ${PLATFORM_NAME[$platform]}"
        for bios in ${PLATFORM_BIOS[$platform]}; do
            if [ -e "$dst_dir/$bios" ] || find "$dst_dir" -name "$(basename "$bios")" -print -quit 2>/dev/null | grep -q .; then
                echo "  ok: $bios"
            else
                echo "  missing: $bios"
                missing=true
            fi
        done
    done

    [ "$missing" = false ]
}

download_selected_core_assets() {
    load_url_config

    local arch base_url core_file core_name platform core_zip info_file
    if ! command -v wget >/dev/null 2>&1; then
        install_tool_package wget wget wget wget wget wget
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        install_tool_package unzip unzip unzip unzip unzip unzip
    fi

    arch="$(detect_libretro_arch)"
    if [ -z "$arch" ]; then
        echo "Architecture is not supported for automatic core download: $(uname -m)"
        return 1
    fi

    base_url="${LIBRETRO_CORE_BASE_URL:-https://buildbot.libretro.com/nightly/linux/$arch/latest}"
    mkdir -p "$SET_DIR/cores" "$SET_DIR/info"

    local needs_info=false
    for platform in "${SELECTED_PLATFORMS[@]}"; do
        IFS='|' read -r core_file core_name <<< "${PLATFORM_CORE[$platform]}"
        info_file="${core_file%.so}.info"
        [ -f "$SET_DIR/info/$info_file" ] || needs_info=true
    done

    if [ "$needs_info" = true ]; then
        echo "Downloading core info..."
        wget --continue --tries=10 --waitretry=5 --retry-connrefused \
            "${CORE_INFO_URL:-https://buildbot.libretro.com/assets/frontend/bundle/retroarch-core-info.zip}" \
            -O "$SET_DIR/info/retroarch-core-info.zip" --show-progress &&
            unzip -o "$SET_DIR/info/retroarch-core-info.zip" -d "$SET_DIR/info"
    fi

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        IFS='|' read -r core_file core_name <<< "${PLATFORM_CORE[$platform]}"
        if [ -f "$SET_DIR/cores/$core_file" ]; then
            echo "Local core ok: $platform -> $core_file"
            continue
        fi

        core_zip="$SET_DIR/cores/$core_file.zip"
        echo "Downloading core $platform: $core_file ($core_name)"
        if wget --continue --tries=10 --waitretry=5 --retry-connrefused \
            "$base_url/$core_file.zip" \
            -O "$core_zip" --show-progress; then
            unzip -o "$core_zip" -d "$SET_DIR/cores"
        else
            echo "WARNING: failed to download core $core_file"
        fi
    done
}

interactive_select_platforms() {
    ensure_config_dir
    load_selected_platforms

    echo "Select platforms to configure/download."
    echo "Enter numbers separated by commas or spaces, 'all' for all, or Enter to keep the current selection."
    echo

    local i=1 id marker selected
    for id in "${PLATFORM_IDS[@]}"; do
        marker=" "
        for selected in "${SELECTED_PLATFORMS[@]}"; do
            [ "$selected" = "$id" ] && marker="x"
        done
        printf "%2d) [%s] %-16s %s\n" "$i" "$marker" "$id" "${PLATFORM_NAME[$id]}"
        i=$((i + 1))
    done

    echo
    printf "Selection: "
    read -r answer
    if [ -z "$answer" ]; then
        if [ "${#SELECTED_PLATFORMS[@]}" -eq 0 ]; then
            echo "No saved selection. Nothing to run."
            return 1
        fi
        return 0
    fi

    SELECTED_PLATFORMS=()
    if [ "$answer" = "all" ]; then
        SELECTED_PLATFORMS=("${PLATFORM_IDS[@]}")
    else
        local n normalized_answer
        normalized_answer="${answer//,/ }"
        for n in $normalized_answer; do
            if ! [[ "$n" =~ ^[0-9]+$ ]]; then
                echo "Ignoring invalid entry: $n"
                continue
            fi
            if [ "$n" -lt 1 ] || [ "$n" -gt "${#PLATFORM_IDS[@]}" ]; then
                echo "Ignoring number outside the list: $n"
                continue
            fi
            SELECTED_PLATFORMS+=("${PLATFORM_IDS[$((n - 1))]}")
        done
    fi

    write_selected_platforms
    create_rom_sources_file
    show_selected_platforms
    echo "URL file: $RETRO_URL_CONFIG"
}
