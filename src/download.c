#include "download.h"
#include "fs.h"
#include "log.h"
#include "config.h"
#include <SDL2/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <json-c/json.h>

#define MAX_FALLBACK_URLS 8
#define MAX_DOWNLOAD_RETRIES 5
#define RETRO_SETUP_USER_AGENT "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 RetroSetupGUI/3.0"

typedef struct {
    DownloadTask tasks[MAX_DOWNLOAD_TASKS];
    int count;
    SDL_mutex* mutex;
    SDL_cond* condition;
} DownloadManager;

typedef struct {
    FILE* file;
    DownloadProgressCallback progress_cb;
    void* user_data;
    bool* cancel_flag;
    bool* pause_flag;
    DownloadResult* result;
    int task_id;
    curl_off_t initial_offset;
    bool resume_requested;
    bool resume_rejected;
    DownloadState state_before_pause;
    Uint64 started_ticks;
} CurlContext;

typedef struct {
    curl_off_t size;
    bool accepts_ranges;
    long status;
} RemoteInfo;

static DownloadManager g_downloads;

static bool validate_for_destination(const char* path, const char* destination, char* error, size_t error_size) {
    if (!fs_is_file(path) || fs_file_size(path) <= 0) {
        if (error) snprintf(error, error_size, "File is missing or empty");
        return false;
    }
    const char* extension = strrchr(destination, '.');
    if (extension && (!strcasecmp(extension, ".zip") || !strcasecmp(extension, ".7z") ||
                      !strcasecmp(extension, ".rar") || !strcasecmp(extension, ".tar")))
        return fs_validate_archive(path, error, error_size);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    unsigned char prefix[32] = {0};
    size_t length = fread(prefix, 1, sizeof(prefix) - 1, file);
    fclose(file);
    if (length > 0 && (strstr((char*)prefix, "<!DOCTYPE html") || strstr((char*)prefix, "<html"))) {
        if (error) snprintf(error, error_size, "Server returned HTML instead of the requested file");
        return false;
    }
    return true;
}

static void manager_lock(void) { if (g_downloads.mutex) SDL_LockMutex(g_downloads.mutex); }
static void manager_unlock(void) { if (g_downloads.mutex) SDL_UnlockMutex(g_downloads.mutex); }

const char* download_state_name(DownloadState state) {
    static const char* names[] = {"QUEUED", "CHECKING", "DOWNLOADING", "RESUMING", "PAUSED",
                                  "VERIFYING", "COMPLETED", "SKIPPED", "FAILED", "CANCELLED"};
    return (state >= DOWNLOAD_QUEUED && state <= DOWNLOAD_CANCELLED) ? names[state] : "UNKNOWN";
}

static DownloadTask* task_by_id_unlocked(int id) {
    for (int i = 0; i < g_downloads.count; ++i)
        if (g_downloads.tasks[i].id == id) return &g_downloads.tasks[i];
    return NULL;
}

static bool state_is_terminal(DownloadState state) {
    return state == DOWNLOAD_COMPLETED || state == DOWNLOAD_SKIPPED || state == DOWNLOAD_FAILED ||
           state == DOWNLOAD_CANCELLED;
}

