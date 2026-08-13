#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <curl/curl.h>

#define MAX_DOWNLOAD_TASKS 256
#define MAX_PARALLEL_DOWNLOADS 3

typedef enum {
    DOWNLOAD_QUEUED = 0,
    DOWNLOAD_CHECKING,
    DOWNLOAD_DOWNLOADING,
    DOWNLOAD_RESUMING,
    DOWNLOAD_PAUSED,
    DOWNLOAD_VERIFYING,
    DOWNLOAD_COMPLETED,
    DOWNLOAD_SKIPPED,
    DOWNLOAD_FAILED,
    DOWNLOAD_CANCELLED
} DownloadState;

typedef struct {
    int id;
    char url[1024];
    char filename[256];
    char destination[2048];
    char partial_path[2048];
    curl_off_t remote_size;
    curl_off_t local_size;
    curl_off_t downloaded_bytes;
    double speed_bytes_per_sec;
    double eta_seconds;
    DownloadState state;
    long http_status;
    bool can_resume;
    char error[256];
} DownloadTask;

typedef struct {
    long http_status;
    curl_off_t downloaded_bytes;
    curl_off_t total_bytes;
    double speed_bytes_per_sec;
    int success;
    int task_id;
    DownloadState state;
    char error[256];
} DownloadResult;

typedef struct {
    int task_count;
    int completed_count;
    int active_count;
    int queued_count;
    curl_off_t downloaded_bytes;
    curl_off_t total_bytes;
    double speed_bytes_per_sec;
    DownloadTask tasks[MAX_DOWNLOAD_TASKS];
} DownloadManagerSnapshot;

typedef void (*DownloadProgressCallback)(const DownloadResult* result, void* user_data);

bool download_init(void);
void download_cleanup(void);
void download_manager_reset(void);
void download_manager_snapshot(DownloadManagerSnapshot* snapshot);
const char* download_state_name(DownloadState state);
bool download_check_url(const char* url, long* http_status, curl_off_t* remote_size,
                        char* error, size_t error_size);

bool download_file(const char* url, const char* destination_path,
                   DownloadProgressCallback progress_cb, void* user_data,
                   bool* cancel_flag, bool* pause_flag,
                   DownloadResult* out_result);

#endif
