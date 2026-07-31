#!/bin/bash

RETRO_SETUP_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_DIR="$RETRO_SETUP_COMMON_DIR"
SET_DIR="${RETRO_SETUP_DIR:-$(cd "$RETRO_SETUP_COMMON_DIR/.." && pwd)}"

detect_steam_retroarch_dir() {
    local candidates=(
        "${STEAM_RA_DIR:-}"
        "$HOME/.local/share/Steam/steamapps/common/RetroArch"
        "$HOME/.steam/steam/steamapps/common/RetroArch"
        "$HOME/.steam/root/steamapps/common/RetroArch"
        "$HOME/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/RetroArch"
        "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RetroArch"
    )
    local dir
    for dir in "${candidates[@]}"; do
        if [ -n "$dir" ] && [ -d "$dir" ] && ( [ -f "$dir/retroarch" ] || [ -f "$dir/retroarch.sh" ] ); then
            echo "$dir"
            return 0
        fi
    done
    return 1
}

if [ "${RETRO_SETUP_MODE:-}" = "steam" ]; then
    STEAM_RA_DIR="${STEAM_RA_DIR:-$(detect_steam_retroarch_dir || true)}"
    if [ -n "$STEAM_RA_DIR" ]; then
        RA_DIR="$STEAM_RA_DIR"
    else
        RA_DIR="${RA_DIR:-$HOME/.local/share/Steam/steamapps/common/RetroArch}"
    fi
    RETRO_SETUP_CONFIG_DIR="${RETRO_SETUP_CONFIG_DIR:-$HOME/.config/retro_setup}"
    RETRO_SETUP_CONFIG="${RETRO_SETUP_CONFIG:-$RETRO_SETUP_CONFIG_DIR/retro_setup_steam.conf}"
else
    RA_DIR="${RA_DIR:-$HOME/.config/retroarch}"
    RETRO_SETUP_CONFIG_DIR="${RETRO_SETUP_CONFIG_DIR:-$HOME/.config/retro_setup}"
    RETRO_SETUP_CONFIG="${RETRO_SETUP_CONFIG:-$RETRO_SETUP_CONFIG_DIR/retro_setup.conf}"
fi

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
    if [ "${RETRO_SETUP_MODE:-}" = "steam" ]; then
        if [ ! -d "$RA_DIR" ] || ( [ ! -f "$RA_DIR/retroarch" ] && [ ! -f "$RA_DIR/retroarch.sh" ] ); then
            echo "ERROR: RetroArch for Steam not found at: $RA_DIR"
            return 1
        fi
    else
        install_tool_package retroarch retroarch retroarch retroarch retroarch retroarch
    fi
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

