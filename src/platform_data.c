#include "platform_data.h"
#include <string.h>

// Platform Catalog matching scripts/libretro_platforms.sh exactly
PlatformInfo g_platforms[TOTAL_PLATFORMS] = {
    // Nintendo (Red / Red-White / Crimson) - Accent 0xE60012
    { "nes", "Nintendo - Nintendo Entertainment System", MFR_NINTENDO, "mesen_libretro.so", "Nintendo - Nintendo Entertainment System (Mesen)", "nes", "", 0xE60012, false },
    { "snes", "Nintendo - Super Nintendo Entertainment System", MFR_NINTENDO, "snes9x_libretro.so", "Snes9x", "sfc smc", "", 0xE60012, false },
    { "n64", "Nintendo - Nintendo 64", MFR_NINTENDO, "mupen64plus_next_libretro.so", "Nintendo - Nintendo 64 (Mupen64Plus-Next)", "n64 z64 v64", "", 0xE60012, false },
    { "gb", "Nintendo - Game Boy", MFR_NINTENDO, "gambatte_libretro.so", "Nintendo - Game Boy / Color (Gambatte)", "gb", "", 0xE60012, false },
    { "gbc", "Nintendo - Game Boy Color", MFR_NINTENDO, "gambatte_libretro.so", "Nintendo - Game Boy / Color (Gambatte)", "gbc", "", 0xE60012, false },
    { "gba", "Nintendo - Game Boy Advance", MFR_NINTENDO, "mgba_libretro.so", "Nintendo - Game Boy Advance (mGBA)", "gba", "", 0xE60012, false },
    { "fds", "Nintendo - Famicom Disk System", MFR_NINTENDO, "fceumm_libretro.so", "Nintendo - NES / Famicom (FCEUmm)", "fds", "disksys.rom", 0xE60012, false },
    { "satellaview", "Nintendo - Satellaview", MFR_NINTENDO, "snes9x_libretro.so", "Snes9x", "bs", "", 0xE60012, false },
    { "gamecube", "Nintendo - GameCube", MFR_NINTENDO, "dolphin_libretro.so", "Nintendo - GameCube / Wii (Dolphin)", "iso gcm rvz", "", 0xE60012, false },

    // Sony (PlayStation Blue) - Accent 0x003791
    { "ps1", "Sony - PlayStation", MFR_SONY, "pcsx_rearmed_libretro.so", "Sony - PlayStation (PCSX ReARMed)", "chd cue iso bin pbp", "scph5500.bin scph5501.bin scph5502.bin", 0x0050FF, false },

    // Sega (Sega Blue / Cyan) - Accent 0x0080FF
    { "mastersystem", "Sega - Master System - Mark III", MFR_SEGA, "genesis_plus_gx_libretro.so", "Sega - MS/GG/MD/CD (Genesis Plus GX)", "sms", "", 0x0080FF, false },
    { "gamegear", "Sega - Game Gear", MFR_SEGA, "genesis_plus_gx_libretro.so", "Sega - MS/GG/MD/CD (Genesis Plus GX)", "gg", "", 0x0080FF, false },
    { "megadrive", "Sega - Mega Drive - Genesis", MFR_SEGA, "genesis_plus_gx_libretro.so", "Sega - MS/GG/MD/CD (Genesis Plus GX)", "md gen bin", "", 0x0080FF, false },
    { "sega32x", "Sega - 32X", MFR_SEGA, "picodrive_libretro.so", "Sega - MS/GG/MD/CD/32X (PicoDrive)", "32x", "", 0x0080FF, false },
    { "segacd", "Sega - Mega-CD - Sega CD", MFR_SEGA, "genesis_plus_gx_libretro.so", "Sega - MS/GG/MD/CD (Genesis Plus GX)", "chd cue iso", "bios_CD_U.bin bios_CD_E.bin bios_CD_J.bin", 0x0080FF, false },
    { "sg1000", "Sega - SG-1000", MFR_SEGA, "genesis_plus_gx_libretro.so", "Sega - MS/GG/MD/CD (Genesis Plus GX)", "sg", "", 0x0080FF, false },
    { "saturn", "Sega - Saturn", MFR_SEGA, "mednafen_saturn_libretro.so", "Sega - Saturn (Beetle Saturn)", "chd cue iso", "saturn_bios.bin mpr-17933.bin sega_101.bin", 0x0080FF, false },

    // SNK (Gold / Red) - Accent 0xFFB703
    { "neogeo", "SNK - Neo Geo", MFR_SNK, "fbneo_libretro.so", "Arcade (FinalBurn Neo)", "zip", "neogeo.zip", 0xFFB703, false },
    { "neogeocd", "SNK - Neo Geo CD", MFR_SNK, "neocd_libretro.so", "SNK - Neo Geo CD (NeoCD)", "chd cue iso", "neocd/front-sp1.bin neocd/top-sp1.bin neocd/neocd.bin", 0xFFB703, false },
    { "neogeopocket", "SNK - Neo Geo Pocket", MFR_SNK, "mednafen_ngp_libretro.so", "SNK - Neo Geo Pocket / Color (Beetle NeoPop)", "ngp", "", 0xFFB703, false },
    { "neogeopocketcolor", "SNK - Neo Geo Pocket Color", MFR_SNK, "mednafen_ngp_libretro.so", "SNK - Neo Geo Pocket / Color (Beetle NeoPop)", "ngc", "", 0xFFB703, false },

    // Arcade (Neon Green) - Accent 0x39FF14
    { "atomiswave", "Arcade - Sammy Atomiswave", MFR_ARCADE, "flycast_libretro.so", "Sega - Dreamcast/NAOMI (Flycast)", "zip chd", "dc/awbios.zip", 0x39FF14, false },
    { "flycast", "Arcade - Sega NAOMI", MFR_ARCADE, "flycast_libretro.so", "Sega - Dreamcast/NAOMI (Flycast)", "zip chd", "dc/naomi.zip", 0x39FF14, false },
    { "model2", "Arcade - Sega Model 2", MFR_ARCADE, "fbneo_libretro.so", "Arcade (FinalBurn Neo)", "zip", "", 0x39FF14, false },
    { "supermodel", "Arcade - Sega Model 3", MFR_ARCADE, "fbneo_libretro.so", "Arcade (FinalBurn Neo)", "zip", "", 0x39FF14, false },

    // Classic / Microsoft / NEC / Bandai / Coleco / Mattel (Purple / Cyan) - Accent 0x9D4EDD
    { "msx", "Microsoft - MSX", MFR_OTHER, "bluemsx_libretro.so", "MSX/SVI/ColecoVision/SG-1000 (blueMSX)", "rom mx1 mx2 dsk", "Machines Databases", 0x9D4EDD, false },
    { "msx2", "Microsoft - MSX2", MFR_OTHER, "bluemsx_libretro.so", "MSX/SVI/ColecoVision/SG-1000 (blueMSX)", "rom mx1 mx2 dsk", "Machines Databases", 0x9D4EDD, false },
    { "msxturbor", "Microsoft - MSX TurboR", MFR_OTHER, "bluemsx_libretro.so", "MSX/SVI/ColecoVision/SG-1000 (blueMSX)", "rom mx1 mx2 dsk", "Machines Databases", 0x9D4EDD, false },
    { "turbografx", "NEC - PC Engine - TurboGrafx 16", MFR_OTHER, "mednafen_pce_fast_libretro.so", "NEC - PC Engine / CD (Beetle PCE FAST)", "pce chd cue", "", 0x9D4EDD, false },
    { "wonderswan", "Bandai - WonderSwan", MFR_OTHER, "mednafen_wswan_libretro.so", "Bandai - WonderSwan / Color (Beetle Cygne)", "ws wsc", "", 0x9D4EDD, false },
    { "colecovision", "Coleco - ColecoVision", MFR_OTHER, "bluemsx_libretro.so", "MSX/SVI/ColecoVision/SG-1000 (blueMSX)", "col", "Machines Databases", 0x9D4EDD, false },
    { "intellivision", "Mattel - Intellivision", MFR_OTHER, "freeintv_libretro.so", "Mattel - Intellivision (FreeIntv)", "int bin", "exec.bin grom.bin", 0x9D4EDD, false },
    { "odyssey2", "Magnavox - Odyssey2", MFR_OTHER, "o2em_libretro.so", "Magnavox - Odyssey2 / Phillips Videopac+ (O2EM)", "bin", "o2rom.bin", 0x9D4EDD, false },
    { "pcfx", "NEC - PC-FX", MFR_OTHER, "mednafen_pcfx_libretro.so", "NEC - PC-FX (Beetle PC-FX)", "chd cue", "pcfx.rom", 0x9D4EDD, false },

    // Atari (Atari Red / Gold) - Accent 0xD90429
    { "atari2600", "Atari - 2600", MFR_ATARI, "stella_libretro.so", "Atari - 2600 (Stella)", "a26", "", 0xD90429, false },
    { "atari5200", "Atari - 5200", MFR_ATARI, "a5200_libretro.so", "Atari - 5200 (a5200)", "a52", "5200.rom", 0xD90429, false },
    { "atari7800", "Atari - 7800", MFR_ATARI, "prosystem_libretro.so", "Atari - 7800 (ProSystem)", "a78", "", 0xD90429, false },
    { "atarilynx", "Atari - Lynx", MFR_ATARI, "handy_libretro.so", "Atari - Lynx (Handy)", "lnx", "lynxboot.img", 0xD90429, false },
    { "atarijaguar", "Atari - Jaguar", MFR_ATARI, "virtualjaguar_libretro.so", "Atari - Jaguar (Virtual Jaguar)", "j64", "", 0xD90429, false },
    { "atarijaguarcd", "Atari - Jaguar CD", MFR_ATARI, "virtualjaguar_libretro.so", "Atari - Jaguar (Virtual Jaguar)", "chd cue iso", "", 0xD90429, false }
};

