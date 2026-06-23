#!/bin/bash

# Catalogo unico usado pelos scripts do retro_setup.
# Mantem id, playlist RetroArch, core, extensoes, BIOS e observacoes em um lugar.

PLATFORM_IDS=(
    nes snes n64 gb gbc gba fds satellaview gamecube
    mastersystem gamegear megadrive sega32x segacd sg1000 saturn
    neogeo neogeocd neogeopocket neogeopocketcolor
    msx msx2 msxturbor
    atomiswave flycast model2 supermodel
    turbografx wonderswan colecovision intellivision odyssey2 pcfx
    atari2600 atari5200 atari7800 atarilynx atarijaguar atarijaguarcd
)

declare -A PLATFORM_NAME=(
    [nes]="Nintendo - Nintendo Entertainment System"
    [snes]="Nintendo - Super Nintendo Entertainment System"
    [n64]="Nintendo - Nintendo 64"
    [gb]="Nintendo - Game Boy"
    [gbc]="Nintendo - Game Boy Color"
    [gba]="Nintendo - Game Boy Advance"
    [fds]="Nintendo - Famicom Disk System"
    [satellaview]="Nintendo - Satellaview"
    [gamecube]="Nintendo - GameCube"
    [mastersystem]="Sega - Master System - Mark III"
    [gamegear]="Sega - Game Gear"
    [megadrive]="Sega - Mega Drive - Genesis"
    [sega32x]="Sega - 32X"
    [segacd]="Sega - Mega-CD - Sega CD"
    [sg1000]="Sega - SG-1000"
    [saturn]="Sega - Saturn"
    [neogeo]="SNK - Neo Geo"
    [neogeocd]="SNK - Neo Geo CD"
    [neogeopocket]="SNK - Neo Geo Pocket"
    [neogeopocketcolor]="SNK - Neo Geo Pocket Color"
    [msx]="Microsoft - MSX"
    [msx2]="Microsoft - MSX2"
    [msxturbor]="Microsoft - MSX TurboR"
    [atomiswave]="Arcade - Sammy Atomiswave"
    [flycast]="Arcade - Sega NAOMI"
    [model2]="Arcade - Sega Model 2"
    [supermodel]="Arcade - Sega Model 3"
    [turbografx]="NEC - PC Engine - TurboGrafx 16"
    [wonderswan]="Bandai - WonderSwan"
    [colecovision]="Coleco - ColecoVision"
    [intellivision]="Mattel - Intellivision"
    [odyssey2]="Magnavox - Odyssey2"
    [pcfx]="NEC - PC-FX"
    [atari2600]="Atari - 2600"
    [atari5200]="Atari - 5200"
    [atari7800]="Atari - 7800"
    [atarilynx]="Atari - Lynx"
    [atarijaguar]="Atari - Jaguar"
    [atarijaguarcd]="Atari - Jaguar CD"
)