/* Claim a destination once per install run. Other workers reuse the result. */
static int manager_claim(const char* url, const char* destination, DownloadResult* reused) {
    manager_lock();
    for (;;) {
        DownloadTask* existing = NULL;
        for (int i = 0; i < g_downloads.count; ++i) {
            if (!strcmp(g_downloads.tasks[i].destination, destination)) { existing = &g_downloads.tasks[i]; break; }
        }
        if (!existing) break;
        while (!state_is_terminal(existing->state)) {
            SDL_CondWait(g_downloads.condition, g_downloads.mutex);
            existing = NULL;
            for (int i = 0; i < g_downloads.count; ++i)
                if (!strcmp(g_downloads.tasks[i].destination, destination)) { existing = &g_downloads.tasks[i]; break; }
            if (!existing) break;
        }
        if (!existing) continue;
        memset(reused, 0, sizeof(*reused));
        reused->task_id = existing->id;
        reused->state = existing->state;
        reused->http_status = existing->http_status;
        reused->downloaded_bytes = existing->downloaded_bytes;
        reused->total_bytes = existing->remote_size;
        reused->speed_bytes_per_sec = existing->speed_bytes_per_sec;
        reused->success = existing->state == DOWNLOAD_COMPLETED || existing->state == DOWNLOAD_SKIPPED;
        snprintf(reused->error, sizeof(reused->error), "%s", existing->error);
        manager_unlock();
        return 0;
    }
    int slot = g_downloads.count;
    if (slot >= MAX_DOWNLOAD_TASKS) {
        /* Long sequential jobs such as thumbnail collections may contain
           thousands of files. Recycle an inactive display slot instead of
           treating the UI history limit as a download limit. */
        slot = -1;
        for (int i = 0; i < g_downloads.count; ++i) {
            if (state_is_terminal(g_downloads.tasks[i].state)) { slot = i; break; }
        }
        if (slot < 0) { manager_unlock(); return -1; }
    }
    DownloadTask* t = &g_downloads.tasks[slot];
    memset(t, 0, sizeof(*t));
    t->id = slot + 1;
    snprintf(t->url, sizeof(t->url), "%s", url);
    snprintf(t->destination, sizeof(t->destination), "%s", destination);
    snprintf(t->partial_path, sizeof(t->partial_path), "%s.part", destination);
    fs_get_filename(t->filename, sizeof(t->filename), destination);
    t->remote_size = -1;
    t->state = DOWNLOAD_QUEUED;
    if (slot == g_downloads.count) ++g_downloads.count;
    int id = t->id;
    manager_unlock();
    return id;
}

static void manager_update(int id, DownloadState state, curl_off_t now, curl_off_t total,
                           double speed, long http, const char* error) {
    manager_lock();
    DownloadTask* t = task_by_id_unlocked(id);
    if (t) {
        t->state = state;
        if (now >= 0) t->downloaded_bytes = now;
        if (total >= 0) t->remote_size = total;
        t->speed_bytes_per_sec = speed;
        t->http_status = http;
        t->eta_seconds = (speed > 0.0 && t->remote_size > t->downloaded_bytes)
                       ? (double)(t->remote_size - t->downloaded_bytes) / speed : -1.0;
        if (error) snprintf(t->error, sizeof(t->error), "%s", error);
        if (state_is_terminal(state) && g_downloads.condition) SDL_CondBroadcast(g_downloads.condition);
    }
    manager_unlock();
}

bool download_init(void) {
    memset(&g_downloads, 0, sizeof(g_downloads));
    g_downloads.mutex = SDL_CreateMutex();
    g_downloads.condition = SDL_CreateCond();
    return g_downloads.mutex && g_downloads.condition && curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK;
}

void download_cleanup(void) {
    curl_global_cleanup();
    if (g_downloads.mutex) SDL_DestroyMutex(g_downloads.mutex);
    if (g_downloads.condition) SDL_DestroyCond(g_downloads.condition);
    g_downloads.mutex = NULL;
    g_downloads.condition = NULL;
}

void download_manager_reset(void) {
    manager_lock();
    memset(g_downloads.tasks, 0, sizeof(g_downloads.tasks));
    g_downloads.count = 0;
    manager_unlock();
}

void download_manager_snapshot(DownloadManagerSnapshot* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    manager_lock();
    out->task_count = g_downloads.count;
    for (int i = 0; i < g_downloads.count; ++i) {
        DownloadTask* t = &g_downloads.tasks[i];
        out->tasks[i] = *t;
        if (t->state == DOWNLOAD_COMPLETED || t->state == DOWNLOAD_SKIPPED) out->completed_count++;
        if (t->state == DOWNLOAD_DOWNLOADING || t->state == DOWNLOAD_RESUMING || t->state == DOWNLOAD_PAUSED) out->active_count++;
        if (t->state == DOWNLOAD_QUEUED || t->state == DOWNLOAD_CHECKING) out->queued_count++;
        if (t->downloaded_bytes > 0) out->downloaded_bytes += t->downloaded_bytes;
        if (t->remote_size > 0) out->total_bytes += t->remote_size;
        if (t->state == DOWNLOAD_DOWNLOADING || t->state == DOWNLOAD_RESUMING) out->speed_bytes_per_sec += t->speed_bytes_per_sec;
    }
    manager_unlock();
}

static size_t probe_abort_callback(void* data, size_t size, size_t count, void* user_data) {
    (void)data; (void)size; (void)count; (void)user_data;
    return 0;
}

