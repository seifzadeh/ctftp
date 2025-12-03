#ifndef LOGGER_H
#define LOGGER_H

#include "config.h"
#include <stdarg.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_INFO  = 1,
    LOG_DEBUG = 2
} LogLevel;

int logger_init(const ServerConfig *cfg);
void logger_close(void);
void log_msg(LogLevel level, const char *fmt, ...);

#endif
