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
    if ! grep -Eq '^[[:space:]]*\[[[:alnum:]_]+\][[:space:]]*$' "$RETRO_URL_CONFIG"; then
        # Backward compatibility with configurations created before the INI format.
        # shellcheck source=/dev/null
        . "$RETRO_URL_CONFIG"
        return
    fi

    local id
    for id in "${PLATFORM_IDS[@]}"; do
        unset "BIOS_URLS_$id" "ROM_URLS_$id" "ROM_DIR_URLS_$id" "ROM_CATALOG_URLS_$id" "ARCHIVE_COMPRESS_URLS_$id"
    done

    local section="" line key value variable
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [ -z "$line" ] && continue
        [[ "$line" == \#* ]] && continue
        if [[ "$line" =~ ^\[([[:alnum:]_]+)\]$ ]]; then
            section="${BASH_REMATCH[1]}"
            continue
        fi
        [[ "$line" == *=* ]] || continue
        key="${line%%=*}"
        value="${line#*=}"
        key="${key%"${key##*[![:space:]]}"}"
        value="${value#"${value%%[![:space:]]*}"}"
        value="${value%"${value##*[![:space:]]}"}"
        if [[ "$value" == \"*\" && "$value" == *\" ]] || [[ "$value" == \'*\' && "$value" == *\' ]]; then
            value="${value:1:${#value}-2}"
        fi
        [ -n "$value" ] || continue

        if [ "$section" = global ]; then
            case "$key" in
                core_info_url) variable=CORE_INFO_URL ;;
                libretro_core_base_url) variable=LIBRETRO_CORE_BASE_URL ;;
                database_rdb_url) variable=DATABASE_RDB_URL ;;
                database_cursors_url) variable=DATABASE_CURSORS_URL ;;
                thumbnails_base_url) variable=THUMBNAILS_BASE_URL ;;
                archive_download_base_url) variable=ARCHIVE_DOWNLOAD_BASE_URL ;;
                *) continue ;;
            esac
            printf -v "$variable" '%s' "$value"
            continue
        fi

        case "$key" in
            bios_url) variable="BIOS_URLS_$section" ;;
            rom_url) variable="ROM_URLS_$section" ;;
            rom_directory_url) variable="ROM_DIR_URLS_$section" ;;
            rom_catalog_url) variable="ROM_CATALOG_URLS_$section" ;;
            archive_item) variable="ARCHIVE_COMPRESS_URLS_$section" ;;
            *) continue ;;
        esac
        local -n url_values="$variable"
        url_values+=("$value")
        unset -n url_values
    done < "$RETRO_URL_CONFIG"
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
        echo '[global]'
        echo 'core_info_url = https://buildbot.libretro.com/assets/frontend/info.zip'
        echo 'database_rdb_url = https://buildbot.libretro.com/assets/frontend/database-rdb.zip'
        echo 'database_cursors_url = https://buildbot.libretro.com/assets/frontend/database-cursors.zip'
        echo 'thumbnails_base_url = https://thumbnails.libretro.com'
        echo 'archive_download_base_url = https://archive.org/download'
        echo
        local id
        for id in "${PLATFORM_IDS[@]}"; do
            echo "# ${PLATFORM_NAME[$id]}"
            echo "[$id]"
            echo '# bios_url = https://example.com/bios.zip'
            echo '# rom_url = https://example.com/rom-pack.zip'
            echo '# rom_directory_url = https://example.com/archive-directory/'
            echo '# rom_catalog_url = https://archive.org/metadata/archive-item-id'
            echo
        done
    } > "$RETRO_URL_CONFIG"
}

