#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

#define MAX_LOG_LINES 500
#define LOG_LINE_LEN 512

void log_init(void);
void log_clear(void);
void log_add(LogLevel level, const char* fmt, ...);

int log_get_count(void);
const char* log_get_line(int index);
const char* log_get_last_line(void);

#endif // LOG_H
