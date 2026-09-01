#ifndef TASKS_H
#define TASKS_H

#include "config.h"
#include "download.h"
#include <stdbool.h>

typedef enum {
    TASK_NONE = 0,
    TASK_PREPARE,
    TASK_INSTALL,
    TASK_UNINSTALL,
    TASK_THUMBNAILS,
    TASK_UPDATE_COVERS,
    TASK_UPDATE_CORES,
    TASK_IMPLODE,
    TASK_STATUS,
    TASK_INSTALLATION_DIAGNOSTIC
} TaskType;

const char* task_get_title(TaskType task);

bool tasks_init(void);
void tasks_cleanup(void);

// Asynchronous task launch via SDL_CreateThread
bool task_start_async(TaskType task, const char* extra_args);
void task_cancel(void);
void task_toggle_pause(void);
bool task_is_paused(void);

bool task_is_running(void);
bool task_is_finished(void);
int task_get_exit_code(void);
float task_get_progress(void);
const char* task_get_status_message(void);
void task_get_work_counts(int* completed, int* total);
void task_get_download_snapshot(DownloadManagerSnapshot* snapshot);

// Synchronous task execution (for CLI mode)
int task_run_sync(TaskType task);

#endif // TASKS_H
