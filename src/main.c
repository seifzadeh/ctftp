// src/main.c
#include "config.h"
#include "logger.h"
#include "events.h"
#include "tftp.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    // Default config path
    const char *cfg_path = "ctftp.conf";
    if (argc > 1) {
        cfg_path = argv[1];
    }

    ServerConfig cfg;

    // Load configuration (use defaults if file is missing)
    if (load_config(cfg_path, &cfg) != 0) {
        fprintf(stderr, "Failed to load config: %s\n", cfg_path);
        return 1;
    }

    // Initialize logger
    if (logger_init(&cfg) != 0) {
        fprintf(stderr, "Failed to init logger, using stderr only\n");
    }

    log_msg(LOG_INFO, "ctftp starting with config: %s", cfg_path);

    // Initialize event subsystem (UDP/HTTP)
    events_init(&cfg);

    // Start TFTP listeners (blocks until process is killed)
    int rc = tftp_start(&cfg);

    // Shutdown events and logger
    events_shutdown();
    logger_close();

    return (rc == 0) ? 0 : 1;
}
