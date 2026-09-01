#include "ui.h"
#include "font.h"
#include "audio.h"
#include "theme.h"
#include "tasks.h"
#include "log.h"
#include "rom_catalog.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

UIManager g_ui;
static SDL_atomic_t g_catalog_load_state;
static char g_catalog_selection_file[MAX_PATH_LEN];

static void ui_update_filtered_games(void) {
    g_ui.game_filtered_count = 0;
    for (int i = 0; i < rom_catalog_count(); ++i) {
        RomCatalogGame* game = rom_catalog_get(i);
        if (!game) continue;
        if (g_ui.game_search_filter[0] && !strcasestr(game->name, g_ui.game_search_filter)) continue;
        g_ui.game_filtered_indices[g_ui.game_filtered_count++] = i;
    }
    if (g_ui.selected_game_index >= g_ui.game_filtered_count)
        g_ui.selected_game_index = g_ui.game_filtered_count > 0 ? g_ui.game_filtered_count - 1 : 0;
    if (g_ui.game_scroll_offset >= g_ui.game_filtered_count) g_ui.game_scroll_offset = 0;
}

// Main menu options with clean operational descriptions
typedef struct {
    TaskType task;
    const char* flag_name;
    const char* title;
    const char* description;
    ThemeColor color;
} MenuOption;

static const MenuOption g_menu_options[] = {
    { TASK_PREPARE,   "--prepare",   "PREPARE RETROARCH",           "Prepare RetroArch directories, core info, and databases",         { 46, 204, 113, 255 } },
    { TASK_NONE,      "--select",    "SELECT PLATFORMS",            "Choose platforms to install and configure",                        { 52, 152, 219, 255 } },
    { TASK_INSTALL,   "--install",   "INSTALL PLATFORMS & ASSETS",  "Download cores, BIOS, and ROMs for selected platforms",          { 230, 126,  34, 255 } },
    { TASK_UNINSTALL, "--uninstall", "UNINSTALL PLATFORMS",         "Remove ROMs, cores, playlists, and BIOS",                        { 231,  76,  60, 255 } },
    { TASK_THUMBNAILS,"--thumbnails", "DOWNLOAD THUMBNAILS",        "Download one boxart thumbnail for each installed ROM",             { 155,  89, 182, 255 } },
    { TASK_UPDATE_COVERS, "--update-covers", "UPDATE ALL COVERS",   "Refresh thumbnails/Steam covers for every already installed platform", { 41, 128, 185, 255 } },
    { TASK_UPDATE_CORES, "--update-cores", "UPDATE ALL CORES",     "Update every already installed core to the latest libretro build", { 22, 160, 133, 255 } },
    { TASK_IMPLODE,   "--implode",   "RESET CONFIGURATION",         "Reset local configuration and clear generated files",            { 192,  57,  43, 255 } },
    { TASK_STATUS,    "--status",    "SYSTEM STATUS",               "View system distribution, mode, and saved settings",              { 241, 196,  15, 255 } },
    { TASK_INSTALLATION_DIAGNOSTIC, "--diagnostic", "INSTALLATION DIAGNOSTIC", "Audit platform health, obsolete installs, and configured URLs", { 26, 188, 156, 255 } }
};

#define MENU_OPTION_COUNT (sizeof(g_menu_options)/sizeof(g_menu_options[0]))

static bool is_point_in_rect(int x, int y, SDL_Rect rect) {
    return (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h);
}

// Pixel art icons for main menu options
static void draw_option_icon(SDL_Renderer* r, TaskType task, int x, int y, int size, ThemeColor bg_color) {
    theme_draw_filled_rect(r, x, y, size, size, bg_color);
    theme_draw_border_rect(r, x, y, size, size, 2, COLOR_BORDER_ACCENT);

    int cx = x + size / 2;
    int cy = y + size / 2;
    ThemeColor white = { 255, 255, 255, 240 };

    switch (task) {
        case TASK_PREPARE: { // Gear / Wrench Icon 🛠️
            theme_draw_filled_rect(r, cx - 12, cy - 12, 24, 24, white);
            theme_draw_filled_rect(r, cx - 15, cy - 4, 30, 8, white);
            theme_draw_filled_rect(r, cx - 4, cy - 15, 8, 30, white);
            theme_draw_filled_rect(r, cx - 6, cy - 6, 12, 12, bg_color);
            theme_draw_filled_rect(r, cx - 2, cy - 2, 4, 4, white);
            break;
        }
        case TASK_NONE: { // Gamepad Controller Icon 🎮 (--select)
            theme_draw_filled_rect(r, cx - 16, cy - 8, 32, 16, white);
            theme_draw_filled_rect(r, cx - 18, cy - 4, 36, 10, white);
            theme_draw_filled_rect(r, cx - 16, cy + 6, 6, 5, white);
            theme_draw_filled_rect(r, cx + 10, cy + 6, 6, 5, white);
            // D-Pad Cross
            theme_draw_filled_rect(r, cx - 13, cy - 5, 4, 10, bg_color);
            theme_draw_filled_rect(r, cx - 16, cy - 2, 10, 4, bg_color);
            // Action Buttons
            theme_draw_filled_rect(r, cx + 9, cy - 4, 3, 3, bg_color);
            theme_draw_filled_rect(r, cx + 13, cy - 1, 3, 3, bg_color);
            theme_draw_filled_rect(r, cx + 5, cy - 1, 3, 3, bg_color);
            theme_draw_filled_rect(r, cx + 9, cy + 2, 3, 3, bg_color);
            break;
        }
        case TASK_INSTALL: { // Download Cartridge / Arrow Down 🚀
            theme_draw_filled_rect(r, cx - 14, cy - 12, 28, 24, white);
            theme_draw_filled_rect(r, cx - 10, cy - 9, 20, 12, bg_color);
            theme_draw_filled_rect(r, cx - 3, cy - 7, 6, 10, white);
            theme_draw_filled_rect(r, cx - 6, cy + 1, 12, 3, white);
            theme_draw_filled_rect(r, cx - 3, cy + 4, 6, 3, white);
            break;
        }
        case TASK_UNINSTALL: { // Trash Can Icon 🗑️
            theme_draw_filled_rect(r, cx - 5, cy - 14, 10, 3, white);
            theme_draw_filled_rect(r, cx - 14, cy - 11, 28, 3, white);
            theme_draw_filled_rect(r, cx - 10, cy - 7, 20, 20, white);
            theme_draw_filled_rect(r, cx - 7, cy - 3, 3, 12, bg_color);
            theme_draw_filled_rect(r, cx - 1, cy - 3, 3, 12, bg_color);
            theme_draw_filled_rect(r, cx + 5, cy - 3, 3, 12, bg_color);
            break;
        }
        case TASK_THUMBNAILS: { // Picture Frame / Boxart Icon 🖼️
            theme_draw_filled_rect(r, cx - 16, cy - 12, 32, 24, white);
            theme_draw_filled_rect(r, cx - 12, cy - 8, 24, 16, bg_color);
            theme_draw_filled_rect(r, cx + 3, cy - 5, 5, 5, white);
            theme_draw_filled_rect(r, cx - 10, cy + 3, 7, 5, white);
            theme_draw_filled_rect(r, cx - 5, cy - 1, 9, 9, white);
            theme_draw_filled_rect(r, cx + 2, cy + 1, 8, 7, white);
            break;
        }
        case TASK_UPDATE_COVERS: { // Refresh / Sync Arrows Icon 🔄
            theme_draw_filled_rect(r, cx - 15, cy - 15, 22, 4, white);
            theme_draw_filled_rect(r, cx + 3, cy - 15, 4, 10, white);
            theme_draw_filled_rect(r, cx - 3, cy - 19, 10, 4, white);
            theme_draw_filled_rect(r, cx - 7, cy + 11, 22, 4, white);
            theme_draw_filled_rect(r, cx - 7, cy + 5, 4, 10, white);
            theme_draw_filled_rect(r, cx - 7, cy + 15, 10, 4, white);
            break;
        }
        case TASK_UPDATE_CORES: { // Chip with Refresh Arrow Icon 🔄🧩
            // Chip body with pins
            theme_draw_filled_rect(r, cx - 10, cy - 10, 20, 20, white);
            theme_draw_filled_rect(r, cx - 6, cy - 6, 12, 12, bg_color);
            theme_draw_filled_rect(r, cx - 14, cy - 6, 4, 3, white);
            theme_draw_filled_rect(r, cx - 14, cy + 3, 4, 3, white);
            theme_draw_filled_rect(r, cx + 10, cy - 6, 4, 3, white);
            theme_draw_filled_rect(r, cx + 10, cy + 3, 4, 3, white);
            theme_draw_filled_rect(r, cx - 6, cy - 14, 3, 4, white);
            theme_draw_filled_rect(r, cx + 3, cy - 14, 3, 4, white);
            theme_draw_filled_rect(r, cx - 6, cy + 10, 3, 4, white);
            theme_draw_filled_rect(r, cx + 3, cy + 10, 3, 4, white);
            break;
        }
        case TASK_IMPLODE: { // Bomb Icon 💣
            theme_draw_filled_rect(r, cx + 3, cy - 16, 3, 3, white);
            theme_draw_filled_rect(r, cx + 1, cy - 12, 3, 3, white);
            theme_draw_filled_rect(r, cx - 10, cy - 7, 20, 20, white);
            theme_draw_filled_rect(r, cx - 12, cy - 4, 24, 14, white);
            theme_draw_filled_rect(r, cx - 7, cy - 10, 14, 24, white);
            theme_draw_filled_rect(r, cx - 7, cy - 4, 3, 3, bg_color);
            break;
        }
        case TASK_STATUS:
        case TASK_INSTALLATION_DIAGNOSTIC: { // Bar Chart Dashboard Icon 📊
            theme_draw_filled_rect(r, cx - 16, cy - 12, 32, 20, white);
            theme_draw_filled_rect(r, cx - 13, cy - 9, 26, 14, bg_color);
            theme_draw_filled_rect(r, cx - 3, cy + 8, 6, 4, white);
            theme_draw_filled_rect(r, cx - 8, cy + 12, 16, 3, white);
            theme_draw_filled_rect(r, cx - 10, cy - 2, 5, 7, white);
            theme_draw_filled_rect(r, cx - 3, cy - 5, 5, 10, white);
            theme_draw_filled_rect(r, cx + 4, cy - 8, 5, 13, white);
            break;
        }
        default:
            break;
    }
}

void ui_update_filtered_platforms(void) {
    g_ui.filtered_count = 0;

    for (int i = 0; i < TOTAL_PLATFORMS; i++) {
        if (g_ui.selected_mfr_tab > 0 && g_ui.selected_mfr_tab < g_category_count) {
            if (strcmp(g_platforms[i].category, g_categories[g_ui.selected_mfr_tab].name) != 0) {
                continue;
            }
        }

        if (g_ui.search_len > 0) {
            char lower_name[256], lower_id[64], lower_search[64];
            for (int k = 0; g_platforms[i].name[k] && k < 255; k++) {
                lower_name[k] = (g_platforms[i].name[k] >= 'A' && g_platforms[i].name[k] <= 'Z') ? g_platforms[i].name[k] + 32 : g_platforms[i].name[k];
                lower_name[k+1] = 0;
            }
            for (int k = 0; g_platforms[i].id[k] && k < 63; k++) {
                lower_id[k] = (g_platforms[i].id[k] >= 'A' && g_platforms[i].id[k] <= 'Z') ? g_platforms[i].id[k] + 32 : g_platforms[i].id[k];
                lower_id[k+1] = 0;
            }
            for (int k = 0; g_ui.search_filter[k] && k < 63; k++) {
                lower_search[k] = (g_ui.search_filter[k] >= 'A' && g_ui.search_filter[k] <= 'Z') ? g_ui.search_filter[k] + 32 : g_ui.search_filter[k];
                lower_search[k+1] = 0;
            }

            if (!strstr(lower_name, lower_search) && !strstr(lower_id, lower_search)) {
                continue;
            }
        }

        g_ui.filtered_indices[g_ui.filtered_count++] = i;
    }

    if (g_ui.selected_platform_index >= g_ui.filtered_count) {
        g_ui.selected_platform_index = (g_ui.filtered_count > 0) ? g_ui.filtered_count - 1 : 0;
    }
}

bool ui_init(const char* title, int width, int height) {
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.window_width = width;
    g_ui.window_height = height;
    g_ui.view = VIEW_MAIN_MENU;
    g_ui.selected_menu_index = 0;
    g_ui.selected_platform_index = 0;
    g_ui.selected_mfr_tab = 0;
    g_ui.running = true;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return false;
    }

    g_ui.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_ui.window) {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        return false;
    }

    g_ui.renderer = SDL_CreateRenderer(g_ui.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ui.renderer) {
        g_ui.renderer = SDL_CreateRenderer(g_ui.window, -1, 0);
    }

    font_init(g_ui.renderer);
    audio_init();
    tasks_init();

    for (int i = 0; i < 50; i++) {
        g_ui.particle_x[i] = rand() % width;
        g_ui.particle_y[i] = rand() % height;
        g_ui.particle_speed[i] = 1 + (rand() % 3);
    }

    ui_update_filtered_platforms();
    return true;
}

void ui_cleanup(void) {
    tasks_cleanup();
    audio_cleanup();
    font_cleanup();

    if (g_ui.renderer) SDL_DestroyRenderer(g_ui.renderer);
    if (g_ui.window) SDL_DestroyWindow(g_ui.window);
    SDL_Quit();
}