static bool query_remote(const char* url, RemoteInfo* info, char* error, size_t error_size) {
    memset(info, 0, sizeof(*info));
    info->size = -1;
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, RETRO_SETUP_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &info->status);
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &info->size);
    if (rc != CURLE_OK && error) snprintf(error, error_size, "%s", errbuf[0] ? errbuf : curl_easy_strerror(rc));
    curl_easy_cleanup(curl);
    return rc == CURLE_OK && info->status >= 200 && info->status < 400;
}

bool download_check_url(const char* url, long* http_status, curl_off_t* remote_size,
                        char* error, size_t error_size) {
    RemoteInfo info;
    bool ok = query_remote(url, &info, error, error_size);
    /* Some API endpoints intentionally reject HEAD. Probe them with GET and
       abort after the first response bytes; an HTTP success still proves the
       endpoint is reachable without downloading the catalog body. */
    if (!ok && info.status == 405) {
        CURL* curl = curl_easy_init();
        if (curl) {
            char probe_error[CURL_ERROR_SIZE] = {0};
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, RETRO_SETUP_USER_AGENT);
            curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, probe_abort_callback);
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, probe_error);
            CURLcode rc = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &info.status);
            ok = info.status >= 200 && info.status < 400 &&
                 (rc == CURLE_OK || rc == CURLE_WRITE_ERROR);
            if (!ok && error)
                snprintf(error, error_size, "%s", probe_error[0] ? probe_error : curl_easy_strerror(rc));
            curl_easy_cleanup(curl);
        }
    }
    if (http_status) *http_status = info.status;
    if (remote_size) *remote_size = info.size;
    return ok;
}

static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    CurlContext* ctx = userdata;
    size_t bytes = size * nitems;
    if (ctx && ctx->resume_requested && !strncmp(buffer, "HTTP/", 5)) {
        const char* p = strchr(buffer, ' ');
        long status = p ? strtol(p + 1, NULL, 10) : 0;
        if (status == 200) {
            ctx->resume_rejected = true;
            return 0; /* abort before an ignored Range response can be appended */
        }
    }
    return bytes;
}

static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlContext* ctx = userdata;
    return (ctx && ctx->file) ? fwrite(ptr, size, nmemb, ctx->file) : 0;
}

static int xferinfo_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    CurlContext* ctx = clientp;
    if (!ctx) return 1;
    if (ctx->cancel_flag && *ctx->cancel_flag) return 1;
    if (ctx->pause_flag && *ctx->pause_flag) {
        manager_update(ctx->task_id, DOWNLOAD_PAUSED, ctx->initial_offset + dlnow,
                       dltotal > 0 ? ctx->initial_offset + dltotal : -1, 0, 0, NULL);
        while (*ctx->pause_flag) {
            if (ctx->cancel_flag && *ctx->cancel_flag) return 1;
            SDL_Delay(50);
        }
    }
    double elapsed = (SDL_GetTicks64() - ctx->started_ticks) / 1000.0;
    double speed = elapsed > 0.05 ? (double)dlnow / elapsed : 0.0;
    curl_off_t now = ctx->initial_offset + dlnow;
    curl_off_t total = dltotal > 0 ? ctx->initial_offset + dltotal : -1;
    DownloadState state = ctx->resume_requested ? DOWNLOAD_RESUMING : DOWNLOAD_DOWNLOADING;
    manager_update(ctx->task_id, state, now, total, speed, 0, NULL);
    ctx->result->task_id = ctx->task_id;
    ctx->result->state = state;
    ctx->result->downloaded_bytes = now;
    ctx->result->total_bytes = total;
    ctx->result->speed_bytes_per_sec = speed;
    if (ctx->progress_cb) ctx->progress_cb(ctx->result, ctx->user_data);
    return 0;
}

typedef struct {
    char* data;
    size_t size;
} DownloadMemoryBuffer;

static size_t append_download_response(void* contents, size_t size, size_t count, void* user_data) {
    size_t bytes = size * count;
    DownloadMemoryBuffer* buffer = user_data;
    char* expanded = realloc(buffer->data, buffer->size + bytes + 1);
    if (!expanded) return 0;
    buffer->data = expanded;
    memcpy(buffer->data + buffer->size, contents, bytes);
    buffer->size += bytes;
    buffer->data[buffer->size] = 0;
    return bytes;
}

