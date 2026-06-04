# retro_setup

Simple automation to install and configure RetroArch platforms.

## Recommended Flow

```bash
cd ~/retro_setup
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
- RetroArch, core, BIOS, ROM, and thumbnail URLs: `~/retro_setup/retro_url.config`
- Local BIOS files: `~/retro_setup/bios`
- Downloaded ROMs: `~/retro_setup/roms`
- Main script: `~/retro_setup/retro_setup.sh`
- Internal helper scripts: `~/retro_setup/scripts`

When a platform is selected, the installer downloads/installs the core, core info, configured BIOS, configured ROMs, and generates playlists. Downloads use `wget --continue`.

RetroArch is configured with `network_on_demand_thumbnails = "true"`, so it can fetch thumbnails automatically as needed. Use `./retro_setup.sh --thumbnails` only if you want to pre-download thumbnails locally.

`--prepare` detects the distribution through `/etc/os-release` and uses the corresponding package manager when supported: Arch/pacman, Debian/Ubuntu/apt, Fedora/RHEL/dnf, openSUSE/zypper or Alpine/apk.

`--implode` removes `~/.config/retroarch`, `~/.cache/retroarch` and `~/.local/share/retroarch`. It preserves `~/retro_setup`, the ROMs inside it and the saved selection.