static void draw_background(void) {
    theme_draw_gradient_panel(g_ui.renderer, 0, 0, g_ui.window_width, g_ui.window_height, COLOR_BG_DARK, COLOR_BG_PANEL);

    g_ui.anim_timer += 0.016f;

    theme_set_draw_color(g_ui.renderer, (ThemeColor){ 100, 180, 255, 90 });
    for (int i = 0; i < 50; i++) {
        g_ui.particle_y[i] += g_ui.particle_speed[i];
        if (g_ui.particle_y[i] > g_ui.window_height) {
            g_ui.particle_y[i] = 0;
            g_ui.particle_x[i] = rand() % g_ui.window_width;
        }

        Uint8 alpha = 80 + (Uint8)(sinf(g_ui.anim_timer * 2.0f + i) * 60);
        SDL_SetRenderDrawColor(g_ui.renderer, 100, 180, 255, alpha);
        SDL_Rect p = { g_ui.particle_x[i], g_ui.particle_y[i], 2, 2 };
        SDL_RenderFillRect(g_ui.renderer, &p);
    }

    if (g_config.crt_scanlines) {
        SDL_SetRenderDrawColor(g_ui.renderer, 0, 0, 0, 30);
        for (int y = 0; y < g_ui.window_height; y += 3) {
            SDL_RenderDrawLine(g_ui.renderer, 0, y, g_ui.window_width, y);
        }
    }
}

static void draw_header_bar(void) {
    theme_draw_gradient_panel(g_ui.renderer, 0, 0, g_ui.window_width, HEADER_HEIGHT, COLOR_SURFACE, COLOR_BG_DARK);
    theme_draw_filled_rect(g_ui.renderer, 0, HEADER_HEIGHT - 2, g_ui.window_width, 2, COLOR_PRIMARY);

    // Title Logo
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER, 12, "RETRO SETUP", FONT_SCALE_TITLE, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);

    int logo_w;
    font_get_text_size("RETRO SETUP", FONT_SCALE_TITLE, &logo_w, NULL);
    font_draw_text(g_ui.renderer, MARGIN_CONTAINER + logo_w + 16, 20, "RetroArch Manager", FONT_SCALE_BODY, COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);

    // Mode Toggle Button (Responsive right-aligned using UIButton component)
    const char* mode_text = (g_config.mode == MODE_STEAM) ? "STEAM" : "STANDALONE";
    UIButton mode_btn;
    memset(&mode_btn, 0, sizeof(mode_btn));
    mode_btn.shortcut = "MODE:";
    mode_btn.label = mode_text;
    mode_btn.scale = FONT_SCALE_BODY;
    mode_btn.bg_color = (g_config.mode == MODE_STEAM) ? COLOR_SUCCESS : COLOR_SECONDARY;

    ui_button_measure(&mode_btn, 140, BUTTON_HEIGHT);

    int btn_x = g_ui.window_width - mode_btn.rect.w - MARGIN_CONTAINER;
    int btn_y = (HEADER_HEIGHT - mode_btn.rect.h) / 2;
    mode_btn.rect.x = btn_x;
    mode_btn.rect.y = btn_y;

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    mode_btn.hovered = ui_button_hit_test(&mode_btn, mx, my);

    ui_button_draw(g_ui.renderer, &mode_btn);
}

static void draw_footer_bar(void) {
    int y = g_ui.window_height - FOOTER_HEIGHT;
    theme_draw_filled_rect(g_ui.renderer, 0, y, g_ui.window_width, FOOTER_HEIGHT, COLOR_BG_DARK);
    theme_draw_filled_rect(g_ui.renderer, 0, y, g_ui.window_width, 2, COLOR_PRIMARY);

    const char* legend = "[ARROWS] Move   [ENTER] Select   [M] Mode   [Q] Quit";
    font_draw_text(g_ui.renderer, MARGIN_CONTAINER, y + 10, legend, FONT_SCALE_BODY, COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);

    // Audio status toggle button
    const char* audio_lbl = g_config.audio_enabled ? "[AUDIO: ON]" : "[AUDIO: OFF]";
    int audio_w;
    font_get_text_size(audio_lbl, FONT_SCALE_BODY, &audio_w, NULL);
    int audio_x = g_ui.window_width - audio_w - MARGIN_CONTAINER;

    ThemeColor audio_color = g_config.audio_enabled ? COLOR_SUCCESS : COLOR_TEXT_MUTED;
    font_draw_text(g_ui.renderer, audio_x, y + 10, audio_lbl, FONT_SCALE_BODY, audio_color.r, audio_color.g, audio_color.b, 255);
}

