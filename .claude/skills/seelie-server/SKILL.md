---
name: seelie-server
description: Build, deploy and operate the Seelie generic UDP server. Covers the binary datagram protocol, CHECK/ANNOUNCE version-check flow, rebar3 builds, systemd deployment, and Aliyun firewall configuration. Use when the user wants to deploy the server, modify the protocol, test version checking, or manage the running daemon.
license: MIT
metadata:
  author: seelie
  version: "1.0"
---

The Seelie project includes a lightweight Erlang/OTP UDP server (`server/`). It currently handles version checking and can be extended for other UDP-based services. It lives on branch `feature/update-server` and is deployed to a public host at `updates.example.com:9340` (the real host:port is configured at build time via the `SEELIE_DEFAULT_UPDATE_ENDPOINT` CMake cache variable — see `CMakeLists.txt`).

## When to use

Triggers:
- "deploy the server", "rebuild the server", "restart the daemon"
- "how does the version check protocol work", "what is the datagram format"
- "test the update server", "check if the server is running"
- "open firewall port for updates", "change update server port"
- modifying `update_handler.erl`, `protocol.erl`, or `sys.config`

## Architecture

```
client (Qt/C++)          UDP            server (Erlang/OTP)
   |      CHECK  ------------------>  udp_server (gen_server)
   |     ANNOUNCE <------------------  protocol (codec)
   |                                   update_handler (manifest)
```

- **udp_server** — opens UDP socket, dispatches packets to spawned handlers
- **protocol** — binary encode/decode + CRC16-CCITT validation
- **update_handler** — reads `CMakeLists.txt` version, compares with client's current version
- **systemd** — `seelie-server.service` manages the release binary

## Protocol

All datagrams share a fixed header, variable payload, and trailing CRC16.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Magic (4 bytes)                        |
|                     'H' 'C' 'H' 0x01                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| Version (1)   |  Command (1)  |           Seq (2)             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Payload Length (2)    |          Payload (*)          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
~                          Payload (cont.)                      ~
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           CRC16-CCITT (2)     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Size | Value |
|-------|------|-------|
| Magic | 4 B | `<<"HCH", 1>>` |
| Version | 1 B | `1` |
| Command | 1 B | `1=CHECK`, `2=ANNOUNCE`, `3=PULL`, `4=PUSH`, `5=ACK` |
| Seq | 2 B | uint16 big-endian, echo back in replies |
| PayloadLen | 2 B | uint16 big-endian, max **1384** |
| Payload | N B | JSON (v1) |
| CRC16 | 2 B | CRC16-CCITT (`0xFFFF` init, poly `0x1021`) over all preceding bytes |

### v1 flow — check only

**CHECK** (client → server)
```json
{"current_version":"1.0.0","platform":"windows"}
```

**ANNOUNCE** (server → client)
```json
{"available":true,"latest_version":"1.2.0"}
```

If the client is already up-to-date or ahead:
```json
{"available":false}
```

Supported platforms: `windows`, `macos`, `linux`.

## Build

Requires Erlang/OTP 25+ and rebar3.

```bash
cd server
rebar3 compile          # dev build
rebar3 eunit            # run CRC + protocol tests
rebar3 as prod release  # production release under _build/prod/rel/server
```

## Deploy

The production release is at `/opt/seelie-server` on the Aliyun host.

### Push changes and rebuild

```bash
scp -r server/* aliyun:/opt/seelie-server/
ssh aliyun "cd /opt/seelie-server && rebar3 as prod release && systemctl restart seelie-server"
```

### systemd service

File: `/etc/systemd/system/seelie-server.service`

```ini
[Unit]
Description=Seelie Generic UDP Server
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/seelie-server/_build/prod/rel/server
Environment=HOME=/root
ExecStart=/opt/seelie-server/_build/prod/rel/server/bin/server foreground
ExecStop=/opt/seelie-server/_build/prod/rel/server/bin/server stop
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Manage:
```bash
systemctl status seelie-server
systemctl restart seelie-server
systemctl stop seelie-server
journalctl -u seelie-server -f
```

### Configuration

`/opt/seelie-server/config/sys.config` (rebuilt into the release):

```erlang
[
    {server, [
        {udp_port, 9340},
        {udp_workers, 4},
        {version_file, "/opt/seelie-server/CMakeLists.txt"}
    ]}
].
```

- **udp_port** — the port to open in Aliyun security group (UDP inbound)
- **version_file** — absolute path to the root `CMakeLists.txt` the server reads for `project(VERSION)`

## Test

A Python test script is included at `test_udp.py`:

```bash
python test_udp.py
```

Or manually with `ncat` / a small Python socket script:

```python
import socket, struct, json

MAGIC = b'HCH\x01'
def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc

def encode(cmd, seq, payload=b''):
    body = struct.pack('>4sBBHH', MAGIC, 1, cmd, seq, len(payload)) + payload
    return body + struct.pack('>H', crc16(body))

payload = json.dumps({"current_version":"1.0.0","platform":"windows"}).encode()
packet = encode(1, 1, payload)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5)
sock.sendto(packet, ("updates.example.com", 9340))  # replace with your update host
data, _ = sock.recvfrom(2048)
print(data)
```

## Files

| Path | Purpose |
|------|---------|
| `server/src/udp_server.erl` | UDP socket gen_server |
| `server/src/protocol.erl` | Datagram codec + CRC16 |
| `server/src/update_handler.erl` | Version comparison logic |
| `server/src/server_app.erl` | OTP application callback |
| `server/src/server_sup.erl` | Supervisor |
| `server/config/sys.config` | Runtime config (port, version_file) |
| `server/rebar.config` | Build config + jsx dependency |
| `test_udp.py` | Python integration test |

## Common tasks

### Change the listening port

1. Edit `server/config/sys.config`
2. Rebuild release: `rebar3 as prod release`
3. Restart: `systemctl restart seelie-server`
4. Open the new port in Aliyun security group (UDP inbound)

### Update the version source

The server extracts `project(Seelie VERSION X.Y.Z ...)` from `version_file`.
If `CMakeLists.txt` moves, update the absolute path in `sys.config` and redeploy.

### Add a new platform

1. Add the platform binary to `?VALID_PLATFORMS` in `update_handler.erl`
2. Rebuild and restart
