#ifndef THEME_H
#define THEME_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    Uint8 r, g, b, a;
} ThemeColor;

// Centralized Color Tokens
extern const ThemeColor COLOR_BG_DARK;
extern const ThemeColor COLOR_BG_PANEL;
extern const ThemeColor COLOR_SURFACE;
extern const ThemeColor COLOR_SURFACE_HOVER;
extern const ThemeColor COLOR_SURFACE_SELECTED;

extern const ThemeColor COLOR_PRIMARY;
extern const ThemeColor COLOR_SECONDARY;
extern const ThemeColor COLOR_SUCCESS;
extern const ThemeColor COLOR_WARNING;
extern const ThemeColor COLOR_ERROR;
extern const ThemeColor COLOR_INFO;

extern const ThemeColor COLOR_TEXT_PRIMARY;
extern const ThemeColor COLOR_TEXT_SECONDARY;
extern const ThemeColor COLOR_TEXT_MUTED;
extern const ThemeColor COLOR_TEXT_HIGHLIGHT;

extern const ThemeColor COLOR_BORDER_DEFAULT;
extern const ThemeColor COLOR_BORDER_FOCUS;
extern const ThemeColor COLOR_BORDER_ACCENT;

// Centralized Layout & Dimension Tokens
#define MARGIN_CONTAINER 20
#define PADDING_CARD 10
#define GAP_SPACING 8

#define HEADER_HEIGHT 56
#define FOOTER_HEIGHT 38
#define CARD_HEIGHT 64
#define PLATFORM_ITEM_HEIGHT 40
#define TAB_HEIGHT 32
#define BUTTON_HEIGHT 34
#define ICON_SIZE 44

// Typography Scale Tokens
#define FONT_SCALE_SMALL 1
#define FONT_SCALE_BODY 2
#define FONT_SCALE_HEADING 2
#define FONT_SCALE_TITLE 3

// Button Layout Tokens
#define BUTTON_PADDING_X 16
#define BUTTON_PADDING_Y 8
#define BUTTON_MIN_WIDTH 80
#define BUTTON_DEFAULT_HEIGHT 36
#define BUTTON_TEXT_SCALE FONT_SCALE_BODY

typedef struct {
    SDL_Rect rect;
    const char* label;
    const char* shortcut;
    int scale;
    bool hovered;
    bool focused;
    bool pressed;
    bool disabled;
    ThemeColor bg_color;
    ThemeColor text_color;
} UIButton;

// Drawing Helpers
void theme_set_draw_color(SDL_Renderer* renderer, ThemeColor color);
void theme_draw_filled_rect(SDL_Renderer* renderer, int x, int y, int w, int h, ThemeColor color);
void theme_draw_border_rect(SDL_Renderer* renderer, int x, int y, int w, int h, int thickness, ThemeColor color);
void theme_draw_gradient_panel(SDL_Renderer* renderer, int x, int y, int w, int h, ThemeColor color1, ThemeColor color2);

// Shared UI Button Component API
void ui_button_measure(UIButton* btn, int min_w, int min_h);
void ui_button_draw(SDL_Renderer* renderer, const UIButton* btn);
bool ui_button_hit_test(const UIButton* btn, int mouse_x, int mouse_y);

#endif // THEME_H