static void draw_main_menu(void) {
    draw_background();
    draw_header_bar();

    int start_y = HEADER_HEIGHT + 14;
    int card_w = g_ui.window_width - 2 * MARGIN_CONTAINER;
    int card_h = CARD_HEIGHT;
    int card_spacing = GAP_SPACING;

    int selected_cnt = get_selected_count();
    char subtitle[512];
    snprintf(subtitle, sizeof(subtitle), "Platforms: %d / %d Selected | OS: %s",
             selected_cnt, TOTAL_PLATFORMS, g_config.distro_name);

    int max_sub_w = g_ui.window_width - 2 * MARGIN_CONTAINER;
    font_draw_text_shadow_truncated(g_ui.renderer, MARGIN_CONTAINER, start_y, subtitle, FONT_SCALE_BODY, max_sub_w, COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    start_y += 26;

    int mx, my;
    SDL_GetMouseState(&mx, &my);

    for (size_t i = 0; i < MENU_OPTION_COUNT; i++) {
        int y = start_y + i * (card_h + card_spacing);
        SDL_Rect rect = { MARGIN_CONTAINER, y, card_w, card_h };

        bool is_keyboard_focused = ((int)i == g_ui.selected_menu_index);
        bool is_mouse_hovered = is_point_in_rect(mx, my, rect);
        bool active = is_keyboard_focused || is_mouse_hovered;

        ThemeColor rcolor = g_menu_options[i].color;

        if (active) {
            theme_draw_gradient_panel(g_ui.renderer, rect.x, rect.y, rect.w, rect.h, COLOR_SURFACE_SELECTED, COLOR_SURFACE);
            theme_draw_border_rect(g_ui.renderer, rect.x, rect.y, rect.w, rect.h, 2, COLOR_BORDER_FOCUS);

            // Refined left accent bar (4px)
            theme_draw_filled_rect(g_ui.renderer, rect.x, rect.y, 4, rect.h, COLOR_PRIMARY);
        } else {
            theme_draw_gradient_panel(g_ui.renderer, rect.x, rect.y, rect.w, rect.h, COLOR_BG_PANEL, COLOR_SURFACE);
            theme_draw_border_rect(g_ui.renderer, rect.x, rect.y, rect.w, rect.h, 1, COLOR_BORDER_DEFAULT);
        }

        // Custom Pixel-Art Icon Box (44x44 px)
        draw_option_icon(g_ui.renderer, g_menu_options[i].task, rect.x + 10, rect.y + (card_h - ICON_SIZE) / 2, ICON_SIZE, rcolor);

        // Title
        int text_x = rect.x + 66;
        int max_title_w = rect.w - 110;
        font_draw_text_shadow_truncated(g_ui.renderer, text_x, rect.y + 10, g_menu_options[i].title, FONT_SCALE_HEADING, max_title_w,
                                        active ? COLOR_TEXT_PRIMARY.r : COLOR_TEXT_SECONDARY.r,
                                        active ? COLOR_TEXT_PRIMARY.g : COLOR_TEXT_SECONDARY.g,
                                        active ? COLOR_TEXT_PRIMARY.b : COLOR_TEXT_SECONDARY.b, 255);

        // Description
        font_draw_text_truncated(g_ui.renderer, text_x, rect.y + 36, g_menu_options[i].description, FONT_SCALE_BODY, max_title_w,
                                 COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);

        // Right arrow indicator for active card
        if (active) {
            font_draw_text_shadow(g_ui.renderer, rect.x + rect.w - 36, rect.y + (card_h - 16) / 2, ">>", FONT_SCALE_BODY, rcolor.r, rcolor.g, rcolor.b, 255);
        }
    }

    draw_footer_bar();
}

static void draw_platform_selector(void) {
    draw_background();
    draw_header_bar();

    int start_y = HEADER_HEIGHT + 10;

    // Manufacturer Filter Tabs
    int tab_x = MARGIN_CONTAINER;
    int tab_h = TAB_HEIGHT;
    int mx, my;
    SDL_GetMouseState(&mx, &my);

    for (int c = 0; c < g_category_count; c++) {
        const char* cname = g_categories[c].name;
        UIButton tab_btn;
        memset(&tab_btn, 0, sizeof(tab_btn));
        tab_btn.label = cname;
        tab_btn.scale = FONT_SCALE_BODY;

        ui_button_measure(&tab_btn, 60, tab_h);

        tab_btn.rect.x = tab_x;
        tab_btn.rect.y = start_y;

        bool active = (g_ui.selected_mfr_tab == c);
        unsigned int ccolor = g_categories[c].color_hex;
        ThemeColor ctheme = { (Uint8)((ccolor >> 16) & 0xFF), (Uint8)((ccolor >> 8) & 0xFF), (Uint8)(ccolor & 0xFF), 230 };

        tab_btn.bg_color = active ? ctheme : COLOR_SURFACE;
        tab_btn.hovered = ui_button_hit_test(&tab_btn, mx, my);
        tab_btn.focused = active;

        ui_button_draw(g_ui.renderer, &tab_btn);

        tab_x += tab_btn.rect.w + 6;
    }

    start_y += tab_h + 10;

    // Search bar + Quick Selection Action buttons
    int search_w = 340;
    theme_draw_filled_rect(g_ui.renderer, MARGIN_CONTAINER, start_y, search_w, BUTTON_HEIGHT, COLOR_SURFACE);
    theme_draw_border_rect(g_ui.renderer, MARGIN_CONTAINER, start_y, search_w, BUTTON_HEIGHT, g_ui.search_active ? 2 : 1,
                           g_ui.search_active ? COLOR_BORDER_FOCUS : COLOR_BORDER_DEFAULT);

    char search_prompt[128];
    snprintf(search_prompt, sizeof(search_prompt), "SEARCH: %s%s", g_ui.search_filter, (g_ui.search_active ? "_" : ""));
    font_draw_text_truncated(g_ui.renderer, MARGIN_CONTAINER + 10, start_y + 9, search_prompt, FONT_SCALE_BODY, search_w - 20,
                             g_ui.search_active ? COLOR_TEXT_PRIMARY.r : COLOR_TEXT_SECONDARY.r,
                             g_ui.search_active ? COLOR_TEXT_PRIMARY.g : COLOR_TEXT_SECONDARY.g,
                             g_ui.search_active ? COLOR_TEXT_PRIMARY.b : COLOR_TEXT_SECONDARY.b, 255);

    // Quick action buttons using UIButton component
    int act_x = MARGIN_CONTAINER + search_w + 12;
    const struct { const char* sc; const char* lbl; } actions[] = {
        { "[A]", "ALL" },
        { "[C]", "CLEAR" },
        { "[I]", "INVERT" },
        { "[ENTER]", "SAVE & RUN" }
    };

    for (int a = 0; a < 4; a++) {
        UIButton abtn;
        memset(&abtn, 0, sizeof(abtn));
        abtn.shortcut = actions[a].sc;
        abtn.label = actions[a].lbl;
        abtn.scale = FONT_SCALE_BODY;
        abtn.bg_color = (a == 3) ? COLOR_SUCCESS : COLOR_BG_PANEL;

        ui_button_measure(&abtn, 0, BUTTON_HEIGHT);

        abtn.rect.x = act_x;
        abtn.rect.y = start_y;
        abtn.hovered = ui_button_hit_test(&abtn, mx, my);

        ui_button_draw(g_ui.renderer, &abtn);

        act_x += abtn.rect.w + 8;
    }

    start_y += 44;

    // Selected counter bar
    char count_str[128];
    snprintf(count_str, sizeof(count_str), "Platforms: %d Filtered | %d Selected Total", g_ui.filtered_count, get_selected_count());
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER, start_y, count_str, FONT_SCALE_BODY, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);

    start_y += 22;

    // Platform Scrollable List
    int list_y = start_y;
    int list_h = g_ui.window_height - list_y - FOOTER_HEIGHT - 6;
    int item_h = PLATFORM_ITEM_HEIGHT;
    int visible_items = list_h / item_h;

    if (g_ui.selected_platform_index < g_ui.scroll_offset) {
        g_ui.scroll_offset = g_ui.selected_platform_index;
    }
    if (g_ui.selected_platform_index >= g_ui.scroll_offset + visible_items) {
        g_ui.scroll_offset = g_ui.selected_platform_index - visible_items + 1;
    }
    if (g_ui.scroll_offset < 0) g_ui.scroll_offset = 0;

    for (int v = 0; v < visible_items; v++) {
        int idx_in_filtered = g_ui.scroll_offset + v;
        if (idx_in_filtered >= g_ui.filtered_count) break;

        int p_idx = g_ui.filtered_indices[idx_in_filtered];
        PlatformInfo* platform = &g_platforms[p_idx];

        int iy = list_y + v * item_h;
        bool is_selected_cursor = (idx_in_filtered == g_ui.selected_platform_index);

        SDL_Rect item_rect = { MARGIN_CONTAINER, iy, g_ui.window_width - 2 * MARGIN_CONTAINER, item_h - 4 };
        bool is_mouse_hover = is_point_in_rect(mx, my, item_rect);
        bool active_item = is_selected_cursor || is_mouse_hover;

        if (active_item) {
            theme_draw_gradient_panel(g_ui.renderer, item_rect.x, item_rect.y, item_rect.w, item_rect.h, COLOR_SURFACE_SELECTED, COLOR_SURFACE);
            theme_draw_border_rect(g_ui.renderer, item_rect.x, item_rect.y, item_rect.w, item_rect.h, 2, COLOR_BORDER_FOCUS);
        } else {
            theme_draw_gradient_panel(g_ui.renderer, item_rect.x, item_rect.y, item_rect.w, item_rect.h, COLOR_BG_PANEL, COLOR_SURFACE);
            theme_draw_border_rect(g_ui.renderer, item_rect.x, item_rect.y, item_rect.w, item_rect.h, 1, COLOR_BORDER_DEFAULT);
        }

        // Checkbox [X] or [ ]
        const char* chk = platform->selected ? "[X]" : "[ ]";
        ThemeColor chk_color = platform->selected ? COLOR_SUCCESS : COLOR_TEXT_MUTED;
        font_draw_text_shadow(g_ui.renderer, item_rect.x + 10, iy + 10, chk, FONT_SCALE_BODY, chk_color.r, chk_color.g, chk_color.b, 255);

        // Category Pill Badge
        unsigned int pcolor = platform->color_hex;
        ThemeColor ptheme = { (Uint8)((pcolor >> 16) & 0xFF), (Uint8)((pcolor >> 8) & 0xFF), (Uint8)(pcolor & 0xFF), 230 };

        int pw = (int)strlen(platform->id) * 16 + 16;
        theme_draw_filled_rect(g_ui.renderer, item_rect.x + 65, iy + 5, pw, 26, ptheme);
        font_draw_text_shadow(g_ui.renderer, item_rect.x + 73, iy + 10, platform->id, FONT_SCALE_BODY, COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);

        // Platform Full Name
        int name_x = item_rect.x + 65 + pw + 14;
        int info_w = 320;
        int name_max_w = item_rect.w - (name_x - item_rect.x) - info_w - 10;
        if (name_max_w < 100) name_max_w = 100;

        font_draw_text_shadow_truncated(g_ui.renderer, name_x, iy + 10, platform->name, FONT_SCALE_BODY, name_max_w,
                                        active_item ? COLOR_TEXT_PRIMARY.r : COLOR_TEXT_SECONDARY.r,
                                        active_item ? COLOR_TEXT_PRIMARY.g : COLOR_TEXT_SECONDARY.g,
                                        active_item ? COLOR_TEXT_PRIMARY.b : COLOR_TEXT_SECONDARY.b, 255);

        // Core / BIOS info
        char extra[128];
        snprintf(extra, sizeof(extra), "%s %s", platform->core_file, (platform->bios_files[0] ? "(BIOS)" : ""));
        int info_x = item_rect.x + item_rect.w - info_w;
        font_draw_text_truncated(g_ui.renderer, info_x, iy + 10, extra, FONT_SCALE_BODY, info_w - 10,
                                 COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    }

    draw_footer_bar();
}

static void format_transfer_size(curl_off_t bytes, char* out, size_t out_size);

static void draw_game_selector(void) {
    draw_background();
    draw_header_bar();
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    int y = HEADER_HEIGHT + 18;
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER, y, "SELECT INDIVIDUAL GAMES", FONT_SCALE_TITLE,
                          COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);
    y += 36;
    int selected = 0;
    for (int i = 0; i < rom_catalog_count(); ++i) {
        RomCatalogGame* game = rom_catalog_get(i);
        if (game && game->selected) selected++;
    }
    char summary[160];
    snprintf(summary, sizeof(summary), "%d shown / %d available | %d selected",
             g_ui.game_filtered_count, rom_catalog_count(), selected);
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER, y, summary, FONT_SCALE_BODY,
                          COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    y += 24;

    const int search_width = 430;
    SDL_Rect search_rect = { MARGIN_CONTAINER, y, search_width, BUTTON_HEIGHT };
    theme_draw_filled_rect(g_ui.renderer, search_rect.x, search_rect.y, search_rect.w, search_rect.h, COLOR_SURFACE);
    theme_draw_border_rect(g_ui.renderer, search_rect.x, search_rect.y, search_rect.w, search_rect.h,
                           g_ui.game_search_active ? 2 : 1,
                           g_ui.game_search_active ? COLOR_BORDER_FOCUS : COLOR_BORDER_DEFAULT);
    char search_text[180];
    snprintf(search_text, sizeof(search_text), "[/] SEARCH: %s%s", g_ui.game_search_filter,
             g_ui.game_search_active ? "_" : "");
    font_draw_text_truncated(g_ui.renderer, search_rect.x + 10, search_rect.y + 9, search_text,
                             FONT_SCALE_BODY, search_rect.w - 20,
                             COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);

    int button_x = search_rect.x + search_rect.w + 12;
    const struct { const char* shortcut; const char* label; ThemeColor color; } actions[] = {
        { "[A]", "ALL SHOWN", COLOR_SECONDARY },
        { "[C]", "CLEAR SHOWN", COLOR_BG_PANEL },
        { "[ENTER]", "SAVE & INSTALL", COLOR_SUCCESS }
    };
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        UIButton button;
        memset(&button, 0, sizeof(button));
        button.shortcut = actions[i].shortcut;
        button.label = actions[i].label;
        button.scale = FONT_SCALE_BODY;
        button.bg_color = actions[i].color;
        ui_button_measure(&button, 0, BUTTON_HEIGHT);
        button.rect.x = button_x;
        button.rect.y = y;
        button.hovered = ui_button_hit_test(&button, mx, my);
        ui_button_draw(g_ui.renderer, &button);
        button_x += button.rect.w + 8;
    }
    y += BUTTON_HEIGHT + 10;

    int visible = (g_ui.window_height - y - FOOTER_HEIGHT - 6) / PLATFORM_ITEM_HEIGHT;
    if (visible < 1) visible = 1;
    if (g_ui.selected_game_index < g_ui.game_scroll_offset) g_ui.game_scroll_offset = g_ui.selected_game_index;
    if (g_ui.selected_game_index >= g_ui.game_scroll_offset + visible)
        g_ui.game_scroll_offset = g_ui.selected_game_index - visible + 1;

    for (int row = 0; row < visible; ++row) {
        int filtered_index = g_ui.game_scroll_offset + row;
        if (filtered_index >= g_ui.game_filtered_count) break;
        int catalog_index = g_ui.game_filtered_indices[filtered_index];
        RomCatalogGame* game = rom_catalog_get(catalog_index);
        if (!game) break;
        SDL_Rect rect = { MARGIN_CONTAINER, y + row * PLATFORM_ITEM_HEIGHT,
                          g_ui.window_width - 2 * MARGIN_CONTAINER, PLATFORM_ITEM_HEIGHT - 4 };
        bool active = filtered_index == g_ui.selected_game_index || is_point_in_rect(mx, my, rect);
        theme_draw_gradient_panel(g_ui.renderer, rect.x, rect.y, rect.w, rect.h,
                                  active ? COLOR_SURFACE_SELECTED : COLOR_BG_PANEL, COLOR_SURFACE);
        theme_draw_border_rect(g_ui.renderer, rect.x, rect.y, rect.w, rect.h, active ? 2 : 1,
                               active ? COLOR_BORDER_FOCUS : COLOR_BORDER_DEFAULT);
        ThemeColor check = game->selected ? COLOR_SUCCESS : COLOR_TEXT_MUTED;
        font_draw_text_shadow(g_ui.renderer, rect.x + 10, rect.y + 10, game->selected ? "[X]" : "[ ]",
                              FONT_SCALE_BODY, check.r, check.g, check.b, 255);
        char size_text[32];
        format_transfer_size((curl_off_t)game->size, size_text, sizeof(size_text));
        font_draw_text_shadow_truncated(g_ui.renderer, rect.x + 62, rect.y + 10, game->name,
                                        FONT_SCALE_BODY, rect.w - 210,
                                        COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
        font_draw_text_shadow(g_ui.renderer, rect.x + rect.w - 130, rect.y + 10, size_text, FONT_SCALE_BODY,
                              COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    }
    draw_footer_bar();
}

static void draw_catalog_loading(void) {
    draw_background();
    draw_header_bar();
    int panel_width = g_ui.window_width - 2 * MARGIN_CONTAINER;
    int panel_height = 180;
    int panel_y = (g_ui.window_height - panel_height) / 2;
    theme_draw_gradient_panel(g_ui.renderer, MARGIN_CONTAINER, panel_y, panel_width, panel_height,
                              COLOR_BG_PANEL, COLOR_SURFACE);
    theme_draw_border_rect(g_ui.renderer, MARGIN_CONTAINER, panel_y, panel_width, panel_height, 2, COLOR_PRIMARY);
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER + 24, panel_y + 28,
                          "LOADING GAME CATALOGS", FONT_SCALE_TITLE,
                          COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER + 24, panel_y + 76,
                          "Reading cached metadata and updating game catalogs...",
                          FONT_SCALE_BODY, COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g,
                          COLOR_TEXT_SECONDARY.b, 255);
    int track_x = MARGIN_CONTAINER + 24;
    int track_y = panel_y + 120;
    int track_w = panel_width - 48;
    theme_draw_filled_rect(g_ui.renderer, track_x, track_y, track_w, 14, COLOR_SURFACE_SELECTED);
    int marker_w = track_w / 5;
    int travel = track_w - marker_w;
    int marker_x = track_x + (int)(fabs(sin(g_ui.anim_timer * 2.0f)) * travel);
    theme_draw_filled_rect(g_ui.renderer, marker_x, track_y, marker_w, 14, COLOR_PRIMARY);
    draw_footer_bar();
}

