# ctftp

A lightweight, multi-threaded TFTP server focused on auto-provisioning scenarios (for example Cisco IP phones).  
It provides detailed per-request logging, configurable listeners on multiple IP/ports, and real-time status events over UDP and HTTP, all driven by a simple configuration file.

---

## Table of Contents

1. [Overview](#overview)  
2. [Features](#features)  
3. [Directory Layout](#directory-layout)  
4. [Building](#building)  
   - [Requirements](#requirements)  
   - [Normal build](#normal-build)  
   - [Static build](#static-build)  
   - [Compiler notes](#compiler-notes)  
5. [Configuration](#configuration)  
   - [Configuration file format](#configuration-file-format)  
   - [Configuration options](#configuration-options)  
   - [Example configurations](#example-configurations)  
6. [Running](#running)  
7. [Logging](#logging)  
   - [Central log](#central-log)  
   - [Per-request file logs](#per-request-file-logs)  
8. [Event Streaming](#event-streaming)  
   - [UDP events](#udp-events)  
   - [HTTP events](#http-events)  
9. [Cisco IP Phone Auto-Provisioning Example](#cisco-ip-phone-auto-provisioning-example)  
10. [Security Considerations](#security-considerations)  
11. [Limitations](#limitations)  
12. [Roadmap / TODO](#roadmap--todo)  
13. [License](#license)  
14. [Contributing](#contributing)  

---

## Overview

**ctftp** is a small, focused TFTP server implementation written in C, designed primarily for auto-provisioning scenarios such as Cisco IP phone deployments.

Key design goals:

- Simple, self-contained codebase.
- Strong observability: detailed logging per request and per file.
- Easy integration with external monitoring/analytics via UDP/HTTP events.
- Configuration-driven behavior (no hard-coded IPs, ports, or directories).

---

## Features

- **Multi-threaded TFTP server**
  - One listener thread per configured `IP:port`.
  - One session thread per client request (RRQ).

- **Auto-provisioning oriented**
  - Ideal for serving configuration files to IP phones and similar devices.
  - Read-only TFTP: only RRQ (read requests) are supported by design.

- **Per-request logging**
  - Central log file with full activity.
  - Per-file `.log` files stored next to requested files, containing key metadata:
    - timestamps
    - client IP:port
    - bytes transferred
    - status and message

- **Event streaming**
  - JSON events over **UDP**.
  - JSON events over **HTTP POST** to a configurable endpoint.
  - Events are emitted for request start, completion, and error conditions.

- **Configuration-driven**
  - Root directory, log directory, listeners, timeouts, retries, log level, and event targets are configured via a simple key/value config file.

- **Hardened-by-default**
  - Basic path sanitization (no `..`, no absolute paths).
  - Easy to run as an unprivileged user (especially on non-privileged ports).
  - Transparent, readable C code primarily targeting C99.

---

## Directory Layout

A typical filesystem layout for `ctftp` looks like this:

```text
ctftp/
  README.md            # This file
  Makefile
  ctftp.conf           # Main configuration file (example)
  src/
    main.c
    config.c / config.h
    logger.c / logger.h
    util.c / util.h
    events.c / events.h
    tftp.c / tftp.h
  obj/                 # Created during build for object files

/srv/tftp              # Default root directory for TFTP files (configurable)
/var/log/ctftp         # Default log directory (configurable)
```

You can adjust directories via the configuration file.

---

## Building

### Requirements

- A POSIX-compatible operating system (Linux, *BSD, etc.).
- A C compiler with at least C99-level support (e.g., GCC, Clang, or similar).
- `make`.

On very old systems, you might need to adjust compiler flags in the `Makefile` (see [Compiler notes](#compiler-notes)).

### Normal build

Clone the repository and build:

```bash
git clone https://github.com/your-username/ctftp.git
cd ctftp

make
```

If the build succeeds, you should see:

- `ctftp` — the main TFTP server binary.

### Static build

`ctftp` also provides a `static` target that attempts to link against static libraries (where available). This can be useful for deploying a single self-contained binary.

```bash
make static
```

If successful, this produces:

- `ctftp-static` — statically linked TFTP server binary.

> Note: Static linking depends on the availability of static C library files on your system (for example `libc.a`).  
> On some distributions, you may need to install additional `*-static` packages or skip the static build entirely.

### Compiler notes

In the `Makefile`, compilation flags are controlled by:

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=gnu99 -pthread
LDFLAGS = -pthread
```

If your compiler does not support `-std=gnu99` (or `-std=c11`), you can simplify:

```makefile
CFLAGS  = -Wall -Wextra -O2 -pthread
```

Similarly, the static build uses:

```makefile
static: CFLAGS += -static
static: LDFLAGS += -static
```

If your toolchain does not support static linking, simply avoid `make static` or remove those flags.

---

## Configuration

### Configuration file format

By default, `ctftp` looks for a configuration file named `ctftp.conf` in the current working directory, unless a specific path is passed on the command line.

The file is a simple `key=value` format, with `#` or `;` indicating comments:

```ini
# Root directory for TFTP files
root_dir=/srv/tftp

# Log directory (central log file)
log_dir=/var/log/ctftp

# Listeners: comma separated ip:port
# You can define up to 8 listeners.
listeners=0.0.0.0:69,192.168.7.34:1069

# UDP event target (optional). If port is 0 or empty, UDP events are disabled.
event_udp=127.0.0.1:9999

# HTTP event URL (optional). If empty, HTTP events are disabled.
# Only plain HTTP is supported (no HTTPS/TLS).
event_http_url=http://127.0.0.1:8080/tftp-events

# Timeout for waiting ACK (seconds)
timeout_sec=3

# Max retransmissions per block
max_retries=5

# Log level: error, info, debug
log_level=debug
```

If the configuration file is missing, `ctftp` falls back to built-in defaults:

- `root_dir=/srv/tftp`
- `log_dir=/var/log/ctftp`
- one listener: `0.0.0.0:69`
- no UDP or HTTP events
- `timeout_sec=3`
- `max_retries=5`
- `log_level=info`

### Configuration options

#### `root_dir`

- Path to the directory where TFTP files are stored and served.
- Only files under this directory (and subdirectories) can be requested.
- Basic path sanitization disallows `..` and absolute paths.

#### `log_dir`

- Directory where the central log file `ctftp.log` is stored.
- Must be writable by the user running `ctftp`.

#### `listeners`

- Comma-separated list of `IP:port` pairs.
- For each entry, `ctftp` starts one UDP listener thread.
- Examples:
  - `0.0.0.0:69` — listen on all interfaces on standard TFTP port.
  - `192.168.0.10:1069` — listen on a specific IP and non-privileged port.
  - `127.0.0.1:1069,192.168.10.10:69` — multi-homed scenarios.

Maximum of 8 listeners is supported by default.

#### `event_udp`

- Format: `host:port`.
- When set, `ctftp` sends a small JSON event over UDP for key events (request start, completion, error).
- Example:
  - `event_udp=127.0.0.1:9999`
- If unset, invalid, or port is `0`, UDP events are disabled.

#### `event_http_url`

- HTTP URL for sending JSON events via HTTP POST.
- Only `http://` (without TLS) is supported in the core implementation (HTTPS can be achieved via a proxy on your network).
- Example:
  - `event_http_url=http://127.0.0.1:8080/tftp-events`
- If empty or invalid, HTTP events are disabled.

#### `timeout_sec`

- Timeout (in seconds) for waiting for an ACK from the client after sending a DATA packet.
- Default: `3`

#### `max_retries`

- Number of retransmission attempts per DATA block before giving up and marking the transfer as failed.
- Default: `5`

#### `log_level`

- Logging verbosity for the central log:
  - `error` — critical errors only.
  - `info`  — normal operational messages (recommended default).
  - `debug` — very verbose; detailed debug information.

---

### Example configurations

#### 1. Minimal, single listener, no events

```ini
root_dir=/srv/tftp
log_dir=/var/log/ctftp
listeners=0.0.0.0:69
log_level=info
```

#### 2. Multi-listener, debug logs, UDP events only

```ini
root_dir=/srv/tftp
log_dir=/var/log/ctftp

listeners=0.0.0.0:69,192.168.50.10:1069

event_udp=192.168.10.100:9999
event_http_url=

timeout_sec=3
max_retries=5
log_level=debug
```

#### 3. Non-privileged port, HTTP events for monitoring

```ini
root_dir=/srv/tftp
log_dir=/var/log/ctftp

listeners=0.0.0.0:1069

event_udp=
event_http_url=http://127.0.0.1:8080/tftp-events

timeout_sec=3
max_retries=5
log_level=info
```

---

## Running

Once built and configured, you can run `ctftp` like this:

```bash
# Using default config file (ctftp.conf in current directory)
./ctftp

# Using a custom config path
./ctftp /etc/ctftp/ctftp.conf
```

If listening on port 69, you typically need elevated privileges:

```bash
sudo ./ctftp /etc/ctftp/ctftp.conf
```

For safer operation:

- Consider using a non-privileged port (e.g. 1069).
- Use a firewall or port forwarding from 69 to your non-privileged port.
- Run `ctftp` under an unprivileged user.

Example of preparing directories and permissions:

```bash
sudo mkdir -p /srv/tftp
sudo mkdir -p /var/log/ctftp
sudo useradd -r -s /usr/sbin/nologin ctftp
sudo chown -R ctftp:ctftp /srv/tftp /var/log/ctftp
```

Then create a simple `systemd` unit, for example:

```ini
[Unit]
Description=ctftp - Multi-threaded TFTP server
After=network.target

[Service]
Type=simple
User=ctftp
Group=ctftp
WorkingDirectory=/opt/ctftp
ExecStart=/opt/ctftp/ctftp /opt/ctftp/ctftp.conf
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

And enable/start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable ctftp
sudo systemctl start ctftp
sudo systemctl status ctftp
```

---

## Logging

### Central log

The central log file is placed in `log_dir`, typically:

```text
/var/log/ctftp/ctftp.log
```

Each line includes a timestamp, log level, and message, for example:

```text
[2025-12-02T10:15:42] [INFO] ctftp starting with config: /etc/ctftp/ctftp.conf
[2025-12-02T10:15:42] [INFO] Starting listener on 0.0.0.0:69
[2025-12-02T10:16:01] [INFO] RRQ from 192.168.10.50:40000 file="SEP000000000123.cnf.xml" mode="octet"
[2025-12-02T10:16:02] [INFO] EVENT type=0 client=192.168.10.50:40000 file="SEP000000000123.cnf.xml" bytes=3456 status=ok msg=transfer_complete
```

You can manage log rotation using a standard tool such as `logrotate`.

### Per-request file logs

For each requested file, `ctftp` appends a line to a `.log` file located in `root_dir`, with the same base name as the requested file.

Example:

- Requested file: `/srv/tftp/SEP000000000123.cnf.xml`
- Per-request log file: `/srv/tftp/SEP000000000123.cnf.xml.log`

Each session appends a `;`-separated line:

```text
start_ts;end_ts;client_ip;client_port;bytes;status;message
```

Example line:

```text
2025-12-02T10:16:01;2025-12-02T10:16:02;192.168.10.50;40000;3456;ok;transfer_complete
```

This makes it extremely easy to:

- See how many times a given phone pulled its configuration.
- Audit when and from where files were requested.
- Correlate successful provisioning with other logs.

---

## Event Streaming

`ctftp` can emit JSON-formatted events over both UDP and HTTP.

### UDP events

When `event_udp` is set, every notable event is encoded as a JSON object and sent as a single UDP datagram to the configured host and port.

Example JSON payload:

```json
{
  "type": 0,
  "client_ip": "192.168.10.50",
  "client_port": 40000,
  "filename": "SEP000000000123.cnf.xml",
  "bytes": 3456,
  "status": "ok",
  "message": "transfer_complete",
  "start": "2025-12-02T10:16:01",
  "end": "2025-12-02T10:16:02"
}
```

Typical use cases:

- Real-time dashboards for provisioning activity.
- Feeding into a central log/metric collector via a small UDP listener.
- Detecting anomalies (e.g. repeated failures for particular phones).

### HTTP events

When `event_http_url` is set, events are also sent via HTTP POST:

- Method: `POST`
- Content-Type: `application/json`
- Body: same JSON object as for UDP.

Example HTTP request (simplified):

```http
POST /tftp-events HTTP/1.1
Host: 127.0.0.1
Content-Type: application/json
Content-Length: 200
Connection: close

{"type":0,"client_ip":"192.168.10.50","client_port":40000,"filename":"SEP000000000123.cnf.xml","bytes":3456,"status":"ok","message":"transfer_complete","start":"2025-12-02T10:16:01","end":"2025-12-02T10:16:02"}
```

The HTTP sender runs in a dedicated background thread, pulling events from an in-memory ring buffer, to avoid blocking the main TFTP processing loop.

---

## Cisco IP Phone Auto-Provisioning Example

A typical deployment for Cisco IP phones might look like this:

1. **Place configuration files under `root_dir`**:

   ```text
   /srv/tftp/
     SEP000000000001.cnf.xml
     SEP000000000002.cnf.xml
     ...
   ```

2. **Configure your DHCP server or phone settings**  
   Ensure that the phones use the IP address of the `ctftp` host as their TFTP server (e.g., via DHCP Option 150 or manual configuration).

3. **Configure `ctftp`** (e.g. `/etc/ctftp/ctftp.conf`):

   ```ini
   root_dir=/srv/tftp
   log_dir=/var/log/ctftp
   listeners=0.0.0.0:69
   log_level=info
   ```

4. **Start `ctftp`**:

   ```bash
   sudo ./ctftp /etc/ctftp/ctftp.conf &
   tail -f /var/log/ctftp/ctftp.log
   ```

5. **Observe logs and per-file `.log` files**  
   Each phone that pulls its configuration will generate:

   - Central log entries.
   - A new line in the corresponding `.log` file under `/srv/tftp`.

This setup makes it straightforward to see exactly which phones successfully downloaded their configs and when.

---

## Security Considerations

- **Read-only TFTP**  
  Only RRQ (read requests) are implemented. WRQ (write requests) are rejected/not implemented on purpose, reducing attack surface.

- **Path sanitization**  
  Filenames are sanitized:
  - Leading `/` characters are stripped.
  - Any occurrence of `..` causes the request to be rejected.
  This prevents trivial directory traversal attacks.

- **No encryption**  
  TFTP itself is unencrypted, and UDP/HTTP events are also unencrypted.  
  Use `ctftp` in trusted networks, or add your own secure tunnels (VPN, SSH port forwarding, proxies) where necessary.

- **Unprivileged operation**  
  It is recommended to:
  - Run `ctftp` as a dedicated, unprivileged user.
  - Use a non-privileged port when possible, or use port forwarding or `authbind`-like mechanisms for port 69.

- **Firewall rules**  
  Use system-level firewalling (iptables, nftables, etc.) to restrict which hosts can reach the TFTP service.

---

## Limitations

Current limitations of `ctftp` include:

- Only RRQ (read) is implemented; no WRQ (write/upload) support.
- No TFTP option negotiation (e.g., `blksize`, `tsize`, `timeout` options).
- No built-in IP-based ACLs (expected to be enforced by the network/firewall).
- HTTP events are plain HTTP only (no HTTPS/TLS in the core implementation).
- Filenames are currently treated in a case-sensitive manner.

Despite these limitations, the server is fully usable for many auto-provisioning scenarios, especially in controlled network environments.

---

## Roadmap / TODO

The following items are planned or suggested improvements for future versions of `ctftp`:

1. **Case-insensitive filename handling (optional)**  
   - Add an option in the configuration file to treat requested filenames in a case-insensitive manner (e.g., to better match environments where provisioning files may be created with inconsistent casing).
   - Optionally map all filenames to a canonical form (e.g., lowercased) internally while preserving underlying filesystem semantics.

2. **Management API for file operations**  
   - Provide a simple HTTP/JSON or command-line API for:
     - Uploading new configuration files to `root_dir`.
     - Deleting existing files.
     - Renaming or editing metadata.
   - This API would be separate from TFTP itself and could be protected via authentication, IP ACLs, or reverse proxy.

3. **Support for secure transfer methods (beyond TFTP)**  
   - Integrate or complement `ctftp` with secure transport options such as:
     - SFTP-based configuration file delivery.
     - HTTPS-based file serving for devices that support it.
     - Gateway/bridge modules that map secure protocols to local TFTP access.
   - The goal is to keep `ctftp` focused and simple, while providing a path for deployments that require encrypted transport end-to-end.

4. **Extended TFTP features** (general roadmap)  
   - Implement TFTP option negotiation (e.g., `blksize`, `tsize`, `timeout`).
   - Add IP-based allow/deny lists at the TFTP level, in addition to external firewall rules.
   - Expand event types and add more detailed status/error codes.
   - Offer a JSON-native logging mode for easier ingestion by log processors.

If you have specific needs or ideas, please open an issue or submit a pull request.

---

## License

This project is released as open source.  
Please add an explicit `LICENSE` file to the repository (for example MIT, Apache-2.0, or BSD-2-Clause) and reference it here once chosen.

_Example (if using MIT):_

```text
ctftp is licensed under the MIT License. See the LICENSE file for details.
```

---

## Contributing

Contributions are welcome!

- **Issues**:  
  Use GitHub issues to report bugs, request features, or ask questions.

- **Pull requests**:  
  - Keep changes focused and well-scoped.
  - Match the existing code style and comment conventions.
  - Add comments for any non-trivial logic.
  - Where appropriate, include sample configurations or documentation updates.

- **Discussions / feedback**:  
  Feel free to propose new ideas, especially around:
  - Enhanced security and hardening.
  - Better observability and event formats.
  - Integrations with other provisioning or monitoring systems.

Thank you for using and contributing to **ctftp**!