const char* get_mfr_name(ManufacturerCategory mfr) {
    switch (mfr) {
        case MFR_ALL: return "ALL PLATFORMS";
        case MFR_NINTENDO: return "NINTENDO";
        case MFR_SONY: return "SONY";
        case MFR_SEGA: return "SEGA";
        case MFR_SNK: return "SNK";
        case MFR_ARCADE: return "ARCADE";
        case MFR_ATARI: return "ATARI";
        case MFR_OTHER: return "CLASSIC / COMPUTERS";
        default: return "UNKNOWN";
    }
}

unsigned int get_mfr_color(ManufacturerCategory mfr) {
    switch (mfr) {
        case MFR_ALL: return 0x4A4E69;
        case MFR_NINTENDO: return 0xE60012;
        case MFR_SONY: return 0x0050FF;
        case MFR_SEGA: return 0x0080FF;
        case MFR_SNK: return 0xFFB703;
        case MFR_ARCADE: return 0x39FF14;
        case MFR_ATARI: return 0xD90429;
        case MFR_OTHER: return 0x9D4EDD;
        default: return 0x888888;
    }
}

int get_platform_index_by_id(const char* id) {
    if (!id) return -1;
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (strcmp(g_platforms[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

void reset_all_selections(bool select_state) {
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        g_platforms[i].selected = select_state;
    }
}

void select_by_manufacturer(ManufacturerCategory mfr, bool select_state) {
    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (mfr == MFR_ALL || g_platforms[i].mfr == mfr) {
            g_platforms[i].selected = select_state;
        }
    }
}
