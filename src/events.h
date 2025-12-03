#ifndef EVENTS_H
#define EVENTS_H

#include "config.h"
#include <stddef.h>

typedef enum {
    EVT_REQ_START = 0,
    EVT_REQ_DONE  = 1,
    EVT_REQ_ERROR = 2
} EventType;

typedef struct {
    EventType type;
    char client_ip[64];
    int  client_port;
    char filename[256];
    size_t bytes;
    char status[32];
    char message[128];
    char start_ts[32];
    char end_ts[32];
} Event;

int events_init(const ServerConfig *cfg);
void events_shutdown(void);
void event_emit(const Event *ev);

#endif
