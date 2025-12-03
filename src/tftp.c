#include "tftp.h"
#include "logger.h"
#include "events.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>

#define TFTP_OPCODE_RRQ  1
#define TFTP_OPCODE_WRQ  2
#define TFTP_OPCODE_DATA 3
#define TFTP_OPCODE_ACK  4
#define TFTP_OPCODE_ERR  5

#define TFTP_DATA_SIZE   512

typedef struct {
    char bind_addr[64];
    int  bind_port;
} ListenerArg;

typedef struct {
    char bind_addr[64];
    int  bind_port;
    char client_ip[64];
    int  client_port;
    char filename[256];
} SessionArg;

static ServerConfig g_cfg;

/* Basic filename sanitization */
static void sanitize_filename(char *dst, size_t dst_size, const char *src) {
    /* Remove leading slashes and ".." segments */
    char tmp[256];
    safe_strcpy(tmp, sizeof(tmp), src);

    /* strip leading '/' */
    while (tmp[0] == '/') {
        memmove(tmp, tmp + 1, strlen(tmp));
    }

    /* remove ".." very simply */
    if (strstr(tmp, "..")) {
        /* dangerous path */
        dst[0] = '\0';
        return;
    }

    safe_strcpy(dst, dst_size, tmp);
}

/* Build full path to requested file */
static void build_file_path(char *out, size_t out_size, const char *filename) {
    snprintf(out, out_size, "%s/%s", g_cfg.root_dir, filename);
}

/* Build per-request log file path (same directory, .log suffix) */
static void build_request_log_path(char *out, size_t out_size, const char *filename) {
    snprintf(out, out_size, "%s/%s.log", g_cfg.root_dir, filename);
}

/* Write one line into per-request log file */
static void write_request_log(const SessionArg *sa,
                              const char *start_ts,
                              const char *end_ts,
                              size_t bytes,
                              const char *status,
                              const char *msg) {
    char fname_sanitized[256];
    sanitize_filename(fname_sanitized, sizeof(fname_sanitized), sa->filename);
    if (fname_sanitized[0] == '\0') return;

    char path[PATH_MAX];
    build_request_log_path(path, sizeof(path), fname_sanitized);

    FILE *f = fopen(path, "a");
    if (!f) {
        log_msg(LOG_ERROR, "Failed to open per-request log file %s: %s", path, strerror(errno));
        return;
    }

    fprintf(f, "%s;%s;%s;%d;%zu;%s;%s\n",
            start_ts, end_ts,
            sa->client_ip, sa->client_port,
            bytes, status, msg);
    fclose(f);
}

/* Send TFTP ERROR packet */
static void send_error_packet(int sock,
                              const struct sockaddr_in *cliaddr,
                              socklen_t cliaddr_len,
                              uint16_t code,
                              const char *msg) {
    unsigned char buf[516];
    uint16_t opcode = htons(TFTP_OPCODE_ERR);
    uint16_t ecode = htons(code);
    memcpy(buf, &opcode, 2);
    memcpy(buf + 2, &ecode, 2);
    size_t mlen = strlen(msg);
    memcpy(buf + 4, msg, mlen);
    buf[4 + mlen] = '\0';
    sendto(sock, buf, 5 + mlen, 0,
           (const struct sockaddr *)cliaddr, cliaddr_len);
}

