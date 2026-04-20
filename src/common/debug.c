#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "debug.h"

int g_debug_level = LOG_ERROR;
FILE *g_log_file = NULL;
const char *lv_str[] = {"ERROR", "INFO ", "DEBUG"};
extern char *program_invocation_short_name;

void log_log(log_level_t level, const char *file, const char *func, int line, const char *fmt, ...)
{
    int res, i, cnt = 0;
    char timestamp[64];
    struct timeval tv;
    struct tm *t;
    FILE *redirect[2];

    if (level > g_debug_level) {
        return;
    }

    va_list args;

    res = gettimeofday(&tv, NULL);
    if (res < 0)
        err(EXIT_FAILURE, "gettimeofday failed");
    t = localtime(&tv.tv_sec);
    if (t == NULL)
        err(EXIT_FAILURE, "localtime failed");

    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", t);

    if (level == LOG_ERROR)
        redirect[cnt++] = stderr;

    if (g_log_file)
        redirect[cnt++] = g_log_file;
    else if (level != LOG_ERROR)
        redirect[cnt++] = stdout;

    for (i = 0; i < cnt; i++) {
        FILE *f = redirect[i];

        fprintf(f, "%s:%02ld ", timestamp, tv.tv_usec / 1000);
        fprintf(f, "[%s][", program_invocation_short_name);
        if (f != g_log_file && level == LOG_ERROR)
            fprintf(f, "\x1b[31m%s\x1b[0m", lv_str[level]);
        else
            fprintf(f, "%s", lv_str[level]);
        fprintf(f, "][%s:%s:%d] ", file, func, line);
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");

        if (f != g_log_file && level == LOG_ERROR)
            fflush(f);
    }
}

void set_log_file(const char *filename)
{
    if (g_log_file) {
        fclose(g_log_file);
    }
    g_log_file = fopen(filename, "a");
    if (!g_log_file) {
        fprintf(stderr, "failed to open log file %s: %s\n", filename, strerror(errno));
        g_log_file = NULL;
    }
}