declare -A PLATFORM_CORE=(
    [nes]="mesen_libretro.so|Nintendo - Nintendo Entertainment System (Mesen)"
    [snes]="snes9x_libretro.so|Snes9x"
    [n64]="mupen64plus_next_libretro.so|Nintendo - Nintendo 64 (Mupen64Plus-Next)"
    [gb]="gambatte_libretro.so|Nintendo - Game Boy / Color (Gambatte)"
    [gbc]="gambatte_libretro.so|Nintendo - Game Boy / Color (Gambatte)"
    [gba]="mgba_libretro.so|Nintendo - Game Boy Advance (mGBA)"
    [fds]="fceumm_libretro.so|FCEUmm"
    [satellaview]="snes9x_libretro.so|Snes9x"
    [gamecube]="dolphin_libretro.so|Nintendo - GameCube / Wii (Dolphin)"
    [mastersystem]="genesis_plus_gx_libretro.so|Sega - MS/GG/MD/CD (Genesis Plus GX)"
    [gamegear]="genesis_plus_gx_libretro.so|Sega - MS/GG/MD/CD (Genesis Plus GX)"
    [megadrive]="genesis_plus_gx_libretro.so|Sega - MS/GG/MD/CD (Genesis Plus GX)"
    [sega32x]="picodrive_libretro.so|Sega - MS/GG/MD/CD/32X (PicoDrive)"
    [segacd]="genesis_plus_gx_libretro.so|Sega - MS/GG/MD/CD (Genesis Plus GX)"
    [sg1000]="genesis_plus_gx_libretro.so|Sega - MS/GG/MD/CD (Genesis Plus GX)"
    [saturn]="mednafen_saturn_libretro.so|Sega - Saturn (Beetle Saturn)"
    [neogeo]="fbneo_libretro.so|Arcade (FinalBurn Neo)"
    [neogeocd]="neocd_libretro.so|SNK - Neo Geo CD (NeoCD)"
    [neogeopocket]="mednafen_ngp_libretro.so|SNK - Neo Geo Pocket / Color (Beetle NeoPop)"
    [neogeopocketcolor]="mednafen_ngp_libretro.so|SNK - Neo Geo Pocket / Color (Beetle NeoPop)"
    [msx]="bluemsx_libretro.so|MSX/SVI/ColecoVision/SG-1000 (blueMSX)"
    [msx2]="bluemsx_libretro.so|MSX/SVI/ColecoVision/SG-1000 (blueMSX)"
    [msxturbor]="bluemsx_libretro.so|MSX/SVI/ColecoVision/SG-1000 (blueMSX)"
    [atomiswave]="flycast_libretro.so|Sega - Dreamcast/NAOMI (Flycast)"
    [flycast]="flycast_libretro.so|Sega - Dreamcast/NAOMI (Flycast)"
    [model2]="fbneo_libretro.so|Arcade (FinalBurn Neo)"
    [supermodel]="fbneo_libretro.so|Arcade (FinalBurn Neo)"
    [turbografx]="mednafen_pce_fast_libretro.so|NEC - PC Engine / CD (Beetle PCE FAST)"
    [wonderswan]="mednafen_wswan_libretro.so|Bandai - WonderSwan / Color (Beetle Cygne)"
    [colecovision]="bluemsx_libretro.so|MSX/SVI/ColecoVision/SG-1000 (blueMSX)"
    [intellivision]="freeintv_libretro.so|Mattel - Intellivision (FreeIntv)"
    [odyssey2]="o2em_libretro.so|Magnavox - Odyssey2 / Phillips Videopac+ (O2EM)"
    [pcfx]="mednafen_pcfx_libretro.so|NEC - PC-FX (Beetle PC-FX)"
    [atari2600]="stella_libretro.so|Atari - 2600 (Stella)"
    [atari5200]="a5200_libretro.so|Atari - 5200 (a5200)"
    [atari7800]="prosystem_libretro.so|Atari - 7800 (ProSystem)"
    [atarilynx]="handy_libretro.so|Atari - Lynx (Handy)"
    [atarijaguar]="virtualjaguar_libretro.so|Atari - Jaguar (Virtual Jaguar)"
    [atarijaguarcd]="virtualjaguar_libretro.so|Atari - Jaguar (Virtual Jaguar)"
)

declare -A PLATFORM_EXTENSIONS=(
    [nes]="nes" [snes]="sfc smc" [n64]="n64 z64 v64" [gb]="gb" [gbc]="gbc" [gba]="gba"
    [fds]="fds" [satellaview]="bs" [gamecube]="iso gcm rvz"
    [mastersystem]="sms" [gamegear]="gg" [megadrive]="md gen bin" [sega32x]="32x"
    [segacd]="chd cue iso" [sg1000]="sg" [saturn]="chd cue iso"
    [neogeo]="zip" [neogeocd]="chd cue iso" [neogeopocket]="ngp" [neogeopocketcolor]="ngc"
    [msx]="rom mx1 mx2 dsk" [msx2]="rom mx1 mx2 dsk" [msxturbor]="rom mx1 mx2 dsk"
    [atomiswave]="zip chd" [flycast]="zip chd" [model2]="zip" [supermodel]="zip"
    [turbografx]="pce chd cue" [wonderswan]="ws wsc" [colecovision]="col"
    [intellivision]="int bin" [odyssey2]="bin" [pcfx]="chd cue"
    [atari2600]="a26" [atari5200]="a52" [atari7800]="a78" [atarilynx]="lnx"
    [atarijaguar]="j64" [atarijaguarcd]="chd cue iso"
)

declare -A PLATFORM_BIOS=(
    [fds]="disksys.rom"
    [segacd]="bios_CD_U.bin bios_CD_E.bin bios_CD_J.bin"
    [saturn]="saturn_bios.bin mpr-17933.bin sega_101.bin"
    [neogeo]="neogeo.zip"
    [neogeocd]="neocd.bin"
    [msx]="Machines Databases"
    [msx2]="Machines Databases"
    [msxturbor]="Machines Databases"
    [colecovision]="Machines Databases"
    [intellivision]="exec.bin grom.bin"
    [odyssey2]="o2rom.bin"
    [pcfx]="pcfx.rom"
    [atari5200]="5200.rom"
    [atarilynx]="lynxboot.img"
)

platform_exists() {
    local id="$1"
    local p
    for p in "${PLATFORM_IDS[@]}"; do
        [ "$p" = "$id" ] && return 0
    done
    return 1
}
