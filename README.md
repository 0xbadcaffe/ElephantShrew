# ElephantShrew

ElephantShrew captures live packets from one or more network interfaces. Packet
recording to the Redis-compatible stream `elephantshrew:packets` is enabled
with `-r`.

## Requirements

- A C++20 compiler
- `meson`
- `ninja`
- `pkg-config`
- Boost
- OpenSSL
- `spdlog`
- PcapPlusPlus
- Redis or DragonFly listening on `127.0.0.1:6379` when using `-r`

## Build

Run the build from the project directory that contains `meson.build`:

```bash
cd ElephantShrew
meson setup builddir
meson compile -C builddir
```

The binary will be created at:

```bash
./builddir/elephantshrew
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

List available interfaces without starting capture:

```bash
./builddir/elephantshrew -s
```

## Notes

- Packet capture commonly requires elevated privileges, which is why the
  examples use `sudo`.
- `-s` scans and lists available interfaces.
- `-i` selects one or more interfaces for live capture.
- `-r` enables packet recording to Redis/DragonFly and validates the connection
  on startup.
- `-d` enables packet-level debug logs that include the interface name,
  addresses, protocol, and packet length.
