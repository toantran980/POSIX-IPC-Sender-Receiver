/**
 * @file log.h
 * @brief Logging module with levels, timestamps, and optional file output.
 * @author Toan Tran
 * @version 1.0.0
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4

int log_init(int level, const char *logFile, bool quiet);
void log_shutdown(void);
void log_set_level(int level);
int log_get_level(void);
void log_set_quiet(bool quiet);
void log_log(int level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#define log_error(...)   log_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_warn(...)    log_log(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_info(...)    log_log(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_debug(...)   log_log(LOG_LEVEL_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
