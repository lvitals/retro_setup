#include "download.h"
#include "fs.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FILE* file;
    DownloadProgressCallback progress_cb;
    void* user_data;
    bool* cancel_flag;
    DownloadResult* result;
} CurlContext;

static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlContext* ctx = (CurlContext*)userdata;
    if (ctx && ctx->file) {
        return fwrite(ptr, size, nmemb, ctx->file);
    }
    return 0;
}

static int xferinfo_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    CurlContext* ctx = (CurlContext*)clientp;

    if (ctx && ctx->cancel_flag && *(ctx->cancel_flag)) {
        log_add(LOG_LEVEL_WARN, "Download canceled by user flag.");
        return 1; // Abort download immediately
    }

    if (ctx && ctx->result) {
        ctx->result->downloaded_bytes = dlnow;
        ctx->result->total_bytes = dltotal;

        if (ctx->progress_cb) {
            ctx->progress_cb(ctx->result, ctx->user_data);
        }
    }
    return 0;
}

bool download_init(void) {
    return (curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK);
}

void download_cleanup(void) {
    curl_global_cleanup();
}

bool download_file(
    const char* url,
    const char* destination_path,
    DownloadProgressCallback progress_cb,
    void* user_data,
    bool* cancel_flag,
    DownloadResult* out_result
) {
    if (!url || !destination_path) return false;

    DownloadResult local_res;
    memset(&local_res, 0, sizeof(local_res));

    // Ensure parent directory of destination exists
    char dst_dir[4096];
    snprintf(dst_dir, sizeof(dst_dir), "%s", destination_path);
    char* slash = strrchr(dst_dir, '/');
    if (slash) {
        *slash = 0;
        fs_mkdir_p(dst_dir);
    }

    char tmp_path[4096];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", destination_path);

    FILE* f = fopen(tmp_path, "wb");
    if (!f) {
        snprintf(local_res.error, sizeof(local_res.error), "Failed to open temporary file for writing");
        log_add(LOG_LEVEL_ERROR, "Download error: %s (%s)", local_res.error, tmp_path);
        if (out_result) *out_result = local_res;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(f);
        fs_remove_file(tmp_path);
        snprintf(local_res.error, sizeof(local_res.error), "Failed to initialize libcurl handle");
        if (out_result) *out_result = local_res;
        return false;
    }

    CurlContext ctx;
    ctx.file = f;
    ctx.progress_cb = progress_cb;
    ctx.user_data = user_data;
    ctx.cancel_flag = cancel_flag;
    ctx.result = &local_res;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RetroSetup/2.0 (Linux)");

    CURLcode res = curl_easy_perform(curl);
    fclose(f);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &local_res.http_status);
    curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &local_res.speed_bytes_per_sec);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && (local_res.http_status == 200 || local_res.http_status == 206)) {
        local_res.success = 1;
        // Atomic rename of tmp file to destination
        if (rename(tmp_path, destination_path) != 0) {
            fs_copy_file(tmp_path, destination_path);
            fs_remove_file(tmp_path);
        }
        if (out_result) *out_result = local_res;
        return true;
    } else {
        local_res.success = 0;
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            snprintf(local_res.error, sizeof(local_res.error), "Download canceled by user");
        } else {
            snprintf(local_res.error, sizeof(local_res.error), "HTTP Error %ld (%s)", local_res.http_status, curl_easy_strerror(res));
        }
        log_add(LOG_LEVEL_ERROR, "Download failed [%s]: %s", url, local_res.error);
        fs_remove_file(tmp_path);
        if (out_result) *out_result = local_res;
        return false;
    }
}
