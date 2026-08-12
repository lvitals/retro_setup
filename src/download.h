#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdbool.h>
#include <curl/curl.h>

typedef struct {
    long http_status;
    curl_off_t downloaded_bytes;
    curl_off_t total_bytes;
    double speed_bytes_per_sec;
    int success;
    char error[256];
} DownloadResult;

typedef void (*DownloadProgressCallback)(const DownloadResult* result, void* user_data);

bool download_init(void);
void download_cleanup(void);

bool download_file(
    const char* url,
    const char* destination_path,
    DownloadProgressCallback progress_cb,
    void* user_data,
    bool* cancel_flag,
    DownloadResult* out_result
);

#endif // DOWNLOAD_H
