#ifndef FONT_H
#define FONT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

bool font_init(SDL_Renderer* renderer);
void font_cleanup(void);

// Standard 8x8 Retro Bitmap Font Renderer
void font_draw_text(SDL_Renderer* renderer, int x, int y, const char* text, int scale, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

// Draw text with drop shadow
void font_draw_text_shadow(SDL_Renderer* renderer, int x, int y, const char* text, int scale, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

// Draw text truncated with "..." if string exceeds max_width in pixels
void font_draw_text_truncated(SDL_Renderer* renderer, int x, int y, const char* text, int scale, int max_width, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void font_draw_text_shadow_truncated(SDL_Renderer* renderer, int x, int y, const char* text, int scale, int max_width, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

// Get width and height of rendered string in pixels
void font_get_text_size(const char* text, int scale, int* width, int* height);

#endif // FONT_H