static void format_transfer_size(curl_off_t bytes, char* out, size_t out_size) {
    if (bytes < 0) snprintf(out, out_size, "?");
    else if (bytes >= 1073741824) snprintf(out, out_size, "%.1f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1048576) snprintf(out, out_size, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024) snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    else snprintf(out, out_size, "%lld B", (long long)bytes);
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Display-only cleanup: keep the transfer destination untouched. */
static void format_download_name(const DownloadTask* task, char* out, size_t out_size) {
    const char* name = task && task->filename[0] ? task->filename : "download";
    const char* slash = strrchr(name, '/');
    if (slash && slash[1]) name = slash + 1;
    size_t written = 0;
    while (*name && *name != '?' && *name != '#' && written + 1 < out_size) {
        if (name[0] == '%' && name[1] && name[2]) {
            int hi = hex_digit_value(name[1]);
            int lo = hex_digit_value(name[2]);
            if (hi >= 0 && lo >= 0) {
                out[written++] = (char)((hi << 4) | lo);
                name += 3;
                continue;
            }
        }
        out[written++] = *name++;
    }
    out[written] = 0;
}

static void draw_progress_bar(int x, int y, int w, int h, float progress, ThemeColor color) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    theme_draw_filled_rect(g_ui.renderer, x, y, w, h, COLOR_SURFACE);
    theme_draw_border_rect(g_ui.renderer, x, y, w, h, 1, COLOR_BORDER_DEFAULT);
    int fill = (int)((w - 2) * progress);
    if (fill > 0) theme_draw_filled_rect(g_ui.renderer, x + 1, y + 1, fill, h - 2, color);
}

static int log_visual_line_count(void) {
    int count = 0;
    for (int i = 0; i < log_get_count(); ++i) {
        const char* text = log_get_line(i);
        count++;
        for (; *text; ++text) if (*text == '\n') count++;
    }
    return count;
}

static int draw_log_visual_lines(SDL_Rect region, int scroll_from_bottom) {
    const int line_height = 18;
    int capacity = (region.h - 12) / line_height;
    if (capacity < 1) return 0;
    int total = log_visual_line_count();
    int max_scroll = total > capacity ? total - capacity : 0;
    if (scroll_from_bottom > max_scroll) scroll_from_bottom = max_scroll;
    if (scroll_from_bottom < 0) scroll_from_bottom = 0;
    int skip = total - capacity - scroll_from_bottom;
    if (skip < 0) skip = 0;
    int visual_index = 0;
    int drawn = 0;
    SDL_RenderSetClipRect(g_ui.renderer, &region);
    for (int i = 0; i < log_get_count() && drawn < capacity; ++i) {
        const char* cursor = log_get_line(i);
        ThemeColor color = COLOR_TEXT_SECONDARY;
        if (strstr(cursor, "ERROR") || strstr(cursor, "FAILED")) color = COLOR_ERROR;
        else if (strstr(cursor, "WARNING") || strstr(cursor, "WARN")) color = COLOR_WARNING;
        else if (strstr(cursor, "=== ")) color = COLOR_SUCCESS;
        do {
            const char* newline = strchr(cursor, '\n');
            size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
            if (visual_index++ >= skip) {
                char line[LOG_LINE_LEN];
                if (length >= sizeof(line)) length = sizeof(line) - 1;
                memcpy(line, cursor, length);
                line[length] = 0;
                font_draw_text_truncated(g_ui.renderer, region.x + 10,
                                         region.y + 7 + drawn * line_height, line,
                                         FONT_SCALE_BODY, region.w - 20,
                                         color.r, color.g, color.b, 255);
                drawn++;
            }
            cursor = newline ? newline + 1 : NULL;
        } while (cursor && drawn < capacity);
    }
    SDL_RenderSetClipRect(g_ui.renderer, NULL);
    return max_scroll;
}

static void draw_task_modal(void) {
    theme_draw_filled_rect(g_ui.renderer, 0, 0, g_ui.window_width, g_ui.window_height, (ThemeColor){ 0, 0, 0, 210 });

    int modal_w = g_ui.window_width - 80;
    int modal_h = g_ui.window_height - 100;
    int modal_x = 40;
    int modal_y = 50;

    theme_draw_gradient_panel(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, COLOR_BG_PANEL, COLOR_BG_DARK);
    theme_draw_border_rect(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, 3, COLOR_PRIMARY);

    int pad = 20;
    int content_x = modal_x + pad;
    int content_w = modal_w - pad * 2;
    font_draw_text_shadow(g_ui.renderer, content_x, modal_y + 16, task_get_title(g_ui.modal_task), FONT_SCALE_TITLE, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);

    DownloadManagerSnapshot downloads;
    task_get_download_snapshot(&downloads);
    int work_done = 0, work_total = 0;
    task_get_work_counts(&work_done, &work_total);
    float byte_progress = downloads.total_bytes > 0 ? (float)((double)downloads.downloaded_bytes / (double)downloads.total_bytes) : 0.0f;
    float stage_progress = (g_ui.modal_task == TASK_INSTALL || g_ui.modal_task == TASK_UNINSTALL)
                         ? (work_total > 0 ? (float)work_done / (float)work_total : 0.0f)
                         : task_get_progress();
    float progress = (g_ui.modal_task == TASK_THUMBNAILS || g_ui.modal_task == TASK_UPDATE_COVERS || downloads.total_bytes <= 0)
                   ? stage_progress
                   : byte_progress * 0.85f + stage_progress * 0.15f;
    if (!task_is_finished() && progress >= 1.0f) progress = 0.99f;
    if (task_is_finished() && task_get_exit_code() == 0) progress = 1.0f;
    if (task_is_finished() && task_get_exit_code() != 0 && progress >= 1.0f) progress = 0.99f;
    char got[32], expected[32], speed[32], overall[256];
    format_transfer_size(downloads.downloaded_bytes, got, sizeof(got));
    format_transfer_size(downloads.total_bytes > 0 ? downloads.total_bytes : -1, expected, sizeof(expected));
    format_transfer_size((curl_off_t)downloads.speed_bytes_per_sec, speed, sizeof(speed));
    int col1_x = content_x;
    int col2_x = content_x + content_w * 23 / 100;
    int col3_x = content_x + content_w * 43 / 100;
    snprintf(overall, sizeof(overall), "Overall: %d%%", (int)(progress * 100.0f));
    font_draw_text(g_ui.renderer, col1_x, modal_y + 47, overall, FONT_SCALE_BODY,
                   COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
    snprintf(overall, sizeof(overall), "Tasks: %d/%d", work_done, work_total);
    font_draw_text(g_ui.renderer, col2_x, modal_y + 47, overall, FONT_SCALE_BODY,
                   COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
    snprintf(overall, sizeof(overall), "Downloaded: %s / %s", got, expected);
    font_draw_text_truncated(g_ui.renderer, col3_x, modal_y + 47, overall, FONT_SCALE_BODY,
                             content_x + content_w - col3_x, COLOR_TEXT_PRIMARY.r,
                             COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);

    snprintf(overall, sizeof(overall), "Speed: %s/s", speed);
    font_draw_text(g_ui.renderer, col1_x, modal_y + 67, overall, FONT_SCALE_BODY,
                   COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    snprintf(overall, sizeof(overall), "Active: %d", downloads.active_count);
    font_draw_text(g_ui.renderer, col2_x, modal_y + 67, overall, FONT_SCALE_BODY,
                   COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    snprintf(overall, sizeof(overall), "Queued: %d", downloads.queued_count);
    font_draw_text(g_ui.renderer, col3_x, modal_y + 67, overall, FONT_SCALE_BODY,
                   COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);

    int bar_y = modal_y + 88;
    draw_progress_bar(content_x, bar_y, content_w, 16, progress, COLOR_SUCCESS);
    char pct[20]; snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100.0f));
    font_draw_text_shadow(g_ui.renderer, content_x + content_w / 2 - 16, bar_y + 1, pct, FONT_SCALE_BODY,
                          COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);

    int active_indices[MAX_PARALLEL_DOWNLOADS];
    int shown = 0;
    for (int i = 0; i < downloads.task_count && shown < MAX_PARALLEL_DOWNLOADS; ++i) {
        DownloadTask* t = &downloads.tasks[i];
        if (t->state != DOWNLOAD_DOWNLOADING && t->state != DOWNLOAD_RESUMING && t->state != DOWNLOAD_PAUSED &&
            t->state != DOWNLOAD_CHECKING) continue;
        active_indices[shown++] = i;
    }

    const int section_gap = 10;
    int dynamic_y = bar_y + 16 + section_gap;
    int dynamic_h = shown > 0 ? 19 + shown * 40 : 48;
    int actions_y = modal_y + modal_h - 50;
    int max_dynamic_h = actions_y - dynamic_y - section_gap * 2 - 18;
    if (dynamic_h > max_dynamic_h) dynamic_h = max_dynamic_h;
    if (dynamic_h < 24) dynamic_h = 24;
    SDL_Rect dynamic_region = {content_x, dynamic_y, content_w, dynamic_h};
    theme_draw_filled_rect(g_ui.renderer, dynamic_region.x, dynamic_region.y,
                           dynamic_region.w, dynamic_region.h, COLOR_BG_PANEL);
    SDL_RenderSetClipRect(g_ui.renderer, &dynamic_region);
    font_draw_text_shadow(g_ui.renderer, content_x, dynamic_y,
                          shown > 0 ? "ACTIVE DOWNLOADS" : "STATUS", FONT_SCALE_BODY,
                          COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);
    int rows_y = dynamic_y + 19;
    for (int shown_index = 0; shown_index < shown; ++shown_index) {
        DownloadTask* t = &downloads.tasks[active_indices[shown_index]];
        int row_h = 35;
        int card_y = rows_y + shown_index * (row_h + 5);
        char friendly_name[256], left[300], now[32], total[32], rate[32], right[360];
        format_download_name(t, friendly_name, sizeof(friendly_name));
        snprintf(left, sizeof(left), "[%d] %s%s", shown_index + 1, friendly_name,
                 t->state == DOWNLOAD_PAUSED ? " [PAUSED]" : "");
        format_transfer_size(t->downloaded_bytes, now, sizeof(now));
        format_transfer_size(t->remote_size, total, sizeof(total));
        format_transfer_size((curl_off_t)t->speed_bytes_per_sec, rate, sizeof(rate));
        int item_pct = t->remote_size > 0 ? (int)(100.0 * (double)t->downloaded_bytes / (double)t->remote_size) : 0;
        int eta = t->eta_seconds > 0 ? (int)t->eta_seconds : 0;
        snprintf(right, sizeof(right), "%s/%s  %d%%  %s/s  ETA %02d:%02d", now, total, item_pct, rate, eta / 60, eta % 60);
        int measured_details_w = 0;
        font_get_text_size(right, FONT_SCALE_BODY, &measured_details_w, NULL);
        int details_w = measured_details_w;
        if (details_w > content_w * 64 / 100) details_w = content_w * 64 / 100;
        int name_w = content_w - details_w - 12;
        font_draw_text_truncated(g_ui.renderer, content_x, card_y + 2, left, FONT_SCALE_BODY, name_w,
                                 COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
        font_draw_text_truncated(g_ui.renderer, content_x + name_w + 12, card_y + 2, right, FONT_SCALE_BODY, details_w,
                                 COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
        draw_progress_bar(content_x, card_y + 22, content_w, 7,
                          t->remote_size > 0 ? (float)((double)t->downloaded_bytes / (double)t->remote_size) : 0.0f,
                          t->state == DOWNLOAD_PAUSED ? COLOR_WARNING : COLOR_SECONDARY);
    }
    if (shown == 0) {
        font_draw_text_truncated(g_ui.renderer, content_x, rows_y + 3, task_get_status_message(), FONT_SCALE_BODY, content_w,
                                 COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255);
    }
    SDL_RenderSetClipRect(g_ui.renderer, NULL);

    /* The log owns all space between the dynamic region and the action footer. */
    int term_x = content_x;
    int term_y = dynamic_region.y + dynamic_region.h + section_gap;
    int term_w = content_w;
    int term_h = actions_y - section_gap - term_y;
    if (term_h < 18) term_h = 18;

    theme_draw_filled_rect(g_ui.renderer, term_x, term_y, term_w, term_h, (ThemeColor){ 10, 12, 18, 255 });
    theme_draw_border_rect(g_ui.renderer, term_x, term_y, term_w, term_h, 1, COLOR_BORDER_DEFAULT);
    int max_log_scroll = draw_log_visual_lines((SDL_Rect){term_x, term_y, term_w, term_h}, g_ui.task_log_scroll_lines);
    if (g_ui.task_log_scroll_lines > max_log_scroll) g_ui.task_log_scroll_lines = max_log_scroll;
    if (max_log_scroll > 0) {
        char position[64];
        snprintf(position, sizeof(position), "LOG %d/%d", max_log_scroll - g_ui.task_log_scroll_lines + 1, max_log_scroll + 1);
        int position_w = 0;
        font_get_text_size(position, FONT_SCALE_BODY, &position_w, NULL);
        theme_draw_filled_rect(g_ui.renderer, term_x + term_w - position_w - 14, term_y + 2, position_w + 10, 18, (ThemeColor){10, 12, 18, 255});
        font_draw_text(g_ui.renderer, term_x + term_w - position_w - 9, term_y + 3, position, FONT_SCALE_BODY,
                       COLOR_TEXT_MUTED.r, COLOR_TEXT_MUTED.g, COLOR_TEXT_MUTED.b, 255);
        int track_x = term_x + term_w - 5;
        int track_y = term_y + 22;
        int track_h = term_h - 26;
        if (track_h > 8) {
            theme_draw_filled_rect(g_ui.renderer, track_x, track_y, 3, track_h, COLOR_SURFACE);
            int total_lines = log_visual_line_count();
            int visible_lines = (term_h - 12) / 18;
            int thumb_h = total_lines > 0 ? track_h * visible_lines / total_lines : track_h;
            if (thumb_h < 8) thumb_h = 8;
            if (thumb_h > track_h) thumb_h = track_h;
            int travel = track_h - thumb_h;
            int from_top = max_log_scroll - g_ui.task_log_scroll_lines;
            int thumb_y = track_y + (max_log_scroll > 0 ? travel * from_top / max_log_scroll : 0);
            theme_draw_filled_rect(g_ui.renderer, track_x, thumb_y, 3, thumb_h, COLOR_PRIMARY);
        }
    }

    // Action Buttons (Pause / Resume / Cancel / Close)
    int mx, my;
    SDL_GetMouseState(&mx, &my);

    if (task_is_finished()) {
        UIButton modal_btn;
        memset(&modal_btn, 0, sizeof(modal_btn));
        modal_btn.shortcut = "[ENTER]";
        modal_btn.label = "CLOSE";
        modal_btn.scale = FONT_SCALE_BODY;
        modal_btn.bg_color = COLOR_SUCCESS;
        ui_button_measure(&modal_btn, 160, 38);
        modal_btn.rect.x = modal_x + (modal_w - modal_btn.rect.w) / 2;
        modal_btn.rect.y = modal_y + modal_h - 50;
        modal_btn.hovered = ui_button_hit_test(&modal_btn, mx, my);
        ui_button_draw(g_ui.renderer, &modal_btn);
    } else {
        UIButton pause_btn;
        memset(&pause_btn, 0, sizeof(pause_btn));
        pause_btn.shortcut = "[P]";
        pause_btn.label = task_is_paused() ? "RESUME" : "PAUSE";
        pause_btn.scale = FONT_SCALE_BODY;
        pause_btn.bg_color = task_is_paused() ? COLOR_SUCCESS : COLOR_WARNING;
        ui_button_measure(&pause_btn, 140, 38);

        UIButton cancel_btn;
        memset(&cancel_btn, 0, sizeof(cancel_btn));
        cancel_btn.shortcut = "[ESC]";
        cancel_btn.label = "CANCEL";
        cancel_btn.scale = FONT_SCALE_BODY;
        cancel_btn.bg_color = COLOR_ERROR;
        ui_button_measure(&cancel_btn, 140, 38);

        int total_w = pause_btn.rect.w + 20 + cancel_btn.rect.w;
        int bx = modal_x + (modal_w - total_w) / 2;
        int by = modal_y + modal_h - 50;

        pause_btn.rect.x = bx;
        pause_btn.rect.y = by;
        pause_btn.hovered = ui_button_hit_test(&pause_btn, mx, my);
        ui_button_draw(g_ui.renderer, &pause_btn);

        cancel_btn.rect.x = bx + pause_btn.rect.w + 20;
        cancel_btn.rect.y = by;
        cancel_btn.hovered = ui_button_hit_test(&cancel_btn, mx, my);
        ui_button_draw(g_ui.renderer, &cancel_btn);
    }
}

typedef struct {
    SDL_Rect dialog;
    int padding;
    int text_x;
    int text_w;
    int body_y;
    int button_y;
    UIButton yes_button;
    UIButton no_button;
    UIButton cancel_button;
} ParallelPromptLayout;

static void setup_parallel_button(UIButton* button, const char* shortcut, const char* label, ThemeColor color) {
    memset(button, 0, sizeof(*button));
    button->shortcut = shortcut;
    button->label = label;
    button->scale = FONT_SCALE_BODY;
    button->bg_color = color;
    ui_button_measure(button, 0, 42);
}

static void calculate_parallel_prompt_layout(ParallelPromptLayout* layout) {
    memset(layout, 0, sizeof(*layout));
    layout->padding = 24;
    setup_parallel_button(&layout->yes_button, "[Y]", "YES (PARALLEL)", COLOR_SUCCESS);
    setup_parallel_button(&layout->no_button, "[N]", "NO (SEQUENTIAL)", COLOR_SECONDARY);
    setup_parallel_button(&layout->cancel_button, "[ESC]", "CANCEL", COLOR_ERROR);

    const int outer_margin = 16;
    const int gap = 12;
    int intrinsic_buttons = layout->yes_button.rect.w + layout->no_button.rect.w +
                            layout->cancel_button.rect.w + gap * 2;
    int minimum_w = intrinsic_buttons + layout->padding * 2 + 6;
    int preferred_w = 900;
    int available_w = g_ui.window_width - outer_margin * 2;
    int dialog_w = preferred_w;
    if (dialog_w > available_w) dialog_w = available_w;
    if (dialog_w < minimum_w && available_w >= minimum_w) dialog_w = minimum_w;
    if (dialog_w < 320) dialog_w = 320;

    int dialog_h = 340;
    int available_h = g_ui.window_height - outer_margin * 2;
    if (dialog_h > available_h) dialog_h = available_h;
    if (dialog_h < 260) dialog_h = 260;
    layout->dialog = (SDL_Rect){(g_ui.window_width - dialog_w) / 2,
                                (g_ui.window_height - dialog_h) / 2, dialog_w, dialog_h};
    layout->text_x = layout->dialog.x + layout->padding;
    layout->text_w = layout->dialog.w - layout->padding * 2;
    layout->body_y = layout->dialog.y + 62;

    int inner_w = layout->text_w;
    int used_w = intrinsic_buttons;
    int extra = inner_w - used_w;
    if (extra > 0) {
        int add = extra / 3;
        layout->yes_button.rect.w += add;
        layout->no_button.rect.w += add;
        layout->cancel_button.rect.w += extra - add * 2;
    }
    int buttons_w = layout->yes_button.rect.w + layout->no_button.rect.w +
                    layout->cancel_button.rect.w + gap * 2;
    int button_x = layout->dialog.x + (layout->dialog.w - buttons_w) / 2;
    layout->button_y = layout->dialog.y + layout->dialog.h - layout->padding - layout->yes_button.rect.h;
    layout->yes_button.rect.x = button_x;
    layout->yes_button.rect.y = layout->button_y;
    layout->no_button.rect.x = button_x + layout->yes_button.rect.w + gap;
    layout->no_button.rect.y = layout->button_y;
    layout->cancel_button.rect.x = layout->no_button.rect.x + layout->no_button.rect.w + gap;
    layout->cancel_button.rect.y = layout->button_y;
}

static int draw_wrapped_prompt_text(const char* text, int x, int y, int max_w, ThemeColor color) {
    char line[256] = {0};
    const char* cursor = text;
    const int line_height = 20;
    while (*cursor) {
        while (*cursor == ' ') cursor++;
        if (!*cursor) break;
        const char* end = cursor;
        while (*end && *end != ' ') end++;
        size_t word_len = (size_t)(end - cursor);
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", (int)word_len, cursor);
        int candidate_w = 0;
        font_get_text_size(candidate, FONT_SCALE_BODY, &candidate_w, NULL);
        if (line[0] && candidate_w > max_w) {
            font_draw_text_shadow(g_ui.renderer, x, y, line, FONT_SCALE_BODY, color.r, color.g, color.b, 255);
            y += line_height;
            snprintf(line, sizeof(line), "%.*s", (int)word_len, cursor);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        cursor = end;
    }
    if (line[0]) {
        font_draw_text_shadow_truncated(g_ui.renderer, x, y, line, FONT_SCALE_BODY, max_w,
                                        color.r, color.g, color.b, 255);
        y += line_height;
    }
    return y;
}

static void draw_parallel_prompt_modal(void) {
    draw_background();
    draw_header_bar();
    draw_platform_selector();

    SDL_SetRenderDrawBlendMode(g_ui.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ui.renderer, 0, 0, 0, 180);
    SDL_Rect full_rect = { 0, 0, g_ui.window_width, g_ui.window_height };
    SDL_RenderFillRect(g_ui.renderer, &full_rect);

    ParallelPromptLayout layout;
    calculate_parallel_prompt_layout(&layout);
    int modal_w = layout.dialog.w;
    int modal_h = layout.dialog.h;
    int modal_x = layout.dialog.x;
    int modal_y = layout.dialog.y;

    theme_draw_gradient_panel(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, COLOR_BG_PANEL, COLOR_BG_DARK);
    theme_draw_border_rect(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, 3, COLOR_PRIMARY);

    theme_draw_filled_rect(g_ui.renderer, modal_x + 3, modal_y + 3, modal_w - 6, 40, COLOR_SURFACE_SELECTED);
    font_draw_text_shadow_truncated(g_ui.renderer, layout.text_x, modal_y + 12, "PARALLEL DOWNLOADS PROMPT",
                                    FONT_SCALE_HEADING, layout.text_w, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);

    int sel_cnt = get_selected_count();
    int max_workers = (g_config.max_parallel_downloads > 0) ? g_config.max_parallel_downloads : 3;

    char msg1[256], msg2[256], msg3[256];
    snprintf(msg1, sizeof(msg1), "You have selected %d platforms for installation.", sel_cnt);
    snprintf(msg2, sizeof(msg2), "Would you like to enable multi-threaded parallel downloads?");
    snprintf(msg3, sizeof(msg3), "(Max %d simultaneous connections to protect server & network limits)", max_workers);

    int text_y = layout.body_y;
    text_y = draw_wrapped_prompt_text(msg1, layout.text_x, text_y, layout.text_w, COLOR_TEXT_PRIMARY) + 8;
    text_y = draw_wrapped_prompt_text(msg2, layout.text_x, text_y, layout.text_w, COLOR_TEXT_PRIMARY) + 12;
    draw_wrapped_prompt_text(msg3, layout.text_x, text_y, layout.text_w, COLOR_TEXT_MUTED);

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    layout.yes_button.hovered = ui_button_hit_test(&layout.yes_button, mx, my);
    layout.no_button.hovered = ui_button_hit_test(&layout.no_button, mx, my);
    layout.cancel_button.hovered = ui_button_hit_test(&layout.cancel_button, mx, my);
    ui_button_draw(g_ui.renderer, &layout.yes_button);
    ui_button_draw(g_ui.renderer, &layout.no_button);
    ui_button_draw(g_ui.renderer, &layout.cancel_button);

    draw_footer_bar();
}

static void draw_system_status_view(void) {
    draw_background();
    draw_header_bar();

    int start_y = HEADER_HEIGHT + 10;
    int content_w = g_ui.window_width - 2 * MARGIN_CONTAINER;
    int content_h = g_ui.window_height - start_y - FOOTER_HEIGHT - 50;

    // Outer panel container
    theme_draw_gradient_panel(g_ui.renderer, MARGIN_CONTAINER, start_y, content_w, content_h, COLOR_BG_PANEL, COLOR_BG_DARK);
    theme_draw_border_rect(g_ui.renderer, MARGIN_CONTAINER, start_y, content_w, content_h, 2, COLOR_PRIMARY);

    // Section Header Subtitle bar
    char header_subtitle[256];
    if (g_ui.status_report.overall_health == HEALTH_STATUS_HEALTHY) {
        snprintf(header_subtitle, sizeof(header_subtitle), "System Health Check: HEALTHY [OK] -- All system components verified.");
    } else {
        snprintf(header_subtitle, sizeof(header_subtitle), "System Health Check: WARNING [%d warning(s) found] -- Check detailed report below.", g_ui.status_report.bios_missing_total + g_ui.status_report.platforms_incomplete);
    }
    theme_draw_filled_rect(g_ui.renderer, MARGIN_CONTAINER + 2, start_y + 2, content_w - 4, 32, COLOR_SURFACE);
    font_draw_text_shadow(g_ui.renderer, MARGIN_CONTAINER + 12, start_y + 10, header_subtitle, FONT_SCALE_BODY,
                          (g_ui.status_report.overall_health == HEALTH_STATUS_HEALTHY) ? COLOR_SUCCESS.r : COLOR_WARNING.r,
                          (g_ui.status_report.overall_health == HEALTH_STATUS_HEALTHY) ? COLOR_SUCCESS.g : COLOR_WARNING.g,
                          (g_ui.status_report.overall_health == HEALTH_STATUS_HEALTHY) ? COLOR_SUCCESS.b : COLOR_WARNING.b, 255);

    // Scroll Viewport Clipping setup
    SDL_Rect viewport = { MARGIN_CONTAINER + 10, start_y + 38, content_w - 20, content_h - 44 };
    SDL_RenderSetClipRect(g_ui.renderer, &viewport);

    int cur_y = viewport.y - g_ui.status_scroll_y;
    int left_x = viewport.x + 10;
    int val_x = left_x + 280;

    SystemDiagnosticReport* rep = &g_ui.status_report;

    #define DRAW_ROW(lbl, val, col) \
        do { \
            if (cur_y + 20 >= viewport.y && cur_y <= viewport.y + viewport.h) { \
                font_draw_text_truncated(g_ui.renderer, left_x, cur_y, (lbl), FONT_SCALE_BODY, 260, COLOR_TEXT_SECONDARY.r, COLOR_TEXT_SECONDARY.g, COLOR_TEXT_SECONDARY.b, 255); \
                char s_val[512]; \
                diagnostic_format_path_short((val), s_val, sizeof(s_val)); \
                font_draw_text_shadow_truncated(g_ui.renderer, val_x, cur_y, s_val, FONT_SCALE_BODY, viewport.w - 300, (col).r, (col).g, (col).b, 255); \
            } \
            cur_y += 22; \
        } while(0)

    #define DRAW_SECTION(title) \
        do { \
            cur_y += 8; \
            if (cur_y + 24 >= viewport.y && cur_y <= viewport.y + viewport.h) { \
                theme_draw_filled_rect(g_ui.renderer, left_x, cur_y, viewport.w - 20, 24, COLOR_SURFACE_SELECTED); \
                font_draw_text_shadow(g_ui.renderer, left_x + 8, cur_y + 4, (title), FONT_SCALE_BODY, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255); \
            } \
            cur_y += 30; \
        } while(0)

    // Section 1: System & Retro Setup
    DRAW_SECTION("=== 1. SYSTEM & RETRO SETUP ===");
    DRAW_ROW("OS Distribution", rep->os_distro, COLOR_TEXT_PRIMARY);
    DRAW_ROW("Architecture", rep->os_arch, COLOR_TEXT_PRIMARY);
    DRAW_ROW("Retro Setup Version", rep->app_version, COLOR_PRIMARY);
    DRAW_ROW("Setup Engine", "Native C (Autonoma / No Shell)", COLOR_SUCCESS);
    DRAW_ROW("Home Directory", rep->home_dir, COLOR_TEXT_SECONDARY);
    DRAW_ROW("Config Directory", rep->config_dir, COLOR_TEXT_SECONDARY);
    DRAW_ROW("ROM Base Directory", rep->rom_base_dir, COLOR_TEXT_SECONDARY);

    // Section 2: RetroArch Environment
    DRAW_SECTION("=== 2. RETROARCH ENVIRONMENT ===");
    DRAW_ROW("RetroArch Binary", rep->retroarch_binary_found ? "FOUND [OK]" : "NOT FOUND [WARNING]", rep->retroarch_binary_found ? COLOR_SUCCESS : COLOR_WARNING);
    if (rep->retroarch_binary_found) {
        DRAW_ROW("Binary Path", rep->retroarch_binary_path, COLOR_TEXT_SECONDARY);
    }
    DRAW_ROW("Operating Mode", (g_config.mode == MODE_STEAM) ? "Steam RetroArch" : "Standalone RetroArch", COLOR_TEXT_PRIMARY);
    DRAW_ROW("Target Directory", rep->retroarch_target_dir, COLOR_TEXT_SECONDARY);
    DRAW_ROW("Configuration File", rep->retroarch_config_found ? "FOUND [OK]" : "MISSING [WARNING]", rep->retroarch_config_found ? COLOR_SUCCESS : COLOR_WARNING);
    DRAW_ROW("Directory Permissions", (rep->dir_ra_readable && rep->dir_ra_writable) ? "READ / WRITE [OK]" : "READ ONLY / RESTRICTED", (rep->dir_ra_readable && rep->dir_ra_writable) ? COLOR_SUCCESS : COLOR_ERROR);

    // Section 3: Storage & Inventory Stats
    DRAW_SECTION("=== 3. STORAGE & ASSETS INVENTORY ===");
    char free_str[128], total_str[128];
    diagnostic_format_size(rep->disk_free_bytes, free_str, sizeof(free_str));
    diagnostic_format_size(rep->disk_total_bytes, total_str, sizeof(total_str));
    char disk_val[256];
    snprintf(disk_val, sizeof(disk_val), "%.100s free / %.100s total", free_str, total_str);
    DRAW_ROW("Available Disk Space", disk_val, COLOR_SUCCESS);

    char cores_val[128];
    snprintf(cores_val, sizeof(cores_val), "%d installed (%d info files)", rep->cores_installed_count, rep->core_info_count);
    DRAW_ROW("Libretro Cores", cores_val, COLOR_TEXT_PRIMARY);

    char bios_val[128];
    snprintf(bios_val, sizeof(bios_val), "%d found, %d missing", rep->bios_found_total, rep->bios_missing_total);
    DRAW_ROW("System BIOS Files", bios_val, (rep->bios_missing_total == 0) ? COLOR_SUCCESS : COLOR_WARNING);

    char roms_sz_str[128];
    diagnostic_format_size(rep->rom_size_total, roms_sz_str, sizeof(roms_sz_str));
    char roms_val[256];
    snprintf(roms_val, sizeof(roms_val), "%d files (%s)", rep->rom_files_total, roms_sz_str);
    DRAW_ROW("ROM Library Files", roms_val, COLOR_TEXT_PRIMARY);

    char pls_val[128];
    snprintf(pls_val, sizeof(pls_val), "%d generated .lpl files", rep->playlists_found_total);
    DRAW_ROW("RetroArch Playlists", pls_val, COLOR_TEXT_PRIMARY);

    char thumbs_sz_str[128];
    diagnostic_format_size(rep->thumbnails_size_total, thumbs_sz_str, sizeof(thumbs_sz_str));
    char thumbs_val[256];
    snprintf(thumbs_val, sizeof(thumbs_val), "%d images (%s)", rep->thumbnails_count_total, thumbs_sz_str);
    DRAW_ROW("Boxart Thumbnails", thumbs_val, COLOR_TEXT_PRIMARY);

    // Section 4: Selected Platform Diagnostics
    DRAW_SECTION("=== 4. SELECTED PLATFORMS HEALTH ===");
    char plat_summary[128];
    snprintf(plat_summary, sizeof(plat_summary), "%d Selected | %d Ready | %d Incomplete", rep->platforms_selected, rep->platforms_ready, rep->platforms_incomplete);
    DRAW_ROW("Platforms Summary", plat_summary, (rep->platforms_incomplete == 0) ? COLOR_SUCCESS : COLOR_WARNING);

    for (int p = 0; p < rep->platform_diag_count; p++) {
        PlatformDiagnostic* pd = &rep->platform_diags[p];
        char p_title[256];
        snprintf(p_title, sizeof(p_title), "  [%s] %s", pd->platform->id, pd->platform->name);
        ThemeColor p_col = (pd->status == PLATFORM_STATUS_READY) ? COLOR_SUCCESS : COLOR_WARNING;
        DRAW_ROW(p_title, (pd->status == PLATFORM_STATUS_READY) ? "READY [OK]" : "INCOMPLETE [!!]", p_col);

        char detail1[512];
        snprintf(detail1, sizeof(detail1), "    Core: %s (%s) | BIOS: %d/%d | ROMs: %d",
                 pd->platform->core_file, pd->core_installed ? "INSTALLED" : "MISSING",
                 pd->bios_found_count, pd->bios_required_count, pd->rom_count);
        DRAW_ROW("", detail1, COLOR_TEXT_MUTED);

        if (pd->bios_missing_count > 0) {
            char detail2[512];
            snprintf(detail2, sizeof(detail2), "    [!] Missing BIOS: %s", pd->bios_missing_names);
            DRAW_ROW("", detail2, COLOR_WARNING);
        }
    }

    // Section 5: Health Check Summary
    DRAW_SECTION("=== 5. HEALTH CHECK SUMMARY ===");
    DRAW_ROW("[OK] OS Distribution & Architecture", "Verified", COLOR_SUCCESS);
    DRAW_ROW("[OK] Native C Execution Engine", "Active (Zero Shell Subprocesses)", COLOR_SUCCESS);
    DRAW_ROW(rep->retroarch_binary_found ? "[OK] RetroArch Installation" : "[!!] RetroArch Installation", rep->retroarch_binary_found ? "FOUND" : "NOT FOUND IN PATH", rep->retroarch_binary_found ? COLOR_SUCCESS : COLOR_WARNING);
    DRAW_ROW(rep->dir_ra_writable ? "[OK] Target Directory Permissions" : "[ERROR] Target Directory Permissions", rep->dir_ra_writable ? "READ/WRITE" : "PERMISSION DENIED", rep->dir_ra_writable ? COLOR_SUCCESS : COLOR_ERROR);
    DRAW_ROW((rep->bios_missing_total == 0) ? "[OK] System BIOS Files" : "[!!] System BIOS Files", (rep->bios_missing_total == 0) ? "ALL EXPECTED PRESENT" : "SOME REQUIRED MISSING", (rep->bios_missing_total == 0) ? COLOR_SUCCESS : COLOR_WARNING);
    DRAW_ROW((rep->platforms_incomplete == 0) ? "[OK] Platform Cores & Playlists" : "[!!] Platform Cores & Playlists", (rep->platforms_incomplete == 0) ? "ALL READY" : "INCOMPLETE PLATFORMS DETECTED", (rep->platforms_incomplete == 0) ? COLOR_SUCCESS : COLOR_WARNING);

    cur_y += 20;
    int total_content_height = cur_y - (viewport.y - g_ui.status_scroll_y);

    SDL_RenderSetClipRect(g_ui.renderer, NULL);

    // Scrollbar indicator
    int max_scroll = total_content_height - viewport.h;
    if (max_scroll < 0) max_scroll = 0;
    if (g_ui.status_scroll_y > max_scroll) g_ui.status_scroll_y = max_scroll;
    if (g_ui.status_scroll_y < 0) g_ui.status_scroll_y = 0;

    if (max_scroll > 0) {
        int sb_x = MARGIN_CONTAINER + content_w - 12;
        int sb_y = viewport.y;
        int sb_h = viewport.h;
        theme_draw_filled_rect(g_ui.renderer, sb_x, sb_y, 6, sb_h, COLOR_SURFACE);

        int thumb_h = (int)((float)viewport.h / (float)total_content_height * sb_h);
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = sb_y + (int)((float)g_ui.status_scroll_y / (float)max_scroll * (sb_h - thumb_h));
        theme_draw_filled_rect(g_ui.renderer, sb_x, thumb_y, 6, thumb_h, COLOR_PRIMARY);
    }

    // Bottom Action Buttons using UIButton component
    int btn_y = g_ui.window_height - FOOTER_HEIGHT - 44;
    int mx, my;
    SDL_GetMouseState(&mx, &my);

    UIButton ref_btn;
    memset(&ref_btn, 0, sizeof(ref_btn));
    ref_btn.shortcut = "[R]";
    ref_btn.label = "REFRESH";
    ref_btn.scale = FONT_SCALE_BODY;
    ref_btn.bg_color = COLOR_SECONDARY;
    ui_button_measure(&ref_btn, 120, BUTTON_HEIGHT);
    ref_btn.rect.x = MARGIN_CONTAINER + 20;
    ref_btn.rect.y = btn_y;
    ref_btn.hovered = ui_button_hit_test(&ref_btn, mx, my);
    ui_button_draw(g_ui.renderer, &ref_btn);

    UIButton close_btn;
    memset(&close_btn, 0, sizeof(close_btn));
    close_btn.shortcut = "[ENTER]";
    close_btn.label = "CLOSE";
    close_btn.scale = FONT_SCALE_BODY;
    close_btn.bg_color = COLOR_SUCCESS;
    ui_button_measure(&close_btn, 160, BUTTON_HEIGHT);
    close_btn.rect.x = g_ui.window_width - MARGIN_CONTAINER - close_btn.rect.w - 20;
    close_btn.rect.y = btn_y;
    close_btn.hovered = ui_button_hit_test(&close_btn, mx, my);
    ui_button_draw(g_ui.renderer, &close_btn);

    draw_footer_bar();

    #undef DRAW_ROW
    #undef DRAW_SECTION
}

static int SDLCALL catalog_load_thread(void* unused) {
    (void)unused;
    rom_catalog_refresh(g_config.url_config_file, g_catalog_selection_file);
    SDL_AtomicSet(&g_catalog_load_state, 2);
    return 0;
}

static void finish_catalog_loading(void) {
    if (g_ui.view != VIEW_CATALOG_LOADING || SDL_AtomicGet(&g_catalog_load_state) != 2) return;
    log_add(LOG_LEVEL_INFO, "Loaded %d selectable catalog games.", rom_catalog_count());
    g_ui.selected_game_index = 0;
    g_ui.game_scroll_offset = 0;
    g_ui.game_search_filter[0] = 0;
    g_ui.game_search_len = 0;
    g_ui.game_search_active = false;
    ui_update_filtered_games();
    SDL_AtomicSet(&g_catalog_load_state, 0);
    if (rom_catalog_count_for_selected_platforms() > 0) g_ui.view = VIEW_GAME_SELECT;
    else if (get_selected_count() > 1) g_ui.view = VIEW_PARALLEL_PROMPT;
    else {
        g_config.use_parallel_downloads = false;
        g_ui.view = VIEW_TASK_RUNNING;
        g_ui.modal_task = TASK_INSTALL;
        task_start_async(TASK_INSTALL, NULL);
    }
}

static void start_install_or_game_selection(void) {
    save_selected_platforms_config();
    fs_join_path(g_catalog_selection_file, sizeof(g_catalog_selection_file),
                 g_config.config_dir, "selected_roms.conf");
    log_add(LOG_LEVEL_INFO, "Loading individual-game catalogs for selected platforms...");
    g_ui.view = VIEW_CATALOG_LOADING;
    SDL_AtomicSet(&g_catalog_load_state, 1);
    SDL_Thread* thread = SDL_CreateThread(catalog_load_thread, "CatalogLoader", NULL);
    if (thread) SDL_DetachThread(thread);
    else {
        log_add(LOG_LEVEL_ERROR, "Could not start game catalog loader thread.");
        SDL_AtomicSet(&g_catalog_load_state, 2);
    }
}

static void save_games_and_start_install(void) {
    char selection_file[MAX_PATH_LEN];
    fs_join_path(selection_file, sizeof(selection_file), g_config.config_dir, "selected_roms.conf");
    rom_catalog_save_selection(selection_file);
    if (get_selected_count() > 1) g_ui.view = VIEW_PARALLEL_PROMPT;
    else {
        g_config.use_parallel_downloads = false;
        g_ui.view = VIEW_TASK_RUNNING;
        g_ui.modal_task = TASK_INSTALL;
        task_start_async(TASK_INSTALL, NULL);
    }
}

static void save_game_selection(void) {
    if (g_catalog_selection_file[0]) rom_catalog_save_selection(g_catalog_selection_file);
}

static void handle_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            g_ui.running = false;
        } else if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                g_ui.window_width = e.window.data1;
                g_ui.window_height = e.window.data2;
            }
        } else if (e.type == SDL_MOUSEWHEEL) {
            if (g_ui.view == VIEW_STATUS) {
                g_ui.status_scroll_y -= e.wheel.y * 36;
                if (g_ui.status_scroll_y < 0) g_ui.status_scroll_y = 0;
            } else if (g_ui.view == VIEW_TASK_RUNNING) {
                g_ui.task_log_scroll_lines += e.wheel.y * 3;
                if (g_ui.task_log_scroll_lines < 0) g_ui.task_log_scroll_lines = 0;
            } else if (g_ui.view == VIEW_GAME_SELECT) {
                g_ui.game_scroll_offset -= e.wheel.y * 3;
                if (g_ui.game_scroll_offset < 0) g_ui.game_scroll_offset = 0;
                if (g_ui.game_scroll_offset >= g_ui.game_filtered_count)
                    g_ui.game_scroll_offset = g_ui.game_filtered_count > 0 ? g_ui.game_filtered_count - 1 : 0;
                g_ui.selected_game_index = g_ui.game_scroll_offset;
            }
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;

            if (key == SDLK_s && (e.key.keysym.mod & KMOD_CTRL)) {
                g_config.audio_enabled = !g_config.audio_enabled;
                continue;
            }

            if (key == SDLK_m && g_ui.view != VIEW_CATALOG_LOADING) {
                audio_play_sound(SOUND_TOGGLE);
                set_setup_mode(g_config.mode == MODE_STEAM ? MODE_STANDALONE : MODE_STEAM);
                load_selected_platforms_config();
                if (g_ui.view == VIEW_STATUS) {
                    diagnostic_run_scan(&g_ui.status_report);
                }
                continue;
            }

            if (g_ui.view == VIEW_STATUS) {
                if (key == SDLK_UP) {
                    g_ui.status_scroll_y -= 36;
                    if (g_ui.status_scroll_y < 0) g_ui.status_scroll_y = 0;
                } else if (key == SDLK_DOWN) {
                    g_ui.status_scroll_y += 36;
                } else if (key == SDLK_PAGEUP) {
                    g_ui.status_scroll_y -= 250;
                    if (g_ui.status_scroll_y < 0) g_ui.status_scroll_y = 0;
                } else if (key == SDLK_PAGEDOWN) {
                    g_ui.status_scroll_y += 250;
                } else if (key == SDLK_HOME) {
                    g_ui.status_scroll_y = 0;
                } else if (key == SDLK_END) {
                    g_ui.status_scroll_y = 99999;
                } else if (key == SDLK_r) {
                    diagnostic_run_scan(&g_ui.status_report);
                    audio_play_sound(SOUND_TOGGLE);
                } else if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
                    g_ui.view = VIEW_MAIN_MENU;
                    audio_play_sound(SOUND_SELECT);
                }
                continue;
            }

            if (g_ui.view == VIEW_PARALLEL_PROMPT) {
                if (key == SDLK_y) {
                    g_config.use_parallel_downloads = true;
                    g_ui.view = VIEW_TASK_RUNNING;
                    g_ui.modal_task = TASK_INSTALL;
                    task_start_async(TASK_INSTALL, NULL);
                    audio_play_sound(SOUND_FANFARE);
                } else if (key == SDLK_n) {
                    g_config.use_parallel_downloads = false;
                    g_ui.view = VIEW_TASK_RUNNING;
                    g_ui.modal_task = TASK_INSTALL;
                    task_start_async(TASK_INSTALL, NULL);
                    audio_play_sound(SOUND_SELECT);
                } else if (key == SDLK_ESCAPE) {
                    g_ui.view = VIEW_PLATFORM_SELECT;
                    audio_play_sound(SOUND_BACK);
                }
                continue;
            }

            if (g_ui.view == VIEW_TASK_RUNNING) {
                if (task_is_finished() && (key == SDLK_RETURN || key == SDLK_ESCAPE || key == SDLK_SPACE)) {
                    g_ui.view = VIEW_MAIN_MENU;
                    g_ui.task_log_scroll_lines = 0;
                    audio_play_sound(SOUND_SELECT);
                } else {
                    if (key == SDLK_UP) g_ui.task_log_scroll_lines++;
                    else if (key == SDLK_DOWN && g_ui.task_log_scroll_lines > 0) g_ui.task_log_scroll_lines--;
                    else if (key == SDLK_PAGEUP) g_ui.task_log_scroll_lines += 10;
                    else if (key == SDLK_PAGEDOWN) {
                        g_ui.task_log_scroll_lines -= 10;
                        if (g_ui.task_log_scroll_lines < 0) g_ui.task_log_scroll_lines = 0;
                    } else if (key == SDLK_HOME) g_ui.task_log_scroll_lines = 1000000;
                    else if (key == SDLK_END) g_ui.task_log_scroll_lines = 0;
                    else if (!task_is_finished() && key == SDLK_p) {
                        task_toggle_pause();
                        audio_play_sound(SOUND_TOGGLE);
                    } else if (!task_is_finished() && key == SDLK_ESCAPE) {
                        task_cancel();
                        audio_play_sound(SOUND_BACK);
                    }
                }
                continue;
            }

            if (g_ui.view == VIEW_GAME_SELECT) {
                int count = g_ui.game_filtered_count;
                int visible = (g_ui.window_height - HEADER_HEIGHT - 130 - FOOTER_HEIGHT) / PLATFORM_ITEM_HEIGHT;
                if (visible < 1) visible = 1;
                if (key == SDLK_UP && count > 0) {
                    g_ui.selected_game_index = (g_ui.selected_game_index - 1 + count) % count;
                } else if (key == SDLK_DOWN && count > 0) {
                    g_ui.selected_game_index = (g_ui.selected_game_index + 1) % count;
                } else if (key == SDLK_PAGEUP && count > 0) {
                    g_ui.selected_game_index -= visible;
                    if (g_ui.selected_game_index < 0) g_ui.selected_game_index = 0;
                } else if (key == SDLK_PAGEDOWN && count > 0) {
                    g_ui.selected_game_index += visible;
                    if (g_ui.selected_game_index >= count) g_ui.selected_game_index = count - 1;
                } else if (key == SDLK_SPACE && count > 0) {
                    RomCatalogGame* game = rom_catalog_get(g_ui.game_filtered_indices[g_ui.selected_game_index]);
                    if (game) {
                        game->selected = !game->selected;
                        save_game_selection();
                    }
                } else if (key == SDLK_a && !g_ui.game_search_active) {
                    for (int i = 0; i < count; ++i)
                        rom_catalog_get(g_ui.game_filtered_indices[i])->selected = true;
                    save_game_selection();
                } else if (key == SDLK_c && !g_ui.game_search_active) {
                    for (int i = 0; i < count; ++i)
                        rom_catalog_get(g_ui.game_filtered_indices[i])->selected = false;
                    save_game_selection();
                } else if (key == SDLK_SLASH || (key == SDLK_f && (e.key.keysym.mod & KMOD_CTRL))) {
                    g_ui.game_search_active = true;
                } else if (key == SDLK_RETURN) {
                    if (g_ui.game_search_active) g_ui.game_search_active = false;
                    else save_games_and_start_install();
                } else if (key == SDLK_BACKSPACE && g_ui.game_search_len > 0) {
                    g_ui.game_search_filter[--g_ui.game_search_len] = 0;
                    ui_update_filtered_games();
                } else if (key == SDLK_ESCAPE) {
                    if (g_ui.game_search_active || g_ui.game_search_len > 0) {
                        g_ui.game_search_active = false;
                        g_ui.game_search_filter[0] = 0;
                        g_ui.game_search_len = 0;
                        ui_update_filtered_games();
                    } else g_ui.view = VIEW_PLATFORM_SELECT;
                }
                continue;
            }

            if (g_ui.view == VIEW_PLATFORM_SELECT || g_ui.view == VIEW_UNINSTALL_SELECT) {
                int start_y = HEADER_HEIGHT + 10;
                int search_y = start_y + TAB_HEIGHT + 10;
                int list_y = search_y + BUTTON_HEIGHT + 10 + 22 + 6;
                int item_h = PLATFORM_ITEM_HEIGHT;
                int visible_items = (g_ui.window_height - list_y - FOOTER_HEIGHT - 6) / item_h;
                if (visible_items < 1) visible_items = 1;

                if (key == SDLK_UP) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index--;
                        if (g_ui.selected_platform_index < 0) {
                            g_ui.selected_platform_index = g_ui.filtered_count - 1;
                        }
                        if (g_ui.selected_platform_index < g_ui.scroll_offset) {
                            g_ui.scroll_offset = g_ui.selected_platform_index;
                        } else if (g_ui.selected_platform_index >= g_ui.scroll_offset + visible_items) {
                            g_ui.scroll_offset = g_ui.selected_platform_index - visible_items + 1;
                        }
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_DOWN) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index++;
                        if (g_ui.selected_platform_index >= g_ui.filtered_count) {
                            g_ui.selected_platform_index = 0;
                        }
                        if (g_ui.selected_platform_index < g_ui.scroll_offset) {
                            g_ui.scroll_offset = g_ui.selected_platform_index;
                        } else if (g_ui.selected_platform_index >= g_ui.scroll_offset + visible_items) {
                            g_ui.scroll_offset = g_ui.selected_platform_index - visible_items + 1;
                        }
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_PAGEUP) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index -= visible_items;
                        if (g_ui.selected_platform_index < 0) g_ui.selected_platform_index = 0;
                        g_ui.scroll_offset = g_ui.selected_platform_index;
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_PAGEDOWN) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index += visible_items;
                        if (g_ui.selected_platform_index >= g_ui.filtered_count) g_ui.selected_platform_index = g_ui.filtered_count - 1;
                        if (g_ui.selected_platform_index >= g_ui.scroll_offset + visible_items) {
                            g_ui.scroll_offset = g_ui.selected_platform_index - visible_items + 1;
                        }
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_HOME) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index = 0;
                        g_ui.scroll_offset = 0;
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_END) {
                    if (g_ui.filtered_count > 0) {
                        g_ui.selected_platform_index = g_ui.filtered_count - 1;
                        if (g_ui.selected_platform_index >= visible_items) {
                            g_ui.scroll_offset = g_ui.selected_platform_index - visible_items + 1;
                        }
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_LEFT) {
                    if (g_category_count > 0) {
                        g_ui.selected_mfr_tab = (g_ui.selected_mfr_tab - 1 + g_category_count) % g_category_count;
                        g_ui.selected_platform_index = 0;
                        g_ui.scroll_offset = 0;
                        ui_update_filtered_platforms();
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_RIGHT) {
                    if (g_category_count > 0) {
                        g_ui.selected_mfr_tab = (g_ui.selected_mfr_tab + 1) % g_category_count;
                        g_ui.selected_platform_index = 0;
                        g_ui.scroll_offset = 0;
                        ui_update_filtered_platforms();
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_SPACE) {
                    if (g_ui.selected_platform_index >= 0 && g_ui.selected_platform_index < g_ui.filtered_count) {
                        int p_idx = g_ui.filtered_indices[g_ui.selected_platform_index];
                        g_platforms[p_idx].selected = !g_platforms[p_idx].selected;
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_TOGGLE);
                    }
                } else if (key == SDLK_RETURN) {
                    save_selected_platforms_config();
                    if (g_ui.view == VIEW_UNINSTALL_SELECT) {
                        g_ui.view = VIEW_TASK_RUNNING;
                        g_ui.modal_task = TASK_UNINSTALL;
                        task_start_async(TASK_UNINSTALL, NULL);
                        audio_play_sound(SOUND_SELECT);
                    } else {
                        start_install_or_game_selection();
                        audio_play_sound(SOUND_FANFARE);
                    }
                } else if (key == SDLK_ESCAPE) {
                    if (g_ui.search_active || g_ui.search_len > 0) {
                        g_ui.search_active = false;
                        g_ui.search_filter[0] = 0;
                        g_ui.search_len = 0;
                        ui_update_filtered_platforms();
                        audio_play_sound(SOUND_BACK);
                    } else {
                        g_ui.view = VIEW_MAIN_MENU;
                        audio_play_sound(SOUND_BACK);
                    }
                } else if (key == SDLK_BACKSPACE) {
                    if (g_ui.search_len > 0) {
                        g_ui.search_filter[--g_ui.search_len] = 0;
                        ui_update_filtered_platforms();
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (!g_ui.search_active) {
                    if (key == SDLK_a) {
                        reset_all_selections(true);
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_SELECT);
                    } else if (key == SDLK_c) {
                        reset_all_selections(false);
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_BACK);
                    } else if (key == SDLK_i) {
                        for (int k = 0; k < TOTAL_PLATFORMS; k++) {
                            g_platforms[k].selected = !g_platforms[k].selected;
                        }
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_TOGGLE);
                    }
                }
                continue;
            }

            if (g_ui.view == VIEW_MAIN_MENU) {
                if (key == SDLK_UP) {
                    g_ui.selected_menu_index = (g_ui.selected_menu_index - 1 + MENU_OPTION_COUNT) % MENU_OPTION_COUNT;
                    audio_play_sound(SOUND_MOVE);
                } else if (key == SDLK_DOWN) {
                    g_ui.selected_menu_index = (g_ui.selected_menu_index + 1) % MENU_OPTION_COUNT;
                    audio_play_sound(SOUND_MOVE);
                } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                    audio_play_sound(SOUND_SELECT);
                    TaskType task = g_menu_options[g_ui.selected_menu_index].task;

                    if (task == TASK_NONE || task == TASK_UNINSTALL) {
                        g_ui.view = (task == TASK_UNINSTALL) ? VIEW_UNINSTALL_SELECT : VIEW_PLATFORM_SELECT;
                        ui_update_filtered_platforms();
                    } else if (task == TASK_STATUS) {
                        g_ui.view = VIEW_STATUS;
                        g_ui.status_scroll_y = 0;
                        diagnostic_run_scan(&g_ui.status_report);
                    } else if (task == TASK_INSTALL) {
                        start_install_or_game_selection();
                    } else {
                        g_ui.view = VIEW_TASK_RUNNING;
                        g_ui.modal_task = task;
                        task_start_async(task, NULL);
                    }
                } else if (key == SDLK_q || key == SDLK_ESCAPE) {
                    g_ui.running = false;
                }
            }
        } else if (e.type == SDL_TEXTINPUT) {
            if (g_ui.view == VIEW_GAME_SELECT && g_ui.game_search_active) {
                if (strcmp(e.text.text, "/") == 0 && g_ui.game_search_len == 0) continue;
                if (g_ui.game_search_len + (int)strlen(e.text.text) < (int)sizeof(g_ui.game_search_filter) - 1) {
                    strcat(g_ui.game_search_filter, e.text.text);
                    g_ui.game_search_len = (int)strlen(g_ui.game_search_filter);
                    ui_update_filtered_games();
                }
            } else if ((g_ui.view == VIEW_PLATFORM_SELECT || g_ui.view == VIEW_UNINSTALL_SELECT) && g_ui.search_active) {
                if (g_ui.search_len + (int)strlen(e.text.text) < (int)sizeof(g_ui.search_filter) - 1) {
                    strcat(g_ui.search_filter, e.text.text);
                    g_ui.search_len = (int)strlen(g_ui.search_filter);
                    ui_update_filtered_platforms();
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = e.button.x;
            int my = e.button.y;

            // 1. Header Mode Button click (Global)
            const char* mode_text = (g_config.mode == MODE_STEAM) ? "STEAM" : "STANDALONE";
            UIButton mode_btn;
            memset(&mode_btn, 0, sizeof(mode_btn));
            mode_btn.shortcut = "MODE:";
            mode_btn.label = mode_text;
            mode_btn.scale = FONT_SCALE_BODY;
            ui_button_measure(&mode_btn, 140, BUTTON_HEIGHT);

            mode_btn.rect.x = g_ui.window_width - mode_btn.rect.w - MARGIN_CONTAINER;
            mode_btn.rect.y = (HEADER_HEIGHT - mode_btn.rect.h) / 2;

            if (g_ui.view != VIEW_CATALOG_LOADING && ui_button_hit_test(&mode_btn, mx, my)) {
                audio_play_sound(SOUND_TOGGLE);
                set_setup_mode(g_config.mode == MODE_STEAM ? MODE_STANDALONE : MODE_STEAM);
                load_selected_platforms_config();
                if (g_ui.view == VIEW_STATUS) {
                    diagnostic_run_scan(&g_ui.status_report);
                }
                continue;
            }

            // 2. Footer audio toggle button click (Global)
            int audio_w;
            font_get_text_size("[AUDIO: ON]", FONT_SCALE_BODY, &audio_w, NULL);
            int audio_x = g_ui.window_width - audio_w - MARGIN_CONTAINER;
            int audio_y = g_ui.window_height - FOOTER_HEIGHT;
            SDL_Rect audio_rect = { audio_x, audio_y, audio_w, FOOTER_HEIGHT };
            if (is_point_in_rect(mx, my, audio_rect)) {
                g_config.audio_enabled = !g_config.audio_enabled;
                audio_play_sound(SOUND_TOGGLE);
                continue;
            }

            // 3. View-specific mouse clicks
            if (g_ui.view == VIEW_GAME_SELECT) {
                int control_y = HEADER_HEIGHT + 18 + 36 + 24;
                const int search_width = 430;
                SDL_Rect search_rect = { MARGIN_CONTAINER, control_y, search_width, BUTTON_HEIGHT };
                if (is_point_in_rect(mx, my, search_rect)) {
                    g_ui.game_search_active = true;
                    continue;
                }
                int button_x = search_rect.x + search_rect.w + 12;
                const struct { const char* shortcut; const char* label; } actions[] = {
                    { "[A]", "ALL SHOWN" }, { "[C]", "CLEAR SHOWN" },
                    { "[ENTER]", "SAVE & INSTALL" }
                };
                bool action_handled = false;
                for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
                    UIButton button;
                    memset(&button, 0, sizeof(button));
                    button.shortcut = actions[i].shortcut;
                    button.label = actions[i].label;
                    button.scale = FONT_SCALE_BODY;
                    ui_button_measure(&button, 0, BUTTON_HEIGHT);
                    button.rect.x = button_x;
                    button.rect.y = control_y;
                    if (ui_button_hit_test(&button, mx, my)) {
                        if (i < 2) {
                            bool state = i == 0;
                            for (int game = 0; game < g_ui.game_filtered_count; ++game)
                                rom_catalog_get(g_ui.game_filtered_indices[game])->selected = state;
                            save_game_selection();
                        } else save_games_and_start_install();
                        action_handled = true;
                        break;
                    }
                    button_x += button.rect.w + 8;
                }
                if (action_handled) continue;

                g_ui.game_search_active = false;
                int list_y = control_y + BUTTON_HEIGHT + 10;
                int visible = (g_ui.window_height - list_y - FOOTER_HEIGHT - 6) / PLATFORM_ITEM_HEIGHT;
                for (int row = 0; row < visible; ++row) {
                    int filtered_index = g_ui.game_scroll_offset + row;
                    if (filtered_index >= g_ui.game_filtered_count) break;
                    int catalog_index = g_ui.game_filtered_indices[filtered_index];
                    RomCatalogGame* game = rom_catalog_get(catalog_index);
                    if (!game) break;
                    SDL_Rect rect = { MARGIN_CONTAINER, list_y + row * PLATFORM_ITEM_HEIGHT,
                                      g_ui.window_width - 2 * MARGIN_CONTAINER, PLATFORM_ITEM_HEIGHT - 4 };
                    if (is_point_in_rect(mx, my, rect)) {
                        g_ui.selected_game_index = filtered_index;
                        game->selected = !game->selected;
                        save_game_selection();
                        break;
                    }
                }
                continue;
            }

            if (g_ui.view == VIEW_STATUS) {
                int btn_y = g_ui.window_height - FOOTER_HEIGHT - 44;
                UIButton ref_btn;
                memset(&ref_btn, 0, sizeof(ref_btn));
                ref_btn.shortcut = "[R]";
                ref_btn.label = "REFRESH";
                ref_btn.scale = FONT_SCALE_BODY;
                ui_button_measure(&ref_btn, 120, BUTTON_HEIGHT);
                ref_btn.rect.x = MARGIN_CONTAINER + 20;
                ref_btn.rect.y = btn_y;

                if (ui_button_hit_test(&ref_btn, mx, my)) {
                    diagnostic_run_scan(&g_ui.status_report);
                    audio_play_sound(SOUND_TOGGLE);
                    continue;
                }

                UIButton close_btn;
                memset(&close_btn, 0, sizeof(close_btn));
                close_btn.shortcut = "[ENTER]";
                close_btn.label = "CLOSE";
                close_btn.scale = FONT_SCALE_BODY;
                ui_button_measure(&close_btn, 160, BUTTON_HEIGHT);
                close_btn.rect.x = g_ui.window_width - MARGIN_CONTAINER - close_btn.rect.w - 20;
                close_btn.rect.y = btn_y;

                if (ui_button_hit_test(&close_btn, mx, my)) {
                    g_ui.view = VIEW_MAIN_MENU;
                    audio_play_sound(SOUND_SELECT);
                    continue;
                }
                continue;
            }

            if (g_ui.view == VIEW_MAIN_MENU) {
                int start_y = HEADER_HEIGHT + 14 + 26;
                int card_w = g_ui.window_width - 2 * MARGIN_CONTAINER;
                int card_h = CARD_HEIGHT;
                int card_spacing = GAP_SPACING;

                for (size_t i = 0; i < MENU_OPTION_COUNT; i++) {
                    int y = start_y + i * (card_h + card_spacing);
                    SDL_Rect rect = { MARGIN_CONTAINER, y, card_w, card_h };
                    if (is_point_in_rect(mx, my, rect)) {
                        g_ui.selected_menu_index = (int)i;
                        audio_play_sound(SOUND_SELECT);

                        TaskType task = g_menu_options[i].task;
                        if (task == TASK_NONE || task == TASK_UNINSTALL) {
                            g_ui.view = (task == TASK_UNINSTALL) ? VIEW_UNINSTALL_SELECT : VIEW_PLATFORM_SELECT;
                            ui_update_filtered_platforms();
                        } else if (task == TASK_STATUS) {
                            g_ui.view = VIEW_STATUS;
                            g_ui.status_scroll_y = 0;
                            diagnostic_run_scan(&g_ui.status_report);
                        } else if (task == TASK_INSTALL) {
                            start_install_or_game_selection();
                        } else {
                            g_ui.view = VIEW_TASK_RUNNING;
                            g_ui.modal_task = task;
                            task_start_async(task, NULL);
                        }
                        break;
                    }
                }
            } else if (g_ui.view == VIEW_PARALLEL_PROMPT) {
                ParallelPromptLayout layout;
                calculate_parallel_prompt_layout(&layout);
                if (ui_button_hit_test(&layout.yes_button, mx, my)) {
                    g_config.use_parallel_downloads = true;
                    g_ui.view = VIEW_TASK_RUNNING;
                    g_ui.modal_task = TASK_INSTALL;
                    task_start_async(TASK_INSTALL, NULL);
                    audio_play_sound(SOUND_FANFARE);
                } else if (ui_button_hit_test(&layout.no_button, mx, my)) {
                    g_config.use_parallel_downloads = false;
                    g_ui.view = VIEW_TASK_RUNNING;
                    g_ui.modal_task = TASK_INSTALL;
                    task_start_async(TASK_INSTALL, NULL);
                    audio_play_sound(SOUND_SELECT);
                } else if (ui_button_hit_test(&layout.cancel_button, mx, my)) {
                    g_ui.view = VIEW_PLATFORM_SELECT;
                    audio_play_sound(SOUND_BACK);
                }
            } else if (g_ui.view == VIEW_PLATFORM_SELECT || g_ui.view == VIEW_UNINSTALL_SELECT) {
                int start_y = HEADER_HEIGHT + 10;
                int tab_x = MARGIN_CONTAINER;
                int tab_h = TAB_HEIGHT;

                for (int c = 0; c < g_category_count; c++) {
                    const char* cname = g_categories[c].name;
                    UIButton tab_btn;
                    memset(&tab_btn, 0, sizeof(tab_btn));
                    tab_btn.label = cname;
                    tab_btn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&tab_btn, 60, tab_h);

                    tab_btn.rect.x = tab_x;
                    tab_btn.rect.y = start_y;

                    if (ui_button_hit_test(&tab_btn, mx, my)) {
                        g_ui.selected_mfr_tab = c;
                        ui_update_filtered_platforms();
                        audio_play_sound(SOUND_MOVE);
                        break;
                    }
                    tab_x += tab_btn.rect.w + 6;
                }

                int search_y = start_y + tab_h + 10;
                int search_w = 340;
                SDL_Rect srect = { MARGIN_CONTAINER, search_y, search_w, BUTTON_HEIGHT };
                if (is_point_in_rect(mx, my, srect)) {
                    g_ui.search_active = true;
                } else {
                    g_ui.search_active = false;
                }

                int act_x = MARGIN_CONTAINER + search_w + 12;
                const struct { const char* sc; const char* lbl; } actions[] = {
                    { "[A]", "ALL" },
                    { "[C]", "CLEAR" },
                    { "[I]", "INVERT" },
                    { "[ENTER]", "SAVE & RUN" }
                };

                for (int a = 0; a < 4; a++) {
                    UIButton abtn;
                    memset(&abtn, 0, sizeof(abtn));
                    abtn.shortcut = actions[a].sc;
                    abtn.label = actions[a].lbl;
                    abtn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&abtn, 0, BUTTON_HEIGHT);

                    abtn.rect.x = act_x;
                    abtn.rect.y = search_y;

                    if (ui_button_hit_test(&abtn, mx, my)) {
                        if (a == 0) {
                            reset_all_selections(true);
                            save_selected_platforms_config();
                            audio_play_sound(SOUND_SELECT);
                        } else if (a == 1) {
                            reset_all_selections(false);
                            save_selected_platforms_config();
                            audio_play_sound(SOUND_BACK);
                        } else if (a == 2) {
                            for (int i = 0; i < TOTAL_PLATFORMS; i++) g_platforms[i].selected = !g_platforms[i].selected;
                            save_selected_platforms_config();
                            audio_play_sound(SOUND_TOGGLE);
                        } else if (a == 3) {
                            save_selected_platforms_config();
                            if (g_ui.view == VIEW_UNINSTALL_SELECT) {
                                g_ui.view = VIEW_TASK_RUNNING;
                                g_ui.modal_task = TASK_UNINSTALL;
                                task_start_async(TASK_UNINSTALL, NULL);
                                audio_play_sound(SOUND_SELECT);
                            } else {
                                start_install_or_game_selection();
                                audio_play_sound(SOUND_FANFARE);
                            }
                        }
                        break;
                    }
                    act_x += abtn.rect.w + 8;
                }

                int list_y = search_y + BUTTON_HEIGHT + 10 + 22 + 6;
                int item_h = PLATFORM_ITEM_HEIGHT;
                int visible_items = (g_ui.window_height - list_y - FOOTER_HEIGHT - 6) / item_h;

                for (int v = 0; v < visible_items; v++) {
                    int idx_in_filtered = g_ui.scroll_offset + v;
                    if (idx_in_filtered >= g_ui.filtered_count) break;

                    int iy = list_y + v * item_h;
                    SDL_Rect irect = { MARGIN_CONTAINER, iy, g_ui.window_width - 2 * MARGIN_CONTAINER, item_h - 4 };
                    if (is_point_in_rect(mx, my, irect)) {
                        g_ui.selected_platform_index = idx_in_filtered;
                        int p_idx = g_ui.filtered_indices[idx_in_filtered];
                        g_platforms[p_idx].selected = !g_platforms[p_idx].selected;
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_TOGGLE);
                        break;
                    }
                }
            } else if (g_ui.view == VIEW_TASK_RUNNING) {
                int modal_w = g_ui.window_width - 80;
                int modal_h = g_ui.window_height - 100;
                int modal_x = 40;
                int modal_y = 50;

                if (task_is_finished()) {
                    UIButton close_btn;
                    memset(&close_btn, 0, sizeof(close_btn));
                    close_btn.shortcut = "[ENTER]";
                    close_btn.label = "CLOSE";
                    close_btn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&close_btn, 160, 38);
                    close_btn.rect.x = modal_x + (modal_w - close_btn.rect.w) / 2;
                    close_btn.rect.y = modal_y + modal_h - 50;

                    if (ui_button_hit_test(&close_btn, mx, my)) {
                        g_ui.view = VIEW_MAIN_MENU;
                        audio_play_sound(SOUND_SELECT);
                    }
                } else {
                    UIButton pause_btn;
                    memset(&pause_btn, 0, sizeof(pause_btn));
                    pause_btn.shortcut = "[P]";
                    pause_btn.label = task_is_paused() ? "RESUME" : "PAUSE";
                    pause_btn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&pause_btn, 140, 38);

                    UIButton cancel_btn;
                    memset(&cancel_btn, 0, sizeof(cancel_btn));
                    cancel_btn.shortcut = "[ESC]";
                    cancel_btn.label = "CANCEL";
                    cancel_btn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&cancel_btn, 140, 38);

                    int total_w = pause_btn.rect.w + 20 + cancel_btn.rect.w;
                    int bx = modal_x + (modal_w - total_w) / 2;
                    int by = modal_y + modal_h - 50;

                    pause_btn.rect.x = bx;
                    pause_btn.rect.y = by;
                    cancel_btn.rect.x = bx + pause_btn.rect.w + 20;
                    cancel_btn.rect.y = by;

                    if (ui_button_hit_test(&pause_btn, mx, my)) {
                        task_toggle_pause();
                        audio_play_sound(SOUND_TOGGLE);
                    } else if (ui_button_hit_test(&cancel_btn, mx, my)) {
                        task_cancel();
                        audio_play_sound(SOUND_BACK);
                    }
                }
            }
        }
    }
}

void ui_run_main_loop(void) {
    SDL_StartTextInput();

    while (g_ui.running) {
        handle_events();
        finish_catalog_loading();

        SDL_SetRenderDrawColor(g_ui.renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_ui.renderer);

        switch (g_ui.view) {
            case VIEW_MAIN_MENU:
                draw_main_menu();
                break;
            case VIEW_PLATFORM_SELECT:
            case VIEW_UNINSTALL_SELECT:
                draw_platform_selector();
                break;
            case VIEW_CATALOG_LOADING:
                draw_catalog_loading();
                break;
            case VIEW_GAME_SELECT:
                draw_game_selector();
                break;
            case VIEW_STATUS:
                draw_system_status_view();
                break;
            case VIEW_TASK_RUNNING:
                draw_main_menu();
                draw_task_modal();
                break;
            case VIEW_PARALLEL_PROMPT:
                draw_parallel_prompt_modal();
                break;
            default:
                draw_main_menu();
                break;
        }

        SDL_RenderPresent(g_ui.renderer);
        SDL_Delay(16); // ~60 FPS
    }

    SDL_StopTextInput();
}