copy_selected_bios() {
    local src_dir="${1:-$SET_DIR/bios}"
    local dst_dir="${2:-$RA_DIR/system}"
    local platform bios source found_path dest_path file relative min_size extensions file_ext

    mkdir -p "$src_dir" "$dst_dir"

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        [ -n "${PLATFORM_BIOS[$platform]:-}" ] || continue

        for bios in ${PLATFORM_BIOS[$platform]}; do
            if [ -d "$dst_dir/$bios" ]; then
                while IFS= read -r -d '' file; do
                    if head -c 256 "$file" 2>/dev/null | tr '[:upper:]' '[:lower:]' | grep -Eq '<!doctype html|<html'; then
                        rm -f "$file"
                        echo "Removed HTML masquerading as firmware: $file"
                    fi
                done < <(find "$dst_dir/$bios" -type f -print0 2>/dev/null)
            fi
        done

        for bios in ${PLATFORM_BIOS[$platform]}; do
            source="$src_dir/$bios"
            found_path=""

            if [ -e "$source" ]; then
                found_path="$source"
            else
                found_path="$(find "$src_dir" -mindepth 1 -name "$(basename "$bios")" -print -quit 2>/dev/null)"
            fi

            if [ -n "$found_path" ]; then
                if { [ -n "${PLATFORM_BIOS_EXTENSIONS[$platform]:-}" ] ||
                     [ "${PLATFORM_BIOS_MIN_SIZE[$platform]:-0}" -gt 0 ]; } &&
                   ! platform_bios_valid "$platform" "$src_dir"; then
                    echo "WARNING: ignoring invalid local BIOS set: $platform -> $found_path"
                    continue
                fi
                dest_path="$dst_dir/$bios"
                if [ -d "$found_path" ]; then
                    mkdir -p "$dest_path"
                    min_size=0
                    extensions="${PLATFORM_BIOS_COPY_EXTENSIONS[$platform]:-${PLATFORM_BIOS_EXTENSIONS[$platform]:-}}"
                    if [ -n "$extensions" ] || [ "$min_size" -gt 0 ]; then
                        while IFS= read -r -d '' file; do
                            [ "$(stat -c %s "$file" 2>/dev/null || echo 0)" -ge "$min_size" ] || continue
                            if [ -n "$extensions" ]; then
                                file_ext="${file##*.}"
                                file_ext="${file_ext,,}"
                                case " $extensions " in
                                    *" $file_ext "*) ;;
                                    *) continue ;;
                                esac
                            fi
                            relative="${file#"$found_path"/}"
                            mkdir -p "$dest_path/$(dirname "$relative")"
                            cp -f "$file" "$dest_path/$relative"
                        done < <(find "$found_path" -type f -print0 2>/dev/null)
                    else
                        cp -a "$found_path/." "$dest_path/"
                    fi
                else
                    mkdir -p "$(dirname "$dest_path")"
                    cp -f "$found_path" "$dest_path"
                fi
                echo "BIOS synced: $platform -> $bios"
            fi
        done
    done
}

platform_bios_valid() {
    local platform="$1" dst_dir="$2"
    local bios file min_size extensions size file_ext valid
    [ -n "${PLATFORM_BIOS[$platform]:-}" ] || return 0
    for bios in ${PLATFORM_BIOS[$platform]}; do
        min_size="${PLATFORM_BIOS_MIN_SIZE[$platform]:-1}"
        extensions="${PLATFORM_BIOS_EXTENSIONS[$platform]:-}"
        valid=false
        while IFS= read -r -d '' file; do
            size="$(stat -c %s "$file" 2>/dev/null || echo 0)"
            [ "$size" -ge "$min_size" ] || continue
            if [ -n "$extensions" ]; then
                file_ext="${file##*.}"
                file_ext="${file_ext,,}"
                case " $extensions " in *" $file_ext "*) ;; *) continue ;; esac
            fi
            valid=true
            break
        done < <(if [ -d "$dst_dir/$bios" ]; then find "$dst_dir/$bios" -type f -print0 2>/dev/null; elif [ -f "$dst_dir/$bios" ]; then printf '%s\0' "$dst_dir/$bios"; fi)
        [ "$valid" = true ] || return 1
    done
    return 0
}

platform_selected_core() {
    local platform="$1" primary="${PLATFORM_CORE[$platform]}"
    if [ "${PLATFORM_FALLBACK_WITHOUT_BIOS[$platform]:-false}" = true ] &&
       [ -n "${PLATFORM_FALLBACK_CORE[$platform]:-}" ] &&
       ! platform_bios_valid "$platform" "$RA_DIR/system" &&
       ! platform_bios_valid "$platform" "$SET_DIR/bios"; then
        printf '%s\n' "${PLATFORM_FALLBACK_CORE[$platform]}"
    else
        printf '%s\n' "$primary"
    fi
}

