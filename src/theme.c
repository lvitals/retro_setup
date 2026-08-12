#include "theme.h"

// Centralized Color Palette Definitions
const ThemeColor COLOR_BG_DARK          = { 15,  17,  26, 255 };
const ThemeColor COLOR_BG_PANEL         = { 25,  30,  48, 255 };
const ThemeColor COLOR_SURFACE          = { 24,  28,  44, 240 };
const ThemeColor COLOR_SURFACE_HOVER    = { 35,  42,  64, 255 };
const ThemeColor COLOR_SURFACE_SELECTED = { 30,  60, 100, 240 };

const ThemeColor COLOR_PRIMARY          = {   0, 210, 255, 255 };
const ThemeColor COLOR_SECONDARY        = {  52, 152, 219, 255 };
const ThemeColor COLOR_SUCCESS          = {  46, 204, 113, 255 };
const ThemeColor COLOR_WARNING          = { 241, 196,  15, 255 };
const ThemeColor COLOR_ERROR            = { 231,  76,  60, 255 };
const ThemeColor COLOR_INFO             = { 155,  89, 182, 255 };

const ThemeColor COLOR_TEXT_PRIMARY     = { 255, 255, 255, 255 };
const ThemeColor COLOR_TEXT_SECONDARY   = { 170, 200, 230, 255 };
const ThemeColor COLOR_TEXT_MUTED       = { 120, 140, 165, 255 };
const ThemeColor COLOR_TEXT_HIGHLIGHT   = {   0, 230, 255, 255 };

const ThemeColor COLOR_BORDER_DEFAULT   = {  60,  75, 100, 180 };
const ThemeColor COLOR_BORDER_FOCUS     = {   0, 220, 255, 255 };
const ThemeColor COLOR_BORDER_ACCENT    = { 255, 255, 255, 220 };

#include "font.h"
#include <stdio.h>
#include <string.h>

void theme_set_draw_color(SDL_Renderer* renderer, ThemeColor color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void theme_draw_filled_rect(SDL_Renderer* renderer, int x, int y, int w, int h, ThemeColor color) {
    theme_set_draw_color(renderer, color);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer, &rect);
}

void theme_draw_border_rect(SDL_Renderer* renderer, int x, int y, int w, int h, int thickness, ThemeColor color) {
    theme_set_draw_color(renderer, color);
    for (int t = 0; t < thickness; t++) {
        SDL_Rect rect = { x + t, y + t, w - 2 * t, h - 2 * t };
        SDL_RenderDrawRect(renderer, &rect);
    }
}

void theme_draw_gradient_panel(SDL_Renderer* renderer, int x, int y, int w, int h, ThemeColor color1, ThemeColor color2) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < h; i++) {
        float factor = (float)i / (float)h;
        Uint8 red = (Uint8)(color1.r + (color2.r - color1.r) * factor);
        Uint8 green = (Uint8)(color1.g + (color2.g - color1.g) * factor);
        Uint8 blue = (Uint8)(color1.b + (color2.b - color1.b) * factor);
        Uint8 alpha = (Uint8)(color1.a + (color2.a - color1.a) * factor);
        SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
        SDL_RenderDrawLine(renderer, x, y + i, x + w - 1, y + i);
    }
}

void ui_button_measure(UIButton* btn, int min_w, int min_h) {
    if (!btn) return;
    int scale = (btn->scale > 0) ? btn->scale : BUTTON_TEXT_SCALE;

    char full_label[256];
    if (btn->shortcut && btn->shortcut[0] && btn->label && btn->label[0]) {
        snprintf(full_label, sizeof(full_label), "%s %s", btn->shortcut, btn->label);
    } else if (btn->shortcut && btn->shortcut[0]) {
        snprintf(full_label, sizeof(full_label), "%s", btn->shortcut);
    } else {
        snprintf(full_label, sizeof(full_label), "%s", btn->label ? btn->label : "");
    }

    int text_w = 0, text_h = 0;
    font_get_text_size(full_label, scale, &text_w, &text_h);

    int calc_w = text_w + BUTTON_PADDING_X * 2;
    int calc_h = text_h + BUTTON_PADDING_Y * 2;

    if (calc_w < min_w) calc_w = min_w;
    if (calc_w < BUTTON_MIN_WIDTH) calc_w = BUTTON_MIN_WIDTH;

    if (calc_h < min_h) calc_h = min_h;
    if (calc_h < BUTTON_DEFAULT_HEIGHT) calc_h = BUTTON_DEFAULT_HEIGHT;

    btn->rect.w = calc_w;
    btn->rect.h = calc_h;
}

void ui_button_draw(SDL_Renderer* renderer, const UIButton* btn) {
    if (!renderer || !btn) return;

    int scale = (btn->scale > 0) ? btn->scale : BUTTON_TEXT_SCALE;

    char full_label[256];
    if (btn->shortcut && btn->shortcut[0] && btn->label && btn->label[0]) {
        snprintf(full_label, sizeof(full_label), "%s %s", btn->shortcut, btn->label);
    } else if (btn->shortcut && btn->shortcut[0]) {
        snprintf(full_label, sizeof(full_label), "%s", btn->shortcut);
    } else {
        snprintf(full_label, sizeof(full_label), "%s", btn->label ? btn->label : "");
    }

    int text_w = 0, text_h = 0;
    font_get_text_size(full_label, scale, &text_w, &text_h);

    // Exact horizontal and vertical centering using measured font dimensions
    int text_x = btn->rect.x + (btn->rect.w - text_w) / 2;
    int text_y = btn->rect.y + (btn->rect.h - text_h) / 2;

    ThemeColor bg = (btn->bg_color.a > 0) ? btn->bg_color : COLOR_SURFACE;
    if (btn->disabled) {
        bg = COLOR_BG_PANEL;
    } else if (btn->pressed) {
        bg = COLOR_SURFACE_SELECTED;
    } else if (btn->hovered || btn->focused) {
        bg = COLOR_SURFACE_HOVER;
    }

    ThemeColor border = (btn->hovered || btn->focused) ? COLOR_BORDER_FOCUS : COLOR_BORDER_DEFAULT;
    int border_thickness = (btn->hovered || btn->focused) ? 2 : 1;

    theme_draw_filled_rect(renderer, btn->rect.x, btn->rect.y, btn->rect.w, btn->rect.h, bg);
    theme_draw_border_rect(renderer, btn->rect.x, btn->rect.y, btn->rect.w, btn->rect.h, border_thickness, border);

    ThemeColor fg = (btn->text_color.a > 0) ? btn->text_color : COLOR_TEXT_PRIMARY;
    if (btn->disabled) fg = COLOR_TEXT_MUTED;

    font_draw_text_shadow(renderer, text_x, text_y, full_label, scale, fg.r, fg.g, fg.b, fg.a);
}

bool ui_button_hit_test(const UIButton* btn, int mouse_x, int mouse_y) {
    if (!btn) return false;
    return (mouse_x >= btn->rect.x && mouse_x < btn->rect.x + btn->rect.w &&
            mouse_y >= btn->rect.y && mouse_y < btn->rect.y + btn->rect.h);
}