/* TFTP session thread */
static void *session_thread_main(void *arg) {
    SessionArg *sa = (SessionArg *)arg;

    char start_ts[32], end_ts[32];
    now_iso8601(start_ts, sizeof(start_ts));

    Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EVT_REQ_START;
    safe_strcpy(ev.client_ip, sizeof(ev.client_ip), sa->client_ip);
    ev.client_port = sa->client_port;
    safe_strcpy(ev.filename, sizeof(ev.filename), sa->filename);
    ev.bytes = 0;
    safe_strcpy(ev.status, sizeof(ev.status), "start");
    safe_strcpy(ev.message, sizeof(ev.message), "RRQ received");
    safe_strcpy(ev.start_ts, sizeof(ev.start_ts), start_ts);
    ev.end_ts[0] = '\0';
    event_emit(&ev);

    char fname_sanitized[256];
    sanitize_filename(fname_sanitized, sizeof(fname_sanitized), sa->filename);
    if (fname_sanitized[0] == '\0') {
        log_msg(LOG_ERROR, "Rejected unsafe filename from %s: \"%s\"",
                sa->client_ip, sa->filename);
        /* We do not have cliaddr here, so use separate socket to send error if needed. */
        goto done;
    }

    char path[PATH_MAX];
    build_file_path(path, sizeof(path), fname_sanitized);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        log_msg(LOG_ERROR, "Failed to open file %s: %s", path, strerror(errno));
        /* We need to inform client with ERROR from a new socket */
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            struct sockaddr_in cli;
            memset(&cli, 0, sizeof(cli));
            cli.sin_family = AF_INET;
            cli.sin_port = htons(sa->client_port);
            inet_pton(AF_INET, sa->client_ip, &cli.sin_addr);
            send_error_packet(sock, &cli, sizeof(cli), 1, "File not found");
            close(sock);
        }
        safe_strcpy(ev.status, sizeof(ev.status), "error");
        safe_strcpy(ev.message, sizeof(ev.message), "file_not_found");
        goto done;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_msg(LOG_ERROR, "Failed to create session socket: %s", strerror(errno));
        close(fd);
        safe_strcpy(ev.status, sizeof(ev.status), "error");
        safe_strcpy(ev.message, sizeof(ev.message), "socket_failed");
        goto done;
    }

    /* Bind local address (IP same as listener, port ephemeral) */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = 0;
    if (inet_pton(AF_INET, sa->bind_addr, &local.sin_addr) != 1) {
        close(fd);
        close(sock);
        safe_strcpy(ev.status, sizeof(ev.status), "error");
        safe_strcpy(ev.message, sizeof(ev.message), "bind_ip_invalid");
        goto done;
    }
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) != 0) {
        log_msg(LOG_ERROR, "Failed to bind session socket: %s", strerror(errno));
        close(fd);
        close(sock);
        safe_strcpy(ev.status, sizeof(ev.status), "error");
        safe_strcpy(ev.message, sizeof(ev.message), "bind_failed");
        goto done;
    }

    struct sockaddr_in cli;
    memset(&cli, 0, sizeof(cli));
    cli.sin_family = AF_INET;
    cli.sin_port = htons(sa->client_port);
    if (inet_pton(AF_INET, sa->client_ip, &cli.sin_addr) != 1) {
        close(fd);
        close(sock);
        safe_strcpy(ev.status, sizeof(ev.status), "error");
        safe_strcpy(ev.message, sizeof(ev.message), "client_ip_invalid");
        goto done;
    }

    uint16_t block = 1;
    ssize_t r;
    unsigned char data_buf[4 + TFTP_DATA_SIZE];
    size_t total_bytes = 0;
    int done_ok = 0;

    while (1) {
        r = read(fd, data_buf + 4, TFTP_DATA_SIZE);
        if (r < 0) {
            log_msg(LOG_ERROR, "Read error on %s: %s", path, strerror(errno));
            send_error_packet(sock, &cli, sizeof(cli), 0, "Read error");
            break;
        }

        uint16_t opcode = htons(TFTP_OPCODE_DATA);
        uint16_t blk_be = htons(block);
        memcpy(data_buf, &opcode, 2);
        memcpy(data_buf + 2, &blk_be, 2);
        size_t pkt_size = 4 + (size_t)r;

        int retries = 0;

    resend_block:
        if (sendto(sock, data_buf, pkt_size, 0,
                   (struct sockaddr *)&cli, sizeof(cli)) < 0) {
            log_msg(LOG_ERROR, "sendto failed: %s", strerror(errno));
            break;
        }

        struct sockaddr_in recv_addr;
        socklen_t recv_len = sizeof(recv_addr);
        unsigned char ack_buf[516];

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        struct timeval tv;
        tv.tv_sec = g_cfg.timeout_sec;
        tv.tv_usec = 0;

        int sel = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            log_msg(LOG_ERROR, "select error: %s", strerror(errno));
            break;
        } else if (sel == 0) {
            /* timeout */
            if (++retries <= g_cfg.max_retries) {
                log_msg(LOG_DEBUG, "Timeout waiting ACK, retry block %u", block);
                goto resend_block;
            } else {
                log_msg(LOG_ERROR, "Max retries exceeded for block %u", block);
                break;
            }
        }

        ssize_t n = recvfrom(sock, ack_buf, sizeof(ack_buf), 0,
                             (struct sockaddr *)&recv_addr, &recv_len);
        if (n < 4) {
            log_msg(LOG_DEBUG, "Short ACK packet ignored");
            goto resend_block;
        }

        uint16_t op = (ack_buf[0] << 8) | ack_buf[1];
        uint16_t ack_blk = (ack_buf[2] << 8) | ack_buf[3];

        if (op != TFTP_OPCODE_ACK || ack_blk != block) {
            log_msg(LOG_DEBUG, "Unexpected packet: op=%u blk=%u", op, ack_blk);
            goto resend_block;
        }

        total_bytes += (size_t)r;

        if (r < TFTP_DATA_SIZE) {
            done_ok = 1;
            break;
        }
        block++;
        if (block == 0) block = 1; /* wrap safety */
    }

    close(fd);
    close(sock);

    now_iso8601(end_ts, sizeof(end_ts));
    safe_strcpy(ev.end_ts, sizeof(ev.end_ts), end_ts);
    ev.bytes = total_bytes;

    if (done_ok) {
        safe_strcpy(ev.status, sizeof(ev.status), "ok");
        safe_strcpy(ev.message, sizeof(ev.message), "transfer_complete");
    } else {
        if (ev.status[0] == '\0') {
            safe_strcpy(ev.status, sizeof(ev.status), "error");
            safe_strcpy(ev.message, sizeof(ev.message), "transfer_failed");
        }
    }
    event_emit(&ev);
    write_request_log(sa, start_ts, end_ts, total_bytes, ev.status, ev.message);