uninstall_platforms() {
    local targets=("$@")
    if [ "${#targets[@]}" -eq 0 ]; then
        echo "No platforms specified for uninstallation."
        return 0
    fi

    load_selected_platforms

    echo "=== Uninstalling Selected Platforms ==="

    local new_selected=()
    local id target keep
    for id in "${SELECTED_PLATFORMS[@]}"; do
        keep=true
        for target in "${targets[@]}"; do
            if [ "$id" = "$target" ]; then
                keep=false
                break
            fi
        done
        if [ "$keep" = true ]; then
            new_selected+=("$id")
        fi
    done
    SELECTED_PLATFORMS=("${new_selected[@]}")
    write_selected_platforms

    for target in "${targets[@]}"; do
        if ! platform_exists "$target"; then
            echo "Ignoring unknown platform: $target"
            continue
        fi

        echo "------------------------------------------"
        echo "Uninstalling platform: $target (${PLATFORM_NAME[$target]:-$target})"

        local rom_dir="$ROM_BASE_DIR/$target"
        if [ -d "$rom_dir" ]; then
            rm -rf "$rom_dir"
            echo "  Removed ROMs directory: $rom_dir"
        fi

        local playlist_file="$RA_DIR/playlists/${PLATFORM_NAME[$target]}.lpl"
        if [ -f "$playlist_file" ]; then
            rm -f "$playlist_file"
            echo "  Removed playlist: $playlist_file"
        fi

        local thumb_dir="$RA_DIR/thumbnails/${PLATFORM_NAME[$target]}"
        if [ -d "$thumb_dir" ]; then
            rm -rf "$thumb_dir"
            echo "  Removed thumbnails: $thumb_dir"
        fi

        if [ -n "${PLATFORM_BIOS[$target]:-}" ]; then
            local bios bios_needed rem_platform rem_bios
            for bios in ${PLATFORM_BIOS[$target]}; do
                bios_needed=false
                for rem_platform in "${SELECTED_PLATFORMS[@]}"; do
                    if [ -n "${PLATFORM_BIOS[$rem_platform]:-}" ]; then
                        for rem_bios in ${PLATFORM_BIOS[$rem_platform]}; do
                            if [ "$rem_bios" = "$bios" ]; then
                                bios_needed=true
                                break 2
                            fi
                        done
                    fi
                done

                if [ "$bios_needed" = false ]; then
                    if [ -e "$RA_DIR/system/$bios" ]; then
                        rm -rf "$RA_DIR/system/$bios"
                        echo "  Removed BIOS from RetroArch system: $bios"
                    fi
                    if [ -e "$SET_DIR/bios/$bios" ]; then
                        rm -rf "$SET_DIR/bios/$bios"
                        echo "  Removed BIOS from local storage: $bios"
                    fi
                    if [ -f "$SET_DIR/bios/$bios.zip" ]; then
                        rm -f "$SET_DIR/bios/$bios.zip"
                    fi
                    if [ -f "$SET_DIR/bios/$bios.7z" ]; then
                        rm -f "$SET_DIR/bios/$bios.7z"
                    fi
                else
                    echo "  Keeping BIOS (still needed by remaining platform): $bios"
                fi
            done
        fi

        if [ -n "${PLATFORM_CORE[$target]:-}" ]; then
            local core_file core_name rem_platform rem_core_file rem_core_name core_needed info_file
            IFS='|' read -r core_file core_name <<< "${PLATFORM_CORE[$target]}"
            info_file="${core_file%.so}.info"

            core_needed=false
            for rem_platform in "${SELECTED_PLATFORMS[@]}"; do
                if [ -n "${PLATFORM_CORE[$rem_platform]:-}" ]; then
                    IFS='|' read -r rem_core_file rem_core_name <<< "${PLATFORM_CORE[$rem_platform]}"
                    if [ "$rem_core_file" = "$core_file" ]; then
                        core_needed=true
                        break
                    fi
                fi
            done

            if [ "$core_needed" = false ]; then
                if [ -f "$RA_DIR/cores/$core_file" ]; then
                    rm -f "$RA_DIR/cores/$core_file"
                    echo "  Removed core from RetroArch: $core_file"
                fi
                if [ -f "$SET_DIR/cores/$core_file" ]; then
                    rm -f "$SET_DIR/cores/$core_file"
                    echo "  Removed local core: $core_file"
                fi
                if [ -f "$SET_DIR/cores/$core_file.zip" ]; then
                    rm -f "$SET_DIR/cores/$core_file.zip"
                fi
                if [ -f "$RA_DIR/core_info/$info_file" ]; then
                    rm -f "$RA_DIR/core_info/$info_file"
                    echo "  Removed core info: $info_file"
                fi
                if [ -f "$SET_DIR/info/$info_file" ]; then
                    rm -f "$SET_DIR/info/$info_file"
                    echo "  Removed local info: $info_file"
                fi
                if [ "${RETRO_SETUP_MODE:-}" = "steam" ] && [ -f "$RA_DIR/cores/$info_file" ]; then
                    rm -f "$RA_DIR/cores/$info_file"
                fi
            else
                echo "  Keeping core (still needed by remaining platform): $core_file"
            fi
        fi
    done

    echo "------------------------------------------"
    echo "Uninstallation complete."
    show_selected_platforms
}

interactive_uninstall_platforms() {
    ensure_config_dir
    load_selected_platforms

    echo "Select platforms to uninstall/remove."
    echo "Enter numbers separated by commas or spaces, 'all' for all, or press Enter to cancel."
    echo

    local i=1 id
    for id in "${PLATFORM_IDS[@]}"; do
        printf "%2d) [ ] %-16s %s\n" "$i" "$id" "${PLATFORM_NAME[$id]}"
        i=$((i + 1))
    done

    echo
    printf "Selection to uninstall: "
    read -r answer
    if [ -z "$answer" ]; then
        echo "No platform selected for uninstallation. Canceled."
        return 0
    fi

    local to_uninstall=()
    if [ "$answer" = "all" ]; then
        to_uninstall=("${PLATFORM_IDS[@]}")
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
            to_uninstall+=("${PLATFORM_IDS[$((n - 1))]}")
        done
    fi

    if [ "${#to_uninstall[@]}" -eq 0 ]; then
        echo "No valid platform selected to uninstall."
        return 0
    fi

    uninstall_platforms "${to_uninstall[@]}"
}

