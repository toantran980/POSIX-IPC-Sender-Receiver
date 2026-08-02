/**
 * @file log.c
 * @brief Logging module implementation with timestamps and log levels.
 * @author Toan Tran
 * @version 1.0.0
 */

#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===================================================================
 * Internal State
 * =================================================================== */

/** Current log level threshold */
static int g_log_level = LOG_LEVEL_INFO;

/** Whether quiet mode is enabled */
static bool g_quiet = false;

/** File handle for optional log file output */
static FILE *g_log_file = NULL;

/** Whether the module has been initialized */
static bool g_initialized = false;

/* ===================================================================
 * Level Name Mapping
 * =================================================================== */

static const char *log_level_name(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default:              return "UNKNOWN";
    }
}

/* ===================================================================
 * Public API Implementation
 * =================================================================== */

int log_init(int level, const char *logFile, bool quiet) {
    g_log_level = level;
    g_quiet = quiet;
    g_log_file = NULL;
    g_initialized = true;

    if (logFile != NULL) {
        g_log_file = fopen(logFile, "a");
        if (g_log_file == NULL) {
            fprintf(stderr, "[ERROR] Failed to open log file '%s'\n", logFile);
            return -1;
        }
        setvbuf(g_log_file, NULL, _IOLBF, 0); /* line-buffered */
    }

    return 0;
}

void log_shutdown(void) {
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_initialized = false;
}

void log_set_level(int level) {
    g_log_level = level;
}

int log_get_level(void) {
    return g_log_level;
}

void log_set_quiet(bool quiet) {
    g_quiet = quiet;
}

void log_log(int level, const char *format, ...) {
    if (!g_initialized) {
        /* Auto-initialize with defaults if log_log is called before log_init */
        log_init(LOG_LEVEL_INFO, NULL, false);
    }

    /* Suppress messages above threshold */
    if (level > g_log_level) {
        return;
    }

    /* Suppress INFO and DEBUG if quiet mode */
    if (g_quiet && level <= LOG_LEVEL_INFO) {
        return;
    }

    /* Get current time */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    /* Format the user message */
    va_list args;
    va_start(args, format);

    /* ERROR and WARN always go to stderr; INFO/DEBUG to stderr or log file */
    FILE *out = stderr;

    /* Write to log file if configured */
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] [%s] ", timebuf, log_level_name(level));
        vfprintf(g_log_file, format, args);
        fputc('\n', g_log_file);
        /* Re-format for stderr if also needed */
        va_end(args);
        va_start(args, format);
    }

    fprintf(out, "[%s] [%s] ", timebuf, log_level_name(level));
    vfprintf(out, format, args);
    fputc('\n', out);

    va_end(args);
}
