---
title: ctftp — Multi-threaded TFTP Server
---

# ctftp

**ctftp** is a lightweight, multi-threaded TFTP server focused on auto-provisioning scenarios (for example Cisco IP phones, IP PBXs, and embedded devices).  
It provides detailed per-request logging, configurable listeners on multiple IP/ports, and real-time status events over UDP and HTTP, all driven by a simple configuration file.

> 🔗 **Source code:** [GitHub repository](https://github.com/your-username/ctftp)  
> 📦 **Downloads:** [Releases & prebuilt binaries](https://github.com/your-username/ctftp/releases)

> Replace `your-username` in the URLs above with your actual GitHub username.

---

## Key Features

- **Multi-threaded TFTP server**
  - One listener thread per configured `IP:port`.
  - One session thread per client request (RRQ).
- **Provisioning-focused**
  - Read-only TFTP (RRQ only); ideal for configuration file delivery.
  - Designed with Cisco IP phone auto-provisioning in mind.
- **Per-request observability**
  - Central log file with all activity.
  - Per-file `.log` files stored next to requested files:
    - Request timestamps.
    - Client IP/port.
    - Bytes transferred.
    - Final status and message.
- **Event streaming**
  - JSON events over UDP for lightweight, low-latency monitoring.
  - JSON events over HTTP POST for integration with log collectors and dashboards.
- **Configuration-driven behavior**
  - Root directory, log directory, listeners, timeouts, retries, log level, and event targets configured via a simple text file.
- **Hardened behaviour**
  - Basic path sanitization (no `..`, no absolute paths).
  - Easy to run under an unprivileged account (especially on non-privileged ports).
  - Straightforward C codebase targeting C99/gnu99.

For a deep dive into the internal architecture, see the project [README](https://github.com/your-username/ctftp/blob/main/README.md).

---

## Quick Start (Prebuilt Binaries)

### 1. Download

Go to the [Releases](https://github.com/your-username/ctftp/releases) page and download:

- `ctftp-linux` — dynamically linked Linux binary  
- `ctftp-static-linux` — statically linked Linux binary (if available)

Make the binary executable:

```bash
chmod +x ctftp-linux
```

> On some systems you may prefer the static binary (`ctftp-static-linux`) to minimize external library dependencies.

---

### 2. Prepare Directories

Create the TFTP root directory and log directory, and optionally a dedicated user:

```bash
sudo mkdir -p /srv/tftp
sudo mkdir -p /var/log/ctftp

# Optional but recommended:
sudo useradd -r -s /usr/sbin/nologin ctftp || true
sudo chown -R ctftp:ctftp /srv/tftp /var/log/ctftp
```

By default:

- `root_dir` → `/srv/tftp`
- `log_dir` → `/var/log/ctftp`

You can change both in the configuration file.

---

### 3. Configuration

Create a `ctftp.conf` file next to your binary, for example:

```ini
# Root directory for TFTP files
root_dir=/srv/tftp

# Log directory for the main ctftp.log
log_dir=/var/log/ctftp

# One or more listeners (up to 8): ip:port,ip:port,...
listeners=0.0.0.0:69

# Optional: send JSON events over UDP
event_udp=

# Optional: send JSON events over HTTP POST
# Example: http://127.0.0.1:8080/tftp-events
event_http_url=

# Timeout waiting for ACK (seconds)
timeout_sec=3

# Max retransmissions per block
max_retries=5

# Log level: error, info, debug
log_level=info
```

With this configuration:

- `ctftp` listens on UDP port 69 on all interfaces.
- TFTP files are served from `/srv/tftp`.
- Logs are written to `/var/log/ctftp/ctftp.log`.
- No UDP/HTTP events are emitted (those can be enabled later).

For more advanced examples, including multi-listener setups and UDP/HTTP events, see the main README.

---

### 4. Run ctftp

To run `ctftp` with the configuration above:

```bash
sudo ./ctftp-linux ./ctftp.conf
```

If you are using a non-privileged port (e.g. 1069 instead of 69), you can run as an unprivileged user:

```bash
./ctftp-linux ./ctftp.conf
```

Point your devices to the TFTP server IP (and port, if non-default). For Cisco IP phones, this is typically configured via DHCP option 150 or manually on the device.

---

## Cisco IP Phone Auto-Provisioning Example

A typical Cisco IP phone provisioning flow with **ctftp** looks like this:

1. Place configuration files under the TFTP root:

   ```text
   /srv/tftp/
     SEP000000000001.cnf.xml
     SEP000000000002.cnf.xml
     ...
   ```

2. Configure your DHCP server (Option 150) or phone settings so that phones use your `ctftp` host as their TFTP server.

3. Run `ctftp`:

   ```bash
   sudo ./ctftp-linux ./ctftp.conf
   ```

4. Monitor:

   - **Central log:** `/var/log/ctftp/ctftp.log`  
     Contains startup messages, RRQ logs, and event summaries.

   - **Per-file logs:** `/srv/tftp/SEP000000000001.cnf.xml.log`, etc.  
     Each line contains:
     ```text
     start_ts;end_ts;client_ip;client_port;bytes;status;message
     ```
     Example:
     ```text
     2025-12-02T10:16:01;2025-12-02T10:16:02;192.168.10.50;40000;3456;ok;transfer_complete
     ```

This makes it straightforward to answer questions such as:

- *Did this phone actually download its config?*
- *When? From which IP? How many bytes? Did it complete successfully?*

---

## Event Streaming

`ctftp` can emit JSON events to external monitoring systems.

### UDP Events

Configure:

```ini
event_udp=127.0.0.1:9999
```

Each TFTP session will generate small JSON events, such as:

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

A simple UDP listener can be used to ingest these into your logging or metrics pipeline.

### HTTP Events

Configure:

```ini
event_http_url=http://127.0.0.1:8080/tftp-events
```

The same JSON payload is sent via HTTP POST. This is ideal for:

- Pushing provisioning events into an HTTP-based log collector.
- Driving internal dashboards that show real-time provisioning status.

A dedicated background thread handles HTTP delivery, so TFTP data paths remain responsive.

---

## Security Notes

- **Read-only TFTP**  
  Only RRQ (read) is implemented; no WRQ (write/upload) support. This reduces attack surface for unauthorized uploads.

- **Path sanitization**  
  Filenames are sanitized:
  - Leading `/` characters are stripped.
  - Any occurrence of `..` is treated as invalid and rejected.
  This protects against trivial directory traversal attacks.

- **No encryption**  
  TFTP and the UDP/HTTP events are unencrypted. Use `ctftp` inside trusted networks or behind VPN/SSH tunnels, or pair it with your own secure endpoints.

- **Unprivileged operation**  
  For best practice:
  - Run `ctftp` under a dedicated service account.
  - Use non-privileged ports (e.g. 1069) when possible, or rely on port forwarding/`authbind`-style tools.

Firewall rules (iptables, nftables, etc.) should be used to restrict access to the TFTP and event ports.

---

## Building from Source

If you prefer to compile `ctftp` yourself:

```bash
git clone https://github.com/your-username/ctftp.git
cd ctftp

# Normal build
make

# Static build (best-effort; requires static libs)
make static
```

Results:

- `ctftp` — dynamically linked server binary.
- `ctftp-static` — statically linked server binary (if static linking is available).

You can then copy the binaries to your deployment environment and use them with your own configuration.

---

## Roadmap / TODO (High-Level)

Current planned enhancements include:

1. **Optional case-insensitive filename handling**
   - Configurable behavior to treat requested filenames in a case-insensitive manner (useful on case-sensitive filesystems with inconsistent naming).

2. **Management API for file operations**
   - A small HTTP/JSON API for:
     - Uploading new configuration files to `root_dir`.
     - Deleting or renaming files.
   - Intended for integrations with provisioning portals or automation tools.

3. **Secure transport backends**
   - Complementary support for secure file delivery, such as:
     - SFTP-based transport.
     - HTTPS-based file serving.
   - Likely implemented as companion components that integrate with `ctftp` rather than changing the core TFTP engine.

For a full and always up-to-date roadmap, check the [Issues](https://github.com/your-username/ctftp/issues) and [README](https://github.com/your-username/ctftp/blob/main/README.md).

---

## Get Involved

If you find **ctftp** useful:

- ⭐ Star the repository to support the project.
- 🐛 Open issues for bugs, questions, or feature requests.
- 🔧 Submit pull requests for improvements or new features.

Thank you for using **ctftp**!
