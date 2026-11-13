#include "base/log.h"
#include "base/os.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Single-threaded until M3; the job system adds a lock around output.
static struct {
    LogLevel min_level;
    u64 start_ns;
    u64 counts[LOG_LEVEL_COUNT];
} g_log;

static const char *const LEVEL_TAG[LOG_LEVEL_COUNT] = { "D", "I", "W", "E" };

static const char *file_basename(const char *path)
{
    const char *s = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') s = p + 1;
    }
    return s;
}

static void log_vwrite(const char *tag, const char *file, int line, const char *fmt, va_list args)
{
    f64 t = g_log.start_ns ? (f64)(os_time_ns() - g_log.start_ns) * 1e-9 : 0.0;
    fprintf(stderr, "[%9.3f] %s %s:%d: ", t, tag, file_basename(file), line);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void log_init(LogLevel min_level)
{
    memset(&g_log, 0, sizeof(g_log));
    g_log.min_level = min_level;
    g_log.start_ns = os_time_ns();
}

void log_set_level(LogLevel min_level) { g_log.min_level = min_level; }

u64 log_count(LogLevel level) { return g_log.counts[level]; }

void log_write(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    g_log.counts[level]++;
    if (level < g_log.min_level) return;
    va_list args;
    va_start(args, fmt);
    log_vwrite(LEVEL_TAG[level], file, line, fmt, args);
    va_end(args);
    if (level >= LOG_LEVEL_WARN) fflush(stderr);
}

void panic(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vwrite("FATAL", file, line, fmt, args);
    va_end(args);
    fflush(stderr);
#if defined(HD_DEBUG)
    DEBUG_BREAK();
#endif
    abort();
}