done:
    free(sa);
    return NULL;
}

/* Parse RRQ packet and extract filename + mode */
static int parse_rrq(const unsigned char *buf, ssize_t len,
                     char *filename, size_t filename_size,
                     char *mode, size_t mode_size) {
    if (len < 4) return -1;
    /* skip opcode (2 bytes) */
    const char *p = (const char *)(buf + 2);
    const char *end = (const char *)(buf + len);

    const char *fn = p;
    while (p < end && *p != '\0') p++;
    if (p >= end) return -1;
    safe_strcpy(filename, filename_size, fn);
    p++; /* skip null */

    if (p >= end) return -1;
    const char *md = p;
    while (p < end && *p != '\0') p++;
    if (p > end) return -1;
    safe_strcpy(mode, mode_size, md);
    return 0;
}

/* Listener thread main loop */
static void *listener_thread_main(void *arg) {
    ListenerArg *la = (ListenerArg *)arg;
    log_msg(LOG_INFO, "Starting listener on %s:%d", la->bind_addr, la->bind_port);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        log_msg(LOG_ERROR, "Failed to create listener socket: %s", strerror(errno));
        free(la);
        return NULL;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(la->bind_port);
    if (inet_pton(AF_INET, la->bind_addr, &addr.sin_addr) != 1) {
        log_msg(LOG_ERROR, "Invalid bind address: %s", la->bind_addr);
        close(sock);
        free(la);
        return NULL;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        log_msg(LOG_ERROR, "Failed to bind %s:%d: %s",
                la->bind_addr, la->bind_port, strerror(errno));
        close(sock);
        free(la);
        return NULL;
    }

    unsigned char buf[1500];

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&cli, &cli_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_msg(LOG_ERROR, "recvfrom error on %s:%d: %s",
                    la->bind_addr, la->bind_port, strerror(errno));
            continue;
        }
        if (n < 2) continue;

        uint16_t opcode = (buf[0] << 8) | buf[1];
        if (opcode != TFTP_OPCODE_RRQ) {
            log_msg(LOG_DEBUG, "Ignoring non-RRQ opcode=%u", opcode);
            continue;
        }

        char filename[256];
        char mode[32];
        if (parse_rrq(buf, n, filename, sizeof(filename), mode, sizeof(mode)) != 0) {
            log_msg(LOG_ERROR, "Failed to parse RRQ");
            continue;
        }

        char cli_ip[64];
        inet_ntop(AF_INET, &cli.sin_addr, cli_ip, sizeof(cli_ip));
        int cli_port = ntohs(cli.sin_port);

        log_msg(LOG_INFO, "RRQ from %s:%d file=\"%s\" mode=\"%s\"",
                cli_ip, cli_port, filename, mode);

        SessionArg *sa = (SessionArg *)calloc(1, sizeof(SessionArg));
        safe_strcpy(sa->bind_addr, sizeof(sa->bind_addr), la->bind_addr);
        sa->bind_port = la->bind_port;
        safe_strcpy(sa->client_ip, sizeof(sa->client_ip), cli_ip);
        sa->client_port = cli_port;
        safe_strcpy(sa->filename, sizeof(sa->filename), filename);

        pthread_t th;
        if (pthread_create(&th, NULL, session_thread_main, sa) != 0) {
            log_msg(LOG_ERROR, "Failed to create session thread");
            free(sa);
            continue;
        }
        pthread_detach(th);
    }

    close(sock);
    free(la);
    return NULL;
}

int tftp_start(const ServerConfig *cfg) {
    g_cfg = *cfg;

    pthread_t threads[MAX_LISTENERS];

    for (int i = 0; i < cfg->num_listeners; ++i) {
        ListenerArg *la = (ListenerArg *)calloc(1, sizeof(ListenerArg));
        safe_strcpy(la->bind_addr, sizeof(la->bind_addr), cfg->listeners[i].addr);
        la->bind_port = cfg->listeners[i].port;

        if (pthread_create(&threads[i], NULL, listener_thread_main, la) != 0) {
            log_msg(LOG_ERROR, "Failed to create listener thread for %s:%d",
                    la->bind_addr, la->bind_port);
            free(la);
            return -1;
        }
    }

    /* Wait forever on listeners (until killed by signal) */
    for (int i = 0; i < cfg->num_listeners; ++i) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