configure_selected_core_profiles() {
    local platform selected core_file config_name config_dir options frontend_options fallback_entry fallback_file
    for platform in "${SELECTED_PLATFORMS[@]}"; do
        selected="$(platform_selected_core "$platform")"
        IFS='|' read -r core_file _ <<< "$selected"
        fallback_entry="${PLATFORM_FALLBACK_CORE[$platform]:-}"
        fallback_file="${fallback_entry%%|*}"
        if [ -n "$fallback_file" ] && [ "$core_file" = "$fallback_file" ]; then
            config_name="${PLATFORM_FALLBACK_CONFIG_NAME[$platform]:-}"
            options="${PLATFORM_FALLBACK_CORE_OPTIONS[$platform]:-}"
            frontend_options="${PLATFORM_FALLBACK_FRONTEND_OPTIONS[$platform]:-}"
        else
            config_name="${PLATFORM_CORE_CONFIG_NAME[$platform]:-}"
            options="${PLATFORM_CORE_OPTIONS[$platform]:-}"
            frontend_options="${PLATFORM_FRONTEND_OPTIONS[$platform]:-}"
        fi
        [ -n "$config_name" ] || continue
        config_dir="$RA_DIR/config/$config_name"
        mkdir -p "$config_dir"
        printf '%s\n' "$options" | tr ';' '\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' > "$config_dir/$config_name.opt"
        printf '%s\n' "$frontend_options" | tr ';' '\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' > "$config_dir/$config_name.cfg"
        echo "Core profile configured: $platform -> $config_name"
    done
}

check_selected_bios() {
    local dst_dir="${1:-$RA_DIR/system}"
    local missing=false
    local platform bios file valid min_size extensions size file_ext

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        [ -n "${PLATFORM_BIOS[$platform]:-}" ] || continue

        echo "$platform - ${PLATFORM_NAME[$platform]}"
        for bios in ${PLATFORM_BIOS[$platform]}; do
            min_size="${PLATFORM_BIOS_MIN_SIZE[$platform]:-1}"
            extensions="${PLATFORM_BIOS_EXTENSIONS[$platform]:-}"
            valid=false
            while IFS= read -r -d '' file; do
                size="$(stat -c %s "$file" 2>/dev/null || echo 0)"
                [ "$size" -ge "$min_size" ] || continue
                if [ -n "$extensions" ]; then
                    file_ext="${file##*.}"
                    file_ext="${file_ext,,}"
                    case " $extensions " in
                        *" $file_ext "*) ;;
                        *) continue ;;
                    esac
                fi
                valid=true
                break
            done < <(if [ -d "$dst_dir/$bios" ]; then find "$dst_dir/$bios" -type f -print0 2>/dev/null; elif [ -f "$dst_dir/$bios" ]; then printf '%s\0' "$dst_dir/$bios"; fi)
            if [ "$valid" = true ]; then
                echo "  ok: $bios"
            else
                echo "  missing or invalid: $bios${extensions:+ (.$extensions, at least $min_size bytes)}"
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
        echo "Note: wget is recommended for core downloads."
    fi
    if ! command -v unzip >/dev/null 2>&1; then
        echo "Note: unzip is required to extract downloaded cores."
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
        IFS='|' read -r core_file core_name <<< "$(platform_selected_core "$platform")"
        info_file="${core_file%.so}.info"
        [ -f "$SET_DIR/info/$info_file" ] || needs_info=true
    done

    if [ "$needs_info" = true ]; then
        echo "Downloading core info..."
        local info_zip="$SET_DIR/info/retroarch-core-info.zip"
        [ -s "$info_zip" ] || rm -f "$info_zip"
        if command -v wget >/dev/null 2>&1; then
            wget --continue --tries=10 --waitretry=5 --retry-connrefused \
                "${CORE_INFO_URL:-https://buildbot.libretro.com/assets/frontend/info.zip}" \
                -O "$info_zip" --show-progress &&
                unzip -o "$info_zip" -d "$SET_DIR/info"
        elif command -v curl >/dev/null 2>&1; then
            curl -L -f -o "$info_zip" "${CORE_INFO_URL:-https://buildbot.libretro.com/assets/frontend/info.zip}" &&
                unzip -o "$info_zip" -d "$SET_DIR/info"
        fi
    fi

    for platform in "${SELECTED_PLATFORMS[@]}"; do
        IFS='|' read -r core_file core_name <<< "$(platform_selected_core "$platform")"
        if [ -f "$SET_DIR/cores/$core_file" ]; then
            echo "Local core ok: $platform -> $core_file"
            continue
        fi

        core_zip="$SET_DIR/cores/$core_file.zip"
        echo "Downloading core $platform: $core_file ($core_name)"
        local dl_success=false
        if command -v wget >/dev/null 2>&1; then
            if wget --continue --tries=10 --waitretry=5 --retry-connrefused \
                "$base_url/$core_file.zip" \
                -O "$core_zip" --show-progress; then
                unzip -o "$core_zip" -d "$SET_DIR/cores"
                dl_success=true
            fi
        fi

        if [ "$dl_success" = false ]; then
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