static bool resolve_archive_fallbacks(const char* original_url,
                                      char fallback_urls[MAX_FALLBACK_URLS][1024],
                                      int* fallback_count) {
    if (!original_url || !fallback_urls || !fallback_count) return false;
    *fallback_count = 0;

    const char* tag = "archive.org/download/";
    const char* p = strstr(original_url, tag);
    if (!p) return false;
    p += strlen(tag);

    char identifier[256] = {0};
    const char* slash = strchr(p, '/');
    if (!slash || slash == p) return false;
    size_t id_len = (size_t)(slash - p);
    if (id_len >= sizeof(identifier)) id_len = sizeof(identifier) - 1;
    strncpy(identifier, p, id_len);
    identifier[id_len] = '\0';

    const char* subpath = slash + 1;
    if (!*subpath) return false;

    char cache_dir[MAX_PATH_LEN];
    if (g_config.config_dir[0]) {
        fs_join_path(cache_dir, sizeof(cache_dir), g_config.config_dir, "catalog_cache");
    } else {
        char* home = getenv("HOME");
        if (home) snprintf(cache_dir, sizeof(cache_dir), "%s/.config/retro_setup/catalog_cache", home);
        else snprintf(cache_dir, sizeof(cache_dir), "catalog_cache");
    }
    char cache_path[MAX_PATH_LEN];
    fs_join_path(cache_path, sizeof(cache_path), cache_dir, identifier);
    strncat(cache_path, ".json", sizeof(cache_path) - strlen(cache_path) - 1);

    char* json_text = NULL;
    FILE* f = fopen(cache_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz > 0 && fseek(f, 0, SEEK_SET) == 0) {
            json_text = malloc((size_t)sz + 1);
            if (json_text) {
                size_t read_bytes = fread(json_text, 1, (size_t)sz, f);
                json_text[read_bytes] = 0;
            }
        }
        fclose(f);
    }

    if (!json_text) {
        char meta_url[512];
        snprintf(meta_url, sizeof(meta_url), "https://archive.org/metadata/%s", identifier);
        CURL* curl = curl_easy_init();
        if (curl) {
            DownloadMemoryBuffer buf = {0};
            curl_easy_setopt(curl, CURLOPT_URL, meta_url);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, RETRO_SETUP_USER_AGENT);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_download_response);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
            if (curl_easy_perform(curl) == CURLE_OK && buf.data) {
                json_text = buf.data;
            } else {
                free(buf.data);
            }
            curl_easy_cleanup(curl);
        }
    }

    if (!json_text) return false;

    json_object* root = json_tokener_parse(json_text);
    free(json_text);
    if (!root) return false;

    json_object* dir_obj = NULL;
    json_object* server_obj = NULL;
    json_object* workable_obj = NULL;

    const char* dir = NULL;
    if (json_object_object_get_ex(root, "dir", &dir_obj)) {
        dir = json_object_get_string(dir_obj);
    }

    int count = 0;

    if (json_object_object_get_ex(root, "workable_servers", &workable_obj) &&
        json_object_is_type(workable_obj, json_type_array)) {
        int wcount = (int)json_object_array_length(workable_obj);
        for (int i = 0; i < wcount && count < MAX_FALLBACK_URLS; ++i) {
            json_object* wserver = json_object_array_get_idx(workable_obj, (size_t)i);
            const char* sname = json_object_get_string(wserver);
            if (sname && *sname) {
                if (dir && *dir) {
                    snprintf(fallback_urls[count++], 1024, "https://%s%s/%s", sname, dir, subpath);
                } else {
                    snprintf(fallback_urls[count++], 1024, "https://%s/items/%s/%s", sname, identifier, subpath);
                }
            }
        }
    }

    if (json_object_object_get_ex(root, "server", &server_obj)) {
        const char* sname = json_object_get_string(server_obj);
        if (sname && *sname && count < MAX_FALLBACK_URLS) {
            char primary_url[1024];
            if (dir && *dir) {
                snprintf(primary_url, sizeof(primary_url), "https://%s%s/%s", sname, dir, subpath);
            } else {
                snprintf(primary_url, sizeof(primary_url), "https://%s/items/%s/%s", sname, identifier, subpath);
            }
            bool exists = false;
            for (int i = 0; i < count; ++i) {
                if (strcmp(fallback_urls[i], primary_url) == 0) { exists = true; break; }
            }
            if (!exists) {
                snprintf(fallback_urls[count++], 1024, "%s", primary_url);
            }
        }
    }

    json_object_put(root);
    *fallback_count = count;
    return count > 0;
}

static CURLcode perform_transfer(const char* url, const char* partial, curl_off_t offset,
                                 CurlContext* ctx, long* http_status, char* curl_error) {
    ctx->file = fopen(partial, offset > 0 ? "ab" : "wb");
    if (!ctx->file) { snprintf(curl_error, CURL_ERROR_SIZE, "Cannot open %s: %s", partial, strerror(errno)); return CURLE_WRITE_ERROR; }
    CURL* curl = curl_easy_init();
    if (!curl) { fclose(ctx->file); ctx->file = NULL; return CURLE_FAILED_INIT; }
    ctx->initial_offset = offset;
    ctx->resume_requested = offset > 0;
    ctx->resume_rejected = false;
    ctx->started_ticks = SDL_GetTicks64();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, RETRO_SETUP_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    if (offset > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, offset);
    CURLcode rc = curl_easy_perform(curl);
    fflush(ctx->file);
    fclose(ctx->file);
    ctx->file = NULL;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    curl_off_t speed = 0;
    if (curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &speed) == CURLE_OK)
        ctx->result->speed_bytes_per_sec = (double)speed;
    curl_easy_cleanup(curl);
    return rc;
}

bool download_file(const char* url, const char* destination, DownloadProgressCallback progress_cb,
                   void* user_data, bool* cancel_flag, bool* pause_flag, DownloadResult* out) {
    DownloadResult result;
    memset(&result, 0, sizeof(result));
    result.total_bytes = -1;
    if (!url || !destination) return false;
    int id = manager_claim(url, destination, &result);
    if (id == 0) {
        log_add(result.success ? LOG_LEVEL_INFO : LOG_LEVEL_ERROR,
                result.success ? "[REUSE] Shared asset already completed: %s" : "[FAILED] Shared asset previously failed: %s",
                destination);
        if (out) *out = result;
        return result.success != 0;
    }
    result.task_id = id;
    if (id < 0) { snprintf(result.error, sizeof(result.error), "Download queue is full"); if (out) *out = result; return false; }

    char dir[2048], partial[2048];
    snprintf(dir, sizeof(dir), "%s", destination);
    char* slash = strrchr(dir, '/');
    if (slash) { *slash = 0; fs_mkdir_p(dir); }
    snprintf(partial, sizeof(partial), "%s.part", destination);
    manager_update(id, DOWNLOAD_CHECKING, 0, -1, 0, 0, NULL);

    RemoteInfo remote;
    char head_error[256] = {0};
    bool have_remote = query_remote(url, &remote, head_error, sizeof(head_error));
    curl_off_t final_size = (curl_off_t)fs_file_size(destination);
    curl_off_t part_size = (curl_off_t)fs_file_size(partial);
    manager_lock();
    DownloadTask* task = task_by_id_unlocked(id);
    if (task) { task->local_size = part_size > 0 ? part_size : final_size; task->can_resume = part_size > 0; }
    manager_unlock();

    /* A number of file hosts (notably Archive.org mirrors) intermittently fail
       HEAD requests while the corresponding GET remains available.  A 4xx is
       useful evidence that the configured object is unavailable, but a 5xx
       from this optional probe must not prevent the real transfer attempt. */
    if (!have_remote && remote.status >= 400 && remote.status < 500 &&
        remote.status != 405) {
        result.http_status = remote.status;
        snprintf(result.error, sizeof(result.error), "HTTP %ld: URL check failed", remote.status);
        manager_update(id, DOWNLOAD_FAILED, 0, remote.size, 0, remote.status, result.error);
        if (out) *out = result;
        return false;
    }
    char existing_error[256] = {0};
    if (final_size > 0 && have_remote && remote.size >= 0 && final_size == remote.size &&
        validate_for_destination(destination, destination, existing_error, sizeof(existing_error))) {
        result.success = 1; result.state = DOWNLOAD_SKIPPED; result.http_status = remote.status;
        result.downloaded_bytes = result.total_bytes = final_size;
        manager_update(id, DOWNLOAD_SKIPPED, final_size, final_size, 0, remote.status, NULL);
        log_add(LOG_LEVEL_INFO, "[DOWNLOAD] %s\nAlready complete - skipped (%.1f MB)", destination, (double)final_size / 1048576.0);
        if (out) *out = result;
        return true;
    }
    if (final_size > 0) {
        if (have_remote && remote.size >= 0 && final_size == remote.size)
            log_add(LOG_LEVEL_WARN, "Existing file failed validation (%s); restarting: %s", existing_error, destination);
        if (!have_remote || remote.size < 0) {
            log_add(LOG_LEVEL_WARN, "Cannot validate existing file; moving it to .part for verification by transfer: %s", destination);
            if (part_size == 0) rename(destination, partial);
        } else {
            log_add(LOG_LEVEL_WARN, "Existing final file has wrong size; restarting: %s", destination);
            remove(destination);
        }
    }
    part_size = (curl_off_t)fs_file_size(partial);
    char partial_error[256] = {0};
    if (have_remote && remote.size >= 0 && part_size == remote.size && part_size > 0 &&
        validate_for_destination(partial, destination, partial_error, sizeof(partial_error))) {
        manager_update(id, DOWNLOAD_VERIFYING, part_size, remote.size, 0, remote.status, NULL);
        if (rename(partial, destination) == 0) {
            result.success = 1; result.state = DOWNLOAD_COMPLETED;
            result.downloaded_bytes = result.total_bytes = part_size;
            manager_update(id, DOWNLOAD_COMPLETED, part_size, remote.size, 0, remote.status, NULL);
            if (out) *out = result;
            return true;
        }
    }
    if (have_remote && remote.size >= 0 && part_size == remote.size && part_size > 0) {
        log_add(LOG_LEVEL_WARN, "Complete partial failed validation (%s); restarting: %s", partial_error, partial);
        remove(partial);
        part_size = 0;
    }
    if (have_remote && remote.size >= 0 && part_size > remote.size) {
        log_add(LOG_LEVEL_WARN, "[DOWNLOAD] Partial is larger than remote; restarting %s", partial);
        remove(partial); part_size = 0;
    }

    char active_url[1024];
    snprintf(active_url, sizeof(active_url), "%s", url);

    char fallback_urls[MAX_FALLBACK_URLS][1024];
    int fallback_count = 0;
    int fallback_idx = -1;
    bool fallbacks_resolved = false;

    char curl_error[CURL_ERROR_SIZE] = {0};
    long http = 0;
    CURLcode rc = CURLE_OK;
    int max_attempts = MAX_DOWNLOAD_RETRIES;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (cancel_flag && *cancel_flag) {
            rc = CURLE_ABORTED_BY_CALLBACK;
            break;
        }

        part_size = (curl_off_t)fs_file_size(partial);
        if (part_size > 0)
            log_add(LOG_LEVEL_INFO, "[RESUME] %s\nRemote size: %.1f MB\nLocal partial: %.1f MB\nResuming from byte %lld (attempt %d/%d)",
                    destination, remote.size > 0 ? (double)remote.size / 1048576.0 : 0.0,
                    (double)part_size / 1048576.0, (long long)part_size, attempt, max_attempts);
        else
            log_add(LOG_LEVEL_INFO, "[DOWNLOAD] %s\nStarted (attempt %d/%d)\n0 B / %s", destination,
                    attempt, max_attempts, remote.size > 0 ? "known remote size" : "unknown size");

        CurlContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.progress_cb = progress_cb; ctx.user_data = user_data; ctx.cancel_flag = cancel_flag;
        ctx.pause_flag = pause_flag; ctx.result = &result; ctx.task_id = id;
        memset(curl_error, 0, sizeof(curl_error));
        http = 0;

        rc = perform_transfer(active_url, partial, part_size, &ctx, &http, curl_error);

        if (ctx.resume_rejected) {
            log_add(LOG_LEVEL_WARN, "Server ignored HTTP Range for %s; restarting safely.", destination);
            remove(partial);
            part_size = 0;
            memset(curl_error, 0, sizeof(curl_error));
            rc = perform_transfer(active_url, partial, 0, &ctx, &http, curl_error);
        }

        result.http_status = http;
        curl_off_t received = (curl_off_t)fs_file_size(partial);

        if (rc == CURLE_OK && (http == 200 || http == 206)) {
            if (received <= 0 || !fs_is_file(partial)) {
                snprintf(result.error, sizeof(result.error), "Downloaded file is missing or empty");
            } else if (have_remote && remote.size >= 0 && received != remote.size) {
                snprintf(result.error, sizeof(result.error), "Size mismatch: got %lld, expected %lld", (long long)received, (long long)remote.size);
            } else {
                char validation_error[256] = {0};
                if (!validate_for_destination(partial, destination, validation_error, sizeof(validation_error))) {
                    snprintf(result.error, sizeof(result.error), "Invalid download: %.220s", validation_error);
                    remove(partial);
                } else if (rename(partial, destination) == 0) {
                    result.success = 1; result.state = DOWNLOAD_COMPLETED;
                    result.downloaded_bytes = received;
                    result.total_bytes = remote.size >= 0 ? remote.size : received;
                    manager_update(id, DOWNLOAD_COMPLETED, received, result.total_bytes, result.speed_bytes_per_sec, http, NULL);
                    log_add(LOG_LEVEL_INFO, "[DONE] %s\n%.1f MB\nAverage speed: %.1f MB/s", destination,
                            (double)received / 1048576.0, result.speed_bytes_per_sec / 1048576.0);
                    if (out) *out = result;
                    return true;
                } else {
                    snprintf(result.error, sizeof(result.error), "Cannot atomically rename .part: %s", strerror(errno));
                }
            }
        } else if (http == 416 && have_remote && remote.size >= 0 && received == remote.size &&
                   validate_for_destination(partial, destination, result.error, sizeof(result.error)) &&
                   rename(partial, destination) == 0) {
            result.success = 1; result.state = DOWNLOAD_COMPLETED; result.downloaded_bytes = result.total_bytes = received;
            manager_update(id, DOWNLOAD_COMPLETED, received, received, 0, http, NULL);
            if (out) *out = result;
            return true;
        } else if (rc == CURLE_ABORTED_BY_CALLBACK && cancel_flag && *cancel_flag) {
            result.state = DOWNLOAD_CANCELLED;
            snprintf(result.error, sizeof(result.error), "Download interrupted; partial file preserved");
            manager_update(id, DOWNLOAD_CANCELLED, received, remote.size, 0, http, result.error);
            log_add(LOG_LEVEL_WARN, "Download interrupted.\nPartial file preserved: %s\n%.1f MB downloaded.\nNEXT RUN WILL RESUME FROM THIS POSITION.", partial, (double)received / 1048576.0);
            if (out) *out = result;
            return false;
        }

        if (!result.error[0]) snprintf(result.error, sizeof(result.error), "HTTP %ld: %.220s", http, curl_error[0] ? curl_error : curl_easy_strerror(rc));

        /* If transfer failed on an Archive.org download URL, resolve and switch to direct storage server */
        if (!fallbacks_resolved && (http >= 500 || http == 429 || http == 0 || rc != CURLE_OK)) {
            fallbacks_resolved = resolve_archive_fallbacks(url, fallback_urls, &fallback_count);
        }

        if (fallback_count > 0 && fallback_idx + 1 < fallback_count) {
            fallback_idx++;
            snprintf(active_url, sizeof(active_url), "%s", fallback_urls[fallback_idx]);
            log_add(LOG_LEVEL_WARN, "[FALLBACK] Archive mirror returned HTTP %ld; switching to direct server: %s",
                    http, active_url);
            continue;
        }

        if (attempt < max_attempts) {
            int delay_seconds = attempt * 2;
            log_add(LOG_LEVEL_WARN, "[RETRY] Attempt %d/%d failed (%s). Retrying in %d seconds...",
                    attempt, max_attempts, result.error, delay_seconds);
            for (int s = 0; s < delay_seconds * 10; ++s) {
                if (cancel_flag && *cancel_flag) break;
                SDL_Delay(100);
            }
        }
    }

    curl_off_t received = (curl_off_t)fs_file_size(partial);
    if (!result.error[0]) snprintf(result.error, sizeof(result.error), "HTTP %ld: %.220s", http, curl_error[0] ? curl_error : curl_easy_strerror(rc));
    if (http >= 400 && received == 0) remove(partial);
    result.state = DOWNLOAD_FAILED;
    manager_update(id, DOWNLOAD_FAILED, received, remote.size, 0, http, result.error);
    log_add(LOG_LEVEL_ERROR, "[FAILED] %s: %s", destination, result.error);
    if (out) *out = result;
    return false;
}
