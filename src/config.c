#include "config.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void set_defaults(ServerConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    safe_strcpy(cfg->root_dir, sizeof(cfg->root_dir), "/var/tftp");
    safe_strcpy(cfg->log_dir, sizeof(cfg->log_dir), "/var/tftp/logs");

    cfg->num_listeners = 1;
    safe_strcpy(cfg->listeners[0].addr, sizeof(cfg->listeners[0].addr), "0.0.0.0");
    cfg->listeners[0].port = 69;

    cfg->event_udp_host[0] = '\0';
    cfg->event_udp_port = 0;

    cfg->event_http_host[0] = '\0';
    cfg->event_http_port = 0;
    cfg->event_http_path[0] = '\0';

    cfg->timeout_sec = 3;
    cfg->max_retries = 5;
    cfg->log_level = 1; /* info */
}

static void parse_listeners(ServerConfig *cfg, const char *val) {
    /* Format: ip:port,ip:port,... */
    char buf[512];
    safe_strcpy(buf, sizeof(buf), val);
    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    int count = 0;

    while (token && count < MAX_LISTENERS) {
        trim(token);
        char *colon = strchr(token, ':');
        if (!colon) {
            token = strtok_r(NULL, ",", &saveptr);
            continue;
        }
        *colon = '\0';
        const char *ip = token;
        const char *port_str = colon + 1;
        int port = 0;
        if (parse_int(port_str, &port) != 0) {
            token = strtok_r(NULL, ",", &saveptr);
            continue;
        }
        safe_strcpy(cfg->listeners[count].addr, sizeof(cfg->listeners[count].addr), ip);
        cfg->listeners[count].port = port;
        count++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    if (count > 0) cfg->num_listeners = count;
}

static void parse_udp(ServerConfig *cfg, const char *val) {
    /* Format: host:port */
    char buf[256];
    safe_strcpy(buf, sizeof(buf), val);
    trim(buf);
    if (buf[0] == '\0') return;
    char *colon = strchr(buf, ':');
    if (!colon) return;
    *colon = '\0';
    const char *host = buf;
    const char *port_str = colon + 1;
    int port = 0;
    if (parse_int(port_str, &port) != 0) return;
    safe_strcpy(cfg->event_udp_host, sizeof(cfg->event_udp_host), host);
    cfg->event_udp_port = port;
}

static void parse_http_url(ServerConfig *cfg, const char *val) {
    /* Only http://host[:port]/path */
    char buf[512];
    safe_strcpy(buf, sizeof(buf), val);
    trim(buf);
    if (buf[0] == '\0') return;
    if (!starts_with(buf, "http://")) return;

    char *p = buf + strlen("http://");
    char *slash = strchr(p, '/');
    char hostport[256] = {0};
    char path[256] = {0};

    if (slash) {
        *slash = '\0';
        safe_strcpy(hostport, sizeof(hostport), p);
        safe_strcpy(path, sizeof(path), slash + 1);
    } else {
        safe_strcpy(hostport, sizeof(hostport), p);
        safe_strcpy(path, sizeof(path), "");
    }

    char *colon = strchr(hostport, ':');
    int port = 80;
    char host[256] = {0};

    if (colon) {
        *colon = '\0';
        safe_strcpy(host, sizeof(host), hostport);
        if (parse_int(colon + 1, &port) != 0) port = 80;
    } else {
        safe_strcpy(host, sizeof(host), hostport);
    }

    safe_strcpy(cfg->event_http_host, sizeof(cfg->event_http_host), host);
    cfg->event_http_port = port;
    char path_with_slash[256];
    if (path[0] == '\0') {
        safe_strcpy(path_with_slash, sizeof(path_with_slash), "/");
    } else {
        snprintf(path_with_slash, sizeof(path_with_slash), "/%s", path);
    }
    safe_strcpy(cfg->event_http_path, sizeof(cfg->event_http_path), path_with_slash);
}

int load_config(const char *path, ServerConfig *cfg) {
    set_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) {
        /* Use defaults if file missing */
        return 0;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0')
            continue;

        char *key = NULL;
        char *val = NULL;
        if (split_kv(line, &key, &val) != 0) continue;

        if (strcmp(key, "root_dir") == 0) {
            safe_strcpy(cfg->root_dir, sizeof(cfg->root_dir), val);
        } else if (strcmp(key, "log_dir") == 0) {
            safe_strcpy(cfg->log_dir, sizeof(cfg->log_dir), val);
        } else if (strcmp(key, "listeners") == 0) {
            parse_listeners(cfg, val);
        } else if (strcmp(key, "event_udp") == 0) {
            parse_udp(cfg, val);
        } else if (strcmp(key, "event_http_url") == 0) {
            parse_http_url(cfg, val);
        } else if (strcmp(key, "timeout_sec") == 0) {
            int v;
            if (parse_int(val, &v) == 0 && v > 0) cfg->timeout_sec = v;
        } else if (strcmp(key, "max_retries") == 0) {
            int v;
            if (parse_int(val, &v) == 0 && v > 0) cfg->max_retries = v;
        } else if (strcmp(key, "log_level") == 0) {
            if (strcmp(val, "error") == 0) cfg->log_level = 0;
            else if (strcmp(val, "info") == 0) cfg->log_level = 1;
            else if (strcmp(val, "debug") == 0) cfg->log_level = 2;
        }
    }

    fclose(f);
    return 0;
}
