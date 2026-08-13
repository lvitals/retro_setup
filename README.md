# retro_setup

Simple automation to install and configure RetroArch platforms.

## Recommended Flow

```bash
git clone <repo-url> /path/to/retro_setup
cd /path/to/retro_setup
./retro_setup.sh --prepare
./retro_setup.sh --select
```

After that, to continue interrupted downloads or reinstall saved platforms:

```bash
./retro_setup.sh --install
```

With no arguments, `./retro_setup.sh` or `./retro_setup_steam.sh` shows help.

## Usage for Steam RetroArch

If you are using RetroArch via **Steam**, use `retro_setup_steam.sh`:

```bash
./retro_setup_steam.sh --prepare   # prepare Steam RetroArch once
./retro_setup_steam.sh --select    # select platforms and install everything for Steam RetroArch
./retro_setup_steam.sh --install   # continue/re-run saved platforms
./retro_setup_steam.sh --uninstall # select platforms to remove what was installed for them
./retro_setup_steam.sh --thumbnails # pre-download thumbnails
./retro_setup_steam.sh --implode   # remove retro_setup config from Steam RetroArch
./retro_setup_steam.sh --status    # show Steam RetroArch location and selected platforms
```

`retro_setup_steam.sh` automatically detects your Steam RetroArch directory (e.g. `~/.local/share/Steam/steamapps/common/RetroArch`). You can also override it with `STEAM_RA_DIR`:

```bash
STEAM_RA_DIR=/path/to/Steam/steamapps/common/RetroArch ./retro_setup_steam.sh --select
```

## Commands (Standalone RetroArch)

```bash
./retro_setup.sh --prepare   # prepare RetroArch once
./retro_setup.sh --select    # select platforms and install everything they need
./retro_setup.sh --install   # continue/re-run saved platforms
./retro_setup.sh --uninstall # select platforms to remove what was installed for them
./retro_setup.sh --thumbnails # optionally pre-download thumbnails
./retro_setup.sh --implode   # remove local RetroArch configuration
./retro_setup.sh --status    # show platforms and configs
```

## Configuration

- Selected platforms (Standalone): `~/.config/retro_setup/retro_setup.conf`
- Selected platforms (Steam): `~/.config/retro_setup/retro_setup_steam.conf`
- RetroArch, core, BIOS, ROM, and thumbnail URLs: `<repo>/retro_url.config`

`retro_url.config` uses the same section-based organization as `platforms.config`.
Global assets belong in `[global]`; platform sources belong in sections such as
`[nes]`. Repeat a key when a platform has more than one source:

```ini
[nes]
rom_url = https://example.com/GoodNES.zip

[ps1]
bios_url = https://example.com/scph5501.bin
rom_directory_url = https://example.com/playstation-library/
rom_directory_url = https://example.com/playstation-language-pack/
```

Supported platform keys are `bios_url`, `rom_url`, `rom_directory_url`, and
`archive_item`. URLs are consumed exactly as written, so an already encoded URL
containing `%20` must not be encoded a second time. The GUI and both Shell
installers also continue to accept the previous Bash-style file format.
- Local BIOS files: `<repo>/bios`
- Downloaded ROMs: `<repo>/roms`
- Main script (Standalone): `<repo>/retro_setup.sh`
- Main script (Steam): `<repo>/retro_setup_steam.sh`
- Internal helper scripts: `<repo>/scripts`

The project resolves `<repo>` from the location of `retro_setup.sh` / `retro_setup_steam.sh`, so it can be cloned in any directory. You can also override paths with environment variables:

```bash
RETRO_SETUP_DIR=/path/to/retro_setup RETRO_URL_CONFIG=/path/to/retro_url.config ROM_BASE_DIR=/path/to/roms ./retro_setup_steam.sh --install
```

When a platform is selected, the installer downloads/installs the core, core info, configured BIOS, configured ROMs, and generates playlists. Downloads use `wget --continue`.

RetroArch is configured with `network_on_demand_thumbnails = "true"`, so it can fetch thumbnails automatically as needed. Use `--thumbnails` only if you want to pre-download thumbnails locally.

`--prepare` detects the distribution through `/etc/os-release` and uses the corresponding package manager when supported: Arch/pacman, Debian/Ubuntu/apt, Fedora/RHEL/dnf, openSUSE/zypper or Alpine/apk.

`--implode` for standalone RetroArch removes `~/.config/retroarch`, `~/.cache/retroarch` and `~/.local/share/retroarch`. For Steam RetroArch, it removes generated playlists, core info, thumbnails, and `retroarch.cfg` inside the Steam RetroArch directory without deleting the base Steam installation. It preserves the repository directory, ROMs, and saved platform selection.
