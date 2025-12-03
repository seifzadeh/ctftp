#include "logger.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static FILE *g_log_fp = NULL;
static int g_log_level = 1;

int logger_init(const ServerConfig *cfg) {
    g_log_level = cfg->log_level;
    char path[1024];
    snprintf(path, sizeof(path), "%s/ctftp.log", cfg->log_dir);
    g_log_fp = fopen(path, "a");
    if (!g_log_fp) {
        /* Fallback to stderr */
        g_log_fp = stderr;
        return -1;
    }
    return 0;
}

void logger_close(void) {
    if (g_log_fp && g_log_fp != stderr) {
        fclose(g_log_fp);
    }
    g_log_fp = NULL;
}

void log_msg(LogLevel level, const char *fmt, ...) {
    if (!g_log_fp) return;
    if (level > g_log_level) return;

    char ts[32];
    now_iso8601(ts, sizeof(ts));

    const char *lvl = "INFO";
    if (level == LOG_ERROR) lvl = "ERROR";
    else if (level == LOG_DEBUG) lvl = "DEBUG";

    fprintf(g_log_fp, "[%s] [%s] ", ts, lvl);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_fp, fmt, ap);
    va_end(ap);

    fprintf(g_log_fp, "\n");
    fflush(g_log_fp);
}
