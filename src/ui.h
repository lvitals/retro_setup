#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "platform_data.h"
#include "config.h"
#include "tasks.h"

#include "diagnostic.h"

typedef enum {
    VIEW_MAIN_MENU = 0,
    VIEW_PLATFORM_SELECT,
    VIEW_UNINSTALL_SELECT,
    VIEW_TASK_RUNNING,
    VIEW_STATUS,
    VIEW_PARALLEL_PROMPT
} UIViewState;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    int window_width;
    int window_height;
    bool running;

    UIViewState view;
    int selected_menu_index;
    int selected_platform_index;
    int selected_mfr_tab; // 0: ALL, 1: NINTENDO, etc.

    char search_filter[64];
    int search_len;
    bool search_active;

    int scroll_offset;
    int filtered_indices[MAX_PLATFORMS];
    int filtered_count;

    int status_scroll_y;
    SystemDiagnosticReport status_report;

    float anim_timer;
    int particle_x[50];
    int particle_y[50];
    int particle_speed[50];

    bool show_confirm_modal;
    TaskType modal_task;
} UIManager;

extern UIManager g_ui;

bool ui_init(const char* title, int width, int height);
void ui_cleanup(void);
void ui_run_main_loop(void);
void ui_update_filtered_platforms(void);

#endif // UI_H
