# ElephantShrew

```text
███████╗██╗     ███████╗██████╗ ██╗  ██╗ █████╗ ███╗   ██╗████████╗
██╔════╝██║     ██╔════╝██╔══██╗██║  ██║██╔══██╗████╗  ██║╚══██╔══╝
█████╗  ██║     █████╗  ██████╔╝███████║███████║██╔██╗ ██║   ██║
██╔══╝  ██║     ██╔══╝  ██╔═══╝ ██╔══██║██╔══██║██║╚██╗██║   ██║
███████╗███████╗███████╗██║     ██║  ██║██║  ██║██║ ╚████║   ██║
╚══════╝╚══════╝╚══════╝╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝

 ███████╗██╗  ██╗██████╗ ███████╗██╗    ██╗
 ██╔════╝██║  ██║██╔══██╗██╔════╝██║    ██║
 ███████╗███████║██████╔╝█████╗  ██║ █╗ ██║
 ╚════██║██╔══██║██╔══██╗██╔══╝  ██║███╗██║
 ███████║██║  ██║██║  ██║███████╗╚███╔███╔╝
 ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝ ╚══╝╚══╝
```

ElephantShrew captures live packets from one or more network interfaces. Packet
recording to the Redis-compatible stream `elephantshrew:packets` is enabled
with `-r`. It can also bridge raw packets from one interface to another, with
optional bidirectional forwarding.

## Requirements

- A C++20 compiler
- `meson`
- `ninja`
- `pkg-config`
- Boost
- OpenSSL
- `spdlog`
- PcapPlusPlus
- `nlohmann/json.hpp`
- Redis or DragonFly listening on `127.0.0.1:6379` when using `-r`

## Build

Run the build from the project root:

```bash
meson setup builddir
meson compile -C builddir
```

The binary will be created at:

```bash
./builddir/elephantshrew
```

The default runtime config lives at:

```bash
./elephantshrew.json
```

## Run

If you do not pass any interface arguments, ElephantShrew will auto-select the
first available interface. The process keeps running until you stop it with
`Ctrl+C`.

Capture from a single interface and record packets:

```bash
sudo ./builddir/elephantshrew -r -i eth0
```

Capture from multiple interfaces by repeating `-i`:

```bash
sudo ./builddir/elephantshrew -r -i eth0 -i wlan0
```

Route packets from `eth0` to `eth1`:

```bash
sudo ./builddir/elephantshrew --route eth0:eth1
```

Bridge packets in both directions between `eth0` and `eth1`:

```bash
sudo ./builddir/elephantshrew --route eth0:eth1 --bidirectional
```

Route packets and record/debug them at the same time:

```bash
sudo ./builddir/elephantshrew -r -d --route eth0:eth1 --bidirectional
```

Run with automatic interface selection:

```bash
sudo ./builddir/elephantshrew -r
```

Run in debug mode and print one log line per incoming packet:

```bash
sudo ./builddir/elephantshrew -d -i eth0
```

Run with both packet recording and debug logging enabled:

```bash
sudo ./builddir/elephantshrew -r -d -i eth0
```

Run with an explicit JSON config file:

```bash
sudo ./builddir/elephantshrew -c elephantshrew.json
```

List available interfaces without starting capture:

```bash
./builddir/elephantshrew -s
```

## Notes

- Packet capture commonly requires elevated privileges, which is why the
  examples use `sudo`.
- `-s` scans and lists available interfaces.
- `-i` selects one or more interfaces for live capture.
- `-c` loads runtime settings from a JSON file.
- `-r` enables packet recording to Redis/DragonFly and validates the connection
  on startup.
- `-d` enables packet-level debug logs that include the interface name,
  addresses, protocol, and packet length.
- `--route` enables interface-to-interface forwarding using a route pair such as
  `eth0:eth1`.
- `--bidirectional` forwards traffic in both directions for the route pair.
- Routing forwards raw Ethernet frames, so ARP, ICMP, UDP, and TCP traffic all
  traverse the bridge transparently. TCP stream reassembly remains the
  responsibility of the connected endpoints.
- CLI flags override the JSON config for `-i`, `-r`, `-d`, `--route`, and
  `--bidirectional`.

## Config File

`elephantshrew.json` contains the runtime defaults:

```json
{
  "capture": {
    "interfaces": [],
    "record_packets": false,
    "debug_packets": false
  },
  "routing": {
    "enabled": false,
    "ingress_iface": "eth0",
    "egress_iface": "eth1",
    "bidirectional": false
  },
  "redis": {
    "host": "127.0.0.1",
    "port": "6379",
    "stream_key": "elephantshrew:packets",
    "max_pending_writes": 1024,
    "connect_timeout_ms": 5000,
    "drain_timeout_ms": 5000
  },
  "supervisor": {
    "restart_delay_ms": 2000,
    "poll_interval_ms": 200
  },
  "ui": {
    "show_startup_art": true
  }
}
```
