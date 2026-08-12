#include "ui.h"
#include "font.h"
#include "audio.h"
#include "theme.h"
#include "tasks.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

UIManager g_ui;

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
    { TASK_THUMBNAILS,"--thumbnails", "DOWNLOAD THUMBNAILS",        "Download boxart, snaps, and title artwork",                        { 155,  89, 182, 255 } },
    { TASK_IMPLODE,   "--implode",   "RESET CONFIGURATION",         "Reset local configuration and clear generated files",            { 192,  57,  43, 255 } },
    { TASK_STATUS,    "--status",    "SYSTEM STATUS",               "View system distribution, mode, and saved settings",              { 241, 196,  15, 255 } }
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
        case TASK_IMPLODE: { // Bomb Icon 💣
            theme_draw_filled_rect(r, cx + 3, cy - 16, 3, 3, white);
            theme_draw_filled_rect(r, cx + 1, cy - 12, 3, 3, white);
            theme_draw_filled_rect(r, cx - 10, cy - 7, 20, 20, white);
            theme_draw_filled_rect(r, cx - 12, cy - 4, 24, 14, white);
            theme_draw_filled_rect(r, cx - 7, cy - 10, 14, 24, white);
            theme_draw_filled_rect(r, cx - 7, cy - 4, 3, 3, bg_color);
            break;
        }
        case TASK_STATUS: { // Bar Chart Dashboard Icon 📊
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
        if (g_ui.selected_mfr_tab != MFR_ALL && g_platforms[i].mfr != (ManufacturerCategory)g_ui.selected_mfr_tab) {
            continue;
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

    for (int m = 0; m < MFR_COUNT; m++) {
        const char* mname = get_mfr_name((ManufacturerCategory)m);
        UIButton tab_btn;
        memset(&tab_btn, 0, sizeof(tab_btn));
        tab_btn.label = mname;
        tab_btn.scale = FONT_SCALE_BODY;

        ui_button_measure(&tab_btn, 60, tab_h);

        tab_btn.rect.x = tab_x;
        tab_btn.rect.y = start_y;

        bool active = (g_ui.selected_mfr_tab == m);
        Uint8 mcolor = get_mfr_color((ManufacturerCategory)m);
        ThemeColor mtheme = { (Uint8)((mcolor >> 16) & 0xFF), (Uint8)((mcolor >> 8) & 0xFF), (Uint8)(mcolor & 0xFF), 230 };

        tab_btn.bg_color = active ? mtheme : COLOR_SURFACE;
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

        // Manufacturer Pill Badge
        Uint8 mcolor = get_mfr_color(platform->mfr);
        ThemeColor mtheme = { (Uint8)((mcolor >> 16) & 0xFF), (Uint8)((mcolor >> 8) & 0xFF), (Uint8)(mcolor & 0xFF), 230 };

        int pw = (int)strlen(platform->id) * 16 + 16;
        theme_draw_filled_rect(g_ui.renderer, item_rect.x + 65, iy + 5, pw, 26, mtheme);
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

static void draw_task_modal(void) {
    theme_draw_filled_rect(g_ui.renderer, 0, 0, g_ui.window_width, g_ui.window_height, (ThemeColor){ 0, 0, 0, 210 });

    int modal_w = g_ui.window_width - 80;
    int modal_h = g_ui.window_height - 100;
    int modal_x = 40;
    int modal_y = 50;

    theme_draw_gradient_panel(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, COLOR_BG_PANEL, COLOR_BG_DARK);
    theme_draw_border_rect(g_ui.renderer, modal_x, modal_y, modal_w, modal_h, 3, COLOR_PRIMARY);

    // Modal Header
    font_draw_text_shadow(g_ui.renderer, modal_x + 20, modal_y + 20, task_get_title(g_ui.modal_task), FONT_SCALE_TITLE, COLOR_PRIMARY.r, COLOR_PRIMARY.g, COLOR_PRIMARY.b, 255);

    // Status Message
    font_draw_text_truncated(g_ui.renderer, modal_x + 20, modal_y + 54, task_get_status_message(), FONT_SCALE_BODY, modal_w - 40, COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);

    // Progress Bar
    int bar_x = modal_x + 20;
    int bar_y = modal_y + 80;
    int bar_w = modal_w - 40;
    int bar_h = 28;

    theme_draw_filled_rect(g_ui.renderer, bar_x, bar_y, bar_w, bar_h, COLOR_SURFACE);
    theme_draw_border_rect(g_ui.renderer, bar_x, bar_y, bar_w, bar_h, 2, COLOR_BORDER_DEFAULT);

    float progress = task_get_progress();
    if (progress >= 0.0f) {
        int fill_w = (int)(bar_w * progress);
        if (fill_w > bar_w) fill_w = bar_w;
        theme_draw_gradient_panel(g_ui.renderer, bar_x, bar_y, fill_w, bar_h, COLOR_SUCCESS, (ThemeColor){ 39, 174, 96, 255 });

        char pct[32];
        snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100));
        font_draw_text_shadow(g_ui.renderer, bar_x + fill_w / 2 - 16, bar_y + 6, pct, FONT_SCALE_BODY, COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
    } else {
        float pulse = (sinf(g_ui.anim_timer * 8.0f) + 1.0f) * 0.5f;
        int fill_w = (int)(bar_w * pulse);
        theme_draw_gradient_panel(g_ui.renderer, bar_x, bar_y, fill_w, bar_h, COLOR_SECONDARY, (ThemeColor){ 41, 128, 185, 200 });
        font_draw_text_shadow(g_ui.renderer, bar_x + bar_w / 2 - 60, bar_y + 6, "PROCESSING...", FONT_SCALE_BODY, COLOR_TEXT_PRIMARY.r, COLOR_TEXT_PRIMARY.g, COLOR_TEXT_PRIMARY.b, 255);
    }

    // Terminal Log Window
    int term_x = modal_x + 20;
    int term_y = bar_y + bar_h + 16;
    int term_w = modal_w - 40;
    int term_h = modal_h - (term_y - modal_y) - 64;

    theme_draw_filled_rect(g_ui.renderer, term_x, term_y, term_w, term_h, (ThemeColor){ 10, 12, 18, 255 });
    theme_draw_border_rect(g_ui.renderer, term_x, term_y, term_w, term_h, 1, COLOR_BORDER_DEFAULT);

    int max_lines_disp = term_h / 18;
    int log_count = log_get_count();
    int start_line = (log_count > max_lines_disp) ? (log_count - max_lines_disp) : 0;

    for (int l = start_line; l < log_count; l++) {
        int ly = term_y + 8 + (l - start_line) * 18;
        const char* log_line = log_get_line(l);
        ThemeColor line_color = COLOR_TEXT_SECONDARY;
        if (strstr(log_line, "ERROR") || strstr(log_line, "FAILED")) {
            line_color = COLOR_ERROR;
        } else if (strstr(log_line, "WARNING") || strstr(log_line, "WARN")) {
            line_color = COLOR_WARNING;
        } else if (strstr(log_line, "=== ")) {
            line_color = COLOR_SUCCESS;
        }
        font_draw_text_truncated(g_ui.renderer, term_x + 10, ly, log_line, FONT_SCALE_BODY, term_w - 20, line_color.r, line_color.g, line_color.b, 255);
    }

    // Action Button (Close / Cancel) using UIButton component
    UIButton modal_btn;
    memset(&modal_btn, 0, sizeof(modal_btn));
    modal_btn.scale = FONT_SCALE_BODY;

    if (task_is_finished()) {
        modal_btn.shortcut = "[ENTER]";
        modal_btn.label = "CLOSE";
        modal_btn.bg_color = COLOR_SUCCESS;
    } else {
        modal_btn.shortcut = "[ESC]";
        modal_btn.label = "CANCEL";
        modal_btn.bg_color = COLOR_ERROR;
    }

    ui_button_measure(&modal_btn, 160, 38);

    int btn_x = modal_x + (modal_w - modal_btn.rect.w) / 2;
    int btn_y = modal_y + modal_h - 50;
    modal_btn.rect.x = btn_x;
    modal_btn.rect.y = btn_y;

    int mx, my;
    SDL_GetMouseState(&mx, &my);
    modal_btn.hovered = ui_button_hit_test(&modal_btn, mx, my);

    ui_button_draw(g_ui.renderer, &modal_btn);
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
        } else if (e.type == SDL_KEYDOWN) {
            SDL_Keycode key = e.key.keysym.sym;

            if (key == SDLK_s && (e.key.keysym.mod & KMOD_CTRL)) {
                g_config.audio_enabled = !g_config.audio_enabled;
                continue;
            }

            if (g_ui.view == VIEW_TASK_RUNNING) {
                if (task_is_finished() && (key == SDLK_RETURN || key == SDLK_ESCAPE || key == SDLK_SPACE)) {
                    g_ui.view = VIEW_MAIN_MENU;
                    audio_play_sound(SOUND_SELECT);
                } else if (!task_is_finished() && key == SDLK_ESCAPE) {
                    task_cancel();
                    audio_play_sound(SOUND_BACK);
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
                        g_ui.view = VIEW_PLATFORM_SELECT;
                        ui_update_filtered_platforms();
                    } else {
                        g_ui.view = VIEW_TASK_RUNNING;
                        g_ui.modal_task = task;
                        task_start_async(task, NULL);
                    }
                } else if (key == SDLK_m) {
                    audio_play_sound(SOUND_TOGGLE);
                    set_setup_mode(g_config.mode == MODE_STEAM ? MODE_STANDALONE : MODE_STEAM);
                    load_selected_platforms_config();
                } else if (key == SDLK_q || key == SDLK_ESCAPE) {
                    g_ui.running = false;
                }
            } else if (g_ui.view == VIEW_PLATFORM_SELECT) {
                if (g_ui.search_active) {
                    if (key == SDLK_ESCAPE || key == SDLK_RETURN) {
                        g_ui.search_active = false;
                    } else if (key == SDLK_BACKSPACE) {
                        if (g_ui.search_len > 0) {
                            g_ui.search_filter[--g_ui.search_len] = 0;
                            ui_update_filtered_platforms();
                        }
                    }
                    continue;
                }

                if (key == SDLK_UP) {
                    if (g_ui.selected_platform_index > 0) {
                        g_ui.selected_platform_index--;
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_DOWN) {
                    if (g_ui.selected_platform_index < g_ui.filtered_count - 1) {
                        g_ui.selected_platform_index++;
                        audio_play_sound(SOUND_MOVE);
                    }
                } else if (key == SDLK_SPACE) {
                    if (g_ui.filtered_count > 0 && g_ui.selected_platform_index < g_ui.filtered_count) {
                        int p_idx = g_ui.filtered_indices[g_ui.selected_platform_index];
                        g_platforms[p_idx].selected = !g_platforms[p_idx].selected;
                        save_selected_platforms_config();
                        audio_play_sound(SOUND_TOGGLE);
                    }
                } else if (key == SDLK_TAB) {
                    g_ui.selected_mfr_tab = (g_ui.selected_mfr_tab + 1) % MFR_COUNT;
                    ui_update_filtered_platforms();
                    audio_play_sound(SOUND_MOVE);
                } else if (key == SDLK_a) {
                    reset_all_selections(true);
                    save_selected_platforms_config();
                    audio_play_sound(SOUND_SELECT);
                } else if (key == SDLK_c) {
                    reset_all_selections(false);
                    save_selected_platforms_config();
                    audio_play_sound(SOUND_BACK);
                } else if (key == SDLK_i) {
                    for (int i = 0; i < TOTAL_PLATFORMS; i++) g_platforms[i].selected = !g_platforms[i].selected;
                    save_selected_platforms_config();
                    audio_play_sound(SOUND_TOGGLE);
                } else if (key == SDLK_SLASH || key == SDLK_f) {
                    g_ui.search_active = true;
                } else if (key == SDLK_RETURN) {
                    save_selected_platforms_config();
                    audio_play_sound(SOUND_FANFARE);
                    g_ui.view = VIEW_TASK_RUNNING;
                    g_ui.modal_task = TASK_INSTALL;
                    task_start_async(TASK_INSTALL, NULL);
                } else if (key == SDLK_ESCAPE) {
                    save_selected_platforms_config();
                    g_ui.view = VIEW_MAIN_MENU;
                    audio_play_sound(SOUND_BACK);
                }
            }
        } else if (e.type == SDL_TEXTINPUT) {
            if (g_ui.view == VIEW_PLATFORM_SELECT && g_ui.search_active) {
                if (g_ui.search_len + (int)strlen(e.text.text) < (int)sizeof(g_ui.search_filter) - 1) {
                    strcat(g_ui.search_filter, e.text.text);
                    g_ui.search_len = (int)strlen(g_ui.search_filter);
                    ui_update_filtered_platforms();
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = e.button.x;
            int my = e.button.y;

            // Footer audio toggle button click
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

            // Header Mode Button click
            const char* mode_text = (g_config.mode == MODE_STEAM) ? "STEAM" : "STANDALONE";
            UIButton mode_btn;
            memset(&mode_btn, 0, sizeof(mode_btn));
            mode_btn.shortcut = "MODE:";
            mode_btn.label = mode_text;
            mode_btn.scale = FONT_SCALE_BODY;
            ui_button_measure(&mode_btn, 140, BUTTON_HEIGHT);

            mode_btn.rect.x = g_ui.window_width - mode_btn.rect.w - MARGIN_CONTAINER;
            mode_btn.rect.y = (HEADER_HEIGHT - mode_btn.rect.h) / 2;

            if (ui_button_hit_test(&mode_btn, mx, my)) {
                audio_play_sound(SOUND_TOGGLE);
                set_setup_mode(g_config.mode == MODE_STEAM ? MODE_STANDALONE : MODE_STEAM);
                load_selected_platforms_config();
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
                            g_ui.view = VIEW_PLATFORM_SELECT;
                            ui_update_filtered_platforms();
                        } else {
                            g_ui.view = VIEW_TASK_RUNNING;
                            g_ui.modal_task = task;
                            task_start_async(task, NULL);
                        }
                        break;
                    }
                }
            } else if (g_ui.view == VIEW_PLATFORM_SELECT) {
                int start_y = HEADER_HEIGHT + 10;
                int tab_x = MARGIN_CONTAINER;
                int tab_h = TAB_HEIGHT;

                for (int m = 0; m < MFR_COUNT; m++) {
                    const char* mname = get_mfr_name((ManufacturerCategory)m);
                    UIButton tab_btn;
                    memset(&tab_btn, 0, sizeof(tab_btn));
                    tab_btn.label = mname;
                    tab_btn.scale = FONT_SCALE_BODY;
                    ui_button_measure(&tab_btn, 60, tab_h);

                    tab_btn.rect.x = tab_x;
                    tab_btn.rect.y = start_y;

                    if (ui_button_hit_test(&tab_btn, mx, my)) {
                        g_ui.selected_mfr_tab = m;
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
                            audio_play_sound(SOUND_FANFARE);
                            g_ui.view = VIEW_TASK_RUNNING;
                            g_ui.modal_task = TASK_INSTALL;
                            task_start_async(TASK_INSTALL, NULL);
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

                UIButton modal_btn;
                memset(&modal_btn, 0, sizeof(modal_btn));
                modal_btn.scale = FONT_SCALE_BODY;
                if (task_is_finished()) {
                    modal_btn.shortcut = "[ENTER]";
                    modal_btn.label = "CLOSE";
                } else {
                    modal_btn.shortcut = "[ESC]";
                    modal_btn.label = "CANCEL";
                }
                ui_button_measure(&modal_btn, 160, 38);
                modal_btn.rect.x = modal_x + (modal_w - modal_btn.rect.w) / 2;
                modal_btn.rect.y = modal_y + modal_h - 50;

                if (ui_button_hit_test(&modal_btn, mx, my)) {
                    if (task_is_finished()) {
                        g_ui.view = VIEW_MAIN_MENU;
                        audio_play_sound(SOUND_SELECT);
                    } else {
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

        SDL_SetRenderDrawColor(g_ui.renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_ui.renderer);

        switch (g_ui.view) {
            case VIEW_MAIN_MENU:
                draw_main_menu();
                break;
            case VIEW_PLATFORM_SELECT:
                draw_platform_selector();
                break;
            case VIEW_TASK_RUNNING:
                draw_main_menu();
                draw_task_modal();
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
