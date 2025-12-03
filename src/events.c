#include "events.h"
#include "logger.h"
#include "util.h"

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#define EVENT_QUEUE_CAP 256

static ServerConfig g_cfg;
static int g_udp_sock = -1;

static Event g_queue[EVENT_QUEUE_CAP];
static int g_q_head = 0;
static int g_q_tail = 0;
static int g_stop = 0;
static pthread_mutex_t g_q_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_q_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_http_thread;
static int g_http_thread_started = 0;

/* Simple ring buffer push */
static void queue_push(const Event *ev) {
    pthread_mutex_lock(&g_q_mutex);
    int next_tail = (g_q_tail + 1) % EVENT_QUEUE_CAP;
    if (next_tail == g_q_head) {
        /* queue full, drop oldest */
        g_q_head = (g_q_head + 1) % EVENT_QUEUE_CAP;
    }
    g_queue[g_q_tail] = *ev;
    g_q_tail = next_tail;
    pthread_cond_signal(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);
}

/* Pop from queue, return 0 if got one, -1 on stop */
static int queue_pop(Event *out) {
    pthread_mutex_lock(&g_q_mutex);
    while (!g_stop && g_q_head == g_q_tail) {
        pthread_cond_wait(&g_q_cond, &g_q_mutex);
    }
    if (g_stop) {
        pthread_mutex_unlock(&g_q_mutex);
        return -1;
    }
    *out = g_queue[g_q_head];
    g_q_head = (g_q_head + 1) % EVENT_QUEUE_CAP;
    pthread_mutex_unlock(&g_q_mutex);
    return 0;
}

/* Send UDP JSON event */
static void send_udp_event(const Event *ev) {
    if (g_udp_sock < 0 || g_cfg.event_udp_port == 0 || g_cfg.event_udp_host[0] == '\0')
        return;

    char json[512];
    snprintf(json, sizeof(json),
             "{\"type\":%d,\"client_ip\":\"%s\",\"client_port\":%d,"
             "\"filename\":\"%s\",\"bytes\":%zu,"
             "\"status\":\"%s\",\"message\":\"%s\","
             "\"start\":\"%s\",\"end\":\"%s\"}",
             ev->type, ev->client_ip, ev->client_port,
             ev->filename, ev->bytes,
             ev->status, ev->message,
             ev->start_ts, ev->end_ts);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_cfg.event_udp_port);
    if (inet_pton(AF_INET, g_cfg.event_udp_host, &addr.sin_addr) != 1) {
        return;
    }

    sendto(g_udp_sock, json, strlen(json), 0,
           (struct sockaddr *)&addr, sizeof(addr));
}

/* Simple blocking HTTP POST sender */
static void send_http_event(const Event *ev) {
    if (g_cfg.event_http_host[0] == '\0' || g_cfg.event_http_port == 0)
        return;

    char body[512];
    snprintf(body, sizeof(body),
             "{\"type\":%d,\"client_ip\":\"%s\",\"client_port\":%d,"
             "\"filename\":\"%s\",\"bytes\":%zu,"
             "\"status\":\"%s\",\"message\":\"%s\","
             "\"start\":\"%s\",\"end\":\"%s\"}",
             ev->type, ev->client_ip, ev->client_port,
             ev->filename, ev->bytes,
             ev->status, ev->message,
             ev->start_ts, ev->end_ts);

    char request[1024];
    int body_len = (int)strlen(body);
    snprintf(request, sizeof(request),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             g_cfg.event_http_path[0] ? g_cfg.event_http_path : "/",
             g_cfg.event_http_host,
             body_len,
             body);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_cfg.event_http_port);
    if (inet_pton(AF_INET, g_cfg.event_http_host, &addr.sin_addr) != 1) {
        close(sock);
        return;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return;
    }

    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent <= 0) {
        close(sock);
        return;
    }

    char buf[256];
    while (recv(sock, buf, sizeof(buf), 0) > 0) {
        /* ignore response */
    }
    close(sock);
}

static void *http_thread_main(void *arg) {
    (void)arg;
    Event ev;
    while (queue_pop(&ev) == 0) {
        send_http_event(&ev);
    }
    return NULL;
}

int events_init(const ServerConfig *cfg) {
    g_cfg = *cfg;

    if (cfg->event_udp_port > 0 && cfg->event_udp_host[0] != '\0') {
        g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (g_udp_sock < 0) {
            log_msg(LOG_ERROR, "Failed to create UDP event socket: %s", strerror(errno));
        }
    }

    if (cfg->event_http_host[0] != '\0' && cfg->event_http_port > 0) {
        if (pthread_create(&g_http_thread, NULL, http_thread_main, NULL) == 0) {
            g_http_thread_started = 1;
        } else {
            log_msg(LOG_ERROR, "Failed to start HTTP event thread");
        }
    }

    return 0;
}

void events_shutdown(void) {
    pthread_mutex_lock(&g_q_mutex);
    g_stop = 1;
    pthread_cond_broadcast(&g_q_cond);
    pthread_mutex_unlock(&g_q_mutex);

    if (g_http_thread_started) {
        pthread_join(g_http_thread, NULL);
    }

    if (g_udp_sock >= 0) {
        close(g_udp_sock);
        g_udp_sock = -1;
    }
}

void event_emit(const Event *ev) {
    /* always log to main logger */
    log_msg(LOG_INFO,
            "EVENT type=%d client=%s:%d file=\"%s\" bytes=%zu status=%s msg=%s",
            ev->type, ev->client_ip, ev->client_port,
            ev->filename, ev->bytes, ev->status, ev->message);

    /* UDP event */
    send_udp_event(ev);

    /* HTTP event */
    if (g_cfg.event_http_host[0] != '\0' && g_cfg.event_http_port > 0) {
        queue_push(ev);
    }
}
