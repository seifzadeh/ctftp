---
title: ctftp - Multi-threaded TFTP Server
description: ctftp is a lightweight, multi-threaded, logging-focused TFTP server designed for auto-provisioning scenarios such as Cisco IP phones.
---

<div align="center">

# ctftp

A lightweight, multi-threaded TFTP server focused on **auto-provisioning** (e.g. Cisco IP phones), with strong logging and event streaming.

[⬇️ Download static Linux binary](https://github.com/seifzadeh/ctftp/releases/download/0.0.1/ctftp-static-linux) ·
[💻 Source code on GitHub](https://github.com/seifzadeh/ctftp)

</div>

---

## 1. Super Quick Start (Linux, static binary)

> If you just want to **run it now** on Linux, follow these steps.

### Step 1 — Download the static binary

```bash
cd /opt
sudo mkdir -p /opt/ctftp
cd /opt/ctftp

# Download static Linux binary
sudo curl -L -o ctftp-static-linux \
  https://github.com/seifzadeh/ctftp/releases/download/0.0.1/ctftp-static-linux

# Make it executable
sudo chmod +x ctftp-static-linux
```

### Step 2 — Create directories

```bash
# Root directory for TFTP files
sudo mkdir -p /srv/tftp

# Log directory for ctftp
sudo mkdir -p /var/log/ctftp

# Optional: dedicated user (recommended)
sudo useradd -r -s /usr/sbin/nologin ctftp || true
sudo chown -R ctftp:ctftp /srv/tftp /var/log/ctftp /opt/ctftp
```

### Step 3 — Basic configuration file

Create `/opt/ctftp/ctftp.conf`:

```ini
# Root directory for TFTP files
root_dir=/srv/tftp

# Log directory (central log file)
log_dir=/var/log/ctftp

# One or more listeners (comma-separated)
# Standard TFTP port 69 on all interfaces
listeners=0.0.0.0:69

# Optional UDP event target (host:port)
event_udp=

# Optional HTTP event URL (plain HTTP)
event_http_url=

# Timeout and retries
timeout_sec=3
max_retries=5

# Log level: error, info, debug
log_level=info
```

Place one test file in `/srv/tftp`, for example:

```bash
echo "hello from ctftp" | sudo tee /srv/tftp/test.txt
sudo chown ctftp:ctftp /srv/tftp/test.txt
```

### Step 4 — Run ctftp manually (for a quick test)

```bash
cd /opt/ctftp
sudo -u ctftp ./ctftp-static-linux ./ctftp.conf
```

Now, from another machine (or the same one), you can test with any TFTP client, for example:

```bash
tftp <server-ip> 69
tftp> get test.txt
```

If everything is correct, you should receive `test.txt` from `/srv/tftp`.

---

## 2. Run ctftp as a systemd service

To run ctftp as a background service on a systemd-based Linux distribution:

### Step 1 — Create systemd unit file

Create `/etc/systemd/system/ctftp.service` with:

```ini
[Unit]
Description=ctftp - Multi-threaded TFTP server
After=network.target

[Service]
Type=simple
User=ctftp
Group=ctftp
WorkingDirectory=/opt/ctftp
ExecStart=/opt/ctftp/ctftp-static-linux /opt/ctftp/ctftp.conf
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

> Make sure the paths match your actual installation (`/opt/ctftp`, config file path, and binary name).

### Step 2 — Reload systemd and enable service

```bash
sudo systemctl daemon-reload
sudo systemctl enable ctftp
sudo systemctl start ctftp
sudo systemctl status ctftp
```

If everything goes well, `ctftp` should be running in the background and listening on the configured port(s).

---

## 3. Checking logs

### Central log

The main log file is written under `log_dir` (from your config), usually:

```bash
sudo tail -f /var/log/ctftp/ctftp.log
```

Example log lines:

```text
[2025-12-02T10:15:42] [INFO] ctftp starting with config: /opt/ctftp/ctftp.conf
[2025-12-02T10:15:42] [INFO] Starting listener on 0.0.0.0:69
[2025-12-02T10:16:01] [INFO] RRQ from 192.168.10.50:40000 file="test.txt" mode="octet"
[2025-12-02T10:16:02] [INFO] EVENT type=0 client=192.168.10.50:40000 file="test.txt" bytes=16 status=ok msg=transfer_complete
```

### Per-file logs

For each requested file, ctftp appends a line to a sidecar log file in `root_dir` with the same name plus `.log`:

```text
/srv/tftp/test.txt
/srv/tftp/test.txt.log
```

View it with:

```bash
sudo cat /srv/tftp/test.txt.log
```

Example content:

```text
2025-12-02T10:16:01;2025-12-02T10:16:02;192.168.10.50;40000;16;ok;transfer_complete
```

This makes it easy to audit which clients have downloaded which files and when.

---

## 4. Basic Configuration Overview

ctftp uses a simple key/value configuration file (usually `ctftp.conf`).

Common options:

- `root_dir` – directory from which TFTP files are served.  
- `log_dir` – directory for the central `ctftp.log`.  
- `listeners` – comma-separated list of `IP:port` pairs, for example:
  - `0.0.0.0:69`
  - `192.168.10.10:69,192.168.10.11:1069`
- `event_udp` – optional UDP target for JSON events, e.g. `127.0.0.1:9999`.  
- `event_http_url` – optional HTTP URL for JSON events over POST.  
- `timeout_sec` – timeout when waiting for ACK.  
- `max_retries` – max retransmission attempts per block.  
- `log_level` – `error`, `info`, or `debug`.

For more detailed documentation, see the main project README in the repository.

---

## 5. Source Code & Contributions

- **Source repository:**  
  https://github.com/seifzadeh/ctftp

If you find ctftp useful:

- ⭐ Star the repository.
- 🐛 Open issues for bugs, questions, or feature requests.
- 🔧 Submit pull requests for improvements, new features, or better documentation.

Thanks for using **ctftp**!