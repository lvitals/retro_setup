#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static char g_log_lines[MAX_LOG_LINES][LOG_LINE_LEN];
static int g_log_count = 0;

void log_init(void) {
    g_log_count = 0;
    memset(g_log_lines, 0, sizeof(g_log_lines));
}

void log_clear(void) {
    g_log_count = 0;
}

void log_add(LogLevel level, const char* fmt, ...) {
    char buf[LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Strip trailing newlines
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = 0;
    }
    if (len == 0) return;

    // Print to stdout
    const char* prefix = "";
    switch (level) {
        case LOG_LEVEL_DEBUG: prefix = "[DEBUG] "; break;
        case LOG_LEVEL_INFO:  prefix = "[INFO] "; break;
        case LOG_LEVEL_WARN:  prefix = "[WARN] "; break;
        case LOG_LEVEL_ERROR: prefix = "[ERROR] "; break;
    }
    printf("%s%s\n", prefix, buf);
    fflush(stdout);

    if (g_log_count < MAX_LOG_LINES) {
        snprintf(g_log_lines[g_log_count++], LOG_LINE_LEN, "%s", buf);
    } else {
        // Shift log lines up
        for (int i = 0; i < MAX_LOG_LINES - 1; i++) {
            snprintf(g_log_lines[i], LOG_LINE_LEN, "%s", g_log_lines[i + 1]);
        }
        snprintf(g_log_lines[MAX_LOG_LINES - 1], LOG_LINE_LEN, "%s", buf);
    }
}

int log_get_count(void) {
    return g_log_count;
}

const char* log_get_line(int index) {
    if (index < 0 || index >= g_log_count) return "";
    return g_log_lines[index];
}

const char* log_get_last_line(void) {
    if (g_log_count == 0) return "";
    return g_log_lines[g_log_count - 1];
}
