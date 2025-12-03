#ifndef CONFIG_H
#define CONFIG_H

#include <limits.h>

#define MAX_LISTENERS 8

typedef struct {
    char addr[64];
    int  port;
} ListenerConfig;

typedef struct {
    char root_dir[PATH_MAX];
    char log_dir[PATH_MAX];

    int  num_listeners;
    ListenerConfig listeners[MAX_LISTENERS];

    char event_udp_host[64];
    int  event_udp_port;

    char event_http_host[128];
    int  event_http_port;
    char event_http_path[128];

    int  timeout_sec;
    int  max_retries;
    int  log_level;  /* 0=error,1=info,2=debug */
} ServerConfig;

int load_config(const char *path, ServerConfig *cfg);

#endif
