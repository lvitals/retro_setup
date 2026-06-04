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

With no arguments, `./retro_setup.sh` shows help.

## Commands

```bash
./retro_setup.sh --prepare   # prepare RetroArch once
./retro_setup.sh --select    # select platforms and install everything they need
./retro_setup.sh --install   # continue/re-run saved platforms
./retro_setup.sh --thumbnails # optionally pre-download thumbnails
./retro_setup.sh --implode   # remove local RetroArch configuration
./retro_setup.sh --status    # show platforms and configs
```

## Configuration

- Selected platforms: `~/.config/retro_setup/retro_setup.conf`
- RetroArch, core, BIOS, ROM, and thumbnail URLs: `<repo>/retro_url.config`
- Local BIOS files: `<repo>/bios`
- Downloaded ROMs: `<repo>/roms`
- Main script: `<repo>/retro_setup.sh`
- Internal helper scripts: `<repo>/scripts`

The project resolves `<repo>` from the location of `retro_setup.sh`, so it can be cloned in any directory. You can also override paths with environment variables:

```bash
RETRO_SETUP_DIR=/path/to/retro_setup RETRO_URL_CONFIG=/path/to/retro_url.config ROM_BASE_DIR=/path/to/roms ./retro_setup.sh --install
```

When a platform is selected, the installer downloads/installs the core, core info, configured BIOS, configured ROMs, and generates playlists. Downloads use `wget --continue`.

RetroArch is configured with `network_on_demand_thumbnails = "true"`, so it can fetch thumbnails automatically as needed. Use `./retro_setup.sh --thumbnails` only if you want to pre-download thumbnails locally.

`--prepare` detects the distribution through `/etc/os-release` and uses the corresponding package manager when supported: Arch/pacman, Debian/Ubuntu/apt, Fedora/RHEL/dnf, openSUSE/zypper or Alpine/apk.

`--implode` removes `~/.config/retroarch`, `~/.cache/retroarch` and `~/.local/share/retroarch`. It preserves the repository directory, the ROMs inside it and the saved selection.
