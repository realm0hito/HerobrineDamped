#pragma once

#include "base/base.h"

typedef enum LogLevel { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR, LOG_LEVEL_COUNT } LogLevel;

void log_init(LogLevel min_level);
void log_set_level(LogLevel min_level);
void log_write(LogLevel level, const char *file, int line, const char *fmt, ...) PRINTF_FMT(4, 5);
u64 log_count(LogLevel level); // messages emitted at this level since log_init

#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
