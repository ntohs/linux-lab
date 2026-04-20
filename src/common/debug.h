#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

typedef enum {
    LOG_ERROR,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

extern int g_debug_level;
extern FILE *g_log_file;
void log_log(log_level_t level, const char *file, const char *func, int line, const char *fmt, ...);
void set_log_file(const char *filename);

#define log_error(fmt, ...) log_log(LOG_ERROR, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) log_log(LOG_INFO, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) log_log(LOG_DEBUG, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

#endif