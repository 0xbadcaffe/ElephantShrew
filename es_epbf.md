# 📡 ElephantShrew — Adding eBPF Support

An implementation guide for adding kernel-side network telemetry, selective sampling, filtering, and optional forwarding to ElephantShrew.

> **Status: design proposal, not an existing feature.** This guide is based on repository commit `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, inspected on September 19, 2026. The eBPF source files, configuration keys, build options, and CLI additions described below still need to be implemented. The kernel examples have not been compiled or verifier-tested as part of preparing this document.

**Recommended first implementation:** keep the existing PcapPlusPlus backend, add an optional TC observer with libbpf, and expose per-interface counters and sampled metadata. Do not start by replacing packet capture with AF_XDP or moving all forwarding into XDP.

## Contents

- [1. What eBPF adds](#1-what-ebpf-adds)
- [2. Choose the right hook](#2-choose-the-right-hook)
- [3. Fit it into the current repository](#3-fit-it-into-the-current-repository)
- [4. Dependencies and capability checks](#4-dependencies-and-capability-checks)
- [5. Minimal TC observer](#5-minimal-tc-observer)
- [6. Meson integration](#6-meson-integration)
- [7. Userspace loading and lifecycle](#7-userspace-loading-and-lifecycle)
- [8. Configuration and CLI](#8-configuration-and-cli)
- [9. Extend the observer into useful features](#9-extend-the-observer-into-useful-features)
- [10. Forwarding and AF_XDP](#10-forwarding-and-af_xdp)
- [11. USB and Wi-Fi interfaces](#11-usb-and-wi-fi-interfaces)
- [12. Testing and acceptance criteria](#12-testing-and-acceptance-criteria)
- [13. Delivery plan](#13-delivery-plan)
- [References](#references)

## 1. What eBPF adds

For this project, eBPF is a way to run small, verified programs at selected Linux networking hooks. The application loads the programs, attaches them to interfaces, and exchanges configuration and measurements through BPF maps. libbpf handles object loading and supports generated skeletons for the userspace integration. [1]

Keep packet processing responsibilities explicit:

| Feature | Proposed implementation | Scope |
|---|---|---|
| Interface statistics | Per-CPU counters, periodically aggregated in userspace | First release |
| Protocol statistics | Bounded Ethernet/VLAN/IP/transport parsing | Next release |
| Selective telemetry | Match interfaces, addresses, protocols, or ports before emitting events | Next release |
| Packet sampling | Emit metadata or bounded packet prefixes rather than every packet | First release for metadata |
| Flow summaries | Bounded flow map, packet/byte counters, first/last-seen timestamps | Later |
| Traffic policy | Explicit allow/drop rules at TC or XDP | Opt-in active mode |
| Traffic mirroring | Clone selected packets to a dedicated capture interface at TC | Later |
| Kernel forwarding | Redirect between explicitly configured interfaces | Separate forwarding backend |
| Accelerated userspace capture | AF_XDP queues and managed packet buffers | Optional advanced backend |

These are proposed features, not performance guarantees. Benchmark against the existing backend before making throughput or latency claims.

### Three operations that must not be confused

**Telemetry selection** means “do not report this packet.” The packet continues normally.

**Traffic filtering** means “drop this packet.” That changes connectivity.

**Packet redirection** means “send this packet to another destination.” It is not automatically a copy for the recorder.

For example, a socket filter can reject a packet from a capture socket without dropping it from the host's networking stack. A TC or XDP drop action has different consequences. Keep these meanings distinct in code, configuration, and logs. [2]

## 2. Choose the right hook

| Approach | Where it runs | Best use in ElephantShrew | Important limitation |
|---|---|---|---|
| Existing pcap backend | Userspace, fed by the capture mechanism | Full packet recording and compatibility | Existing parsing and storage overhead remains |
| Capture-socket filter | On a particular socket | Reduce packets delivered to that capture socket | Does not provide general interface forwarding or host-wide drops |
| TC / TCX | Interface ingress or egress, with an `skb` context | Initial telemetry, policies, and mirroring | Must coexist with existing networking programs and rules |
| Native XDP | Driver receive path | Early ingress counting, filtering, and redirection | Requires support for the requested actions on the actual devices |
| Generic XDP | Software receive path | Compatibility experiments | Not equivalent to native XDP performance |
| AF_XDP | XDP redirection into userspace queues | Specialized high-rate packet processing | Requires explicit buffer/queue ownership and packet disposition |

TCX provides a link-based attachment API for TC programs. Use it when the running kernel and installed libbpf support it; retain legacy TC attachment as an explicit compatibility path. Native and generic XDP are different modes, and AF_XDP copy mode is different from zero-copy mode. [3], [4], [5]

### Recommended architecture

```text
                     Linux networking
              +-----------------------------+
Interface --->| Optional eBPF observer      |---> Normal packet path
              | - count all observed skbs  |          |
              | - classify bounded headers|          | Existing pcap capture
              | - emit selected metadata  |          v
              +-------------+---------------+     PcapReceiver
                            |                         |
                  BPF maps / ring buffers             v
                            |                   Packet recording
                            v
                       EbpfManager
                            |
                    Bounded event queue
                            |
                Telemetry aggregation / output
```

This is a responsibility diagram, not a promise about the exact ordering of every capture tap relative to every hook. Validate packet visibility on the selected kernel and hook.

Keep three independent choices in the configuration:

- **Capture backend:** `none`, `pcap`, or, later, `af_xdp`.
- **eBPF hook:** `tc` or `xdp`, with explicit attachment modes.
- **Forwarding engine:** existing userspace `pcap`, or a future `tc`/`xdp` engine.

Only one engine may own a particular forwarding path. Never run userspace reinjection and kernel redirection for the same traffic at the same time.

## 3. Fit it into the current repository

The existing application creates one `PcapReceiver` for each capture interface and shares a packet store. In forwarding mode it creates receivers for one interface pair and optionally the reverse direction. `PcapReceiver::processPacket()` parses packets, optionally forwards them, and optionally records them. [R1], [R2]

### Changes to existing files

| File | Change |
|---|---|
| `inc/RuntimeConfig.hpp` | Add `EbpfOptions`; add explicit capture-backend and forwarding-engine selections. |
| `src/RuntimeConfig.cpp` | Parse and validate the new options; reject unsupported or contradictory combinations. |
| `src/ElephantShrewMain.cpp` | Add proposed CLI options, capability-reporting mode, and final effective-config validation. |
| `inc/ElephantShrew.hpp` | Own an `EbpfManager` whose lifetime is tied to the capture service. |
| `src/ElephantShrew.cpp` | Resolve interfaces once; start the requested observer and capture backend; unwind partial startup safely. |
| `src/PcapReceiver.cpp` | Keep full-packet capture working; disable reinjection when a kernel forwarding engine owns the path. |
| `int/IPacketStore.hpp` | Keep full-packet storage separate from telemetry, or extend the schema with explicit record kinds and lengths. |
| `meson_options.txt` / `meson.build` | Add optional libbpf dependencies, BPF compilation, and skeleton generation. |

The current `IReceiver` only defines `Receive()`. A sidecar `EbpfManager` does not need to pretend to be a raw-packet receiver. Introduce an explicit stop contract if a future AF_XDP receiver needs one, and update all affected implementations together. [R3]

### Proposed new files

```text
bpf/
    elephantshrew_shared.h       # Versioned C-compatible event/map structures
    elephantshrew_tc.bpf.c       # Initial TC observer
    elephantshrew_xdp.bpf.c      # Later XDP backend
    packet_parser.bpf.h         # Later bounded parser shared by programs
inc/
    EbpfManager.hpp              # Object, attachment, maps, consumer ownership
    EbpfCapabilities.hpp         # Feature probing and compatibility reporting
src/
    EbpfManager.cpp
    EbpfCapabilities.cpp
int/
    ITelemetrySink.hpp            # Metrics/events; not an assumed full packet
configs/
    ebpf-observe.json
    ebpf-hybrid.json
    ebpf-forward.json             # Later, active-mode configuration
tests/
    ebpf/                        # Config, verifier, lifecycle, namespace tests
README_EBPF.md
```

### Fix these integration hazards first

**Storage backpressure:** the current packet store waits when its pending-write limit is reached. Since the pcap callback calls `Store()` directly, asynchronous database I/O does not make that callback nonblocking. Do not reuse this blocking path in the ring-buffer callback. Add bounded, nonblocking enqueueing with explicit loss counters and cancellation-aware shutdown. [R2], [R4]

**Configuration precedence:** the JSON parser currently enables routing when either routing interface string is nonempty. The final design should validate the fully merged configuration after CLI overrides, give `routing.enabled` a documented meaning, and avoid accidentally enabling forwarding from example interface names. Unknown eBPF keys should be errors rather than silently ignored promises of protection. [R5], [R6]

**Tests:** the current `test('basic', exe)` launches the long-running service without a terminating mode. Replace it with a deterministic test, such as a newly implemented `--help` or `--check-config` path, before adding privileged integration tests. [R6], [R7]

## 4. Dependencies and capability checks

Use a maintained Linux kernel on the target machine and test the exact capabilities required by each mode. Do not use the version string as the only compatibility test.

For a Debian development machine, the additional build and inspection packages can be installed with:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
    clang llvm libbpf-dev libelf-dev zlib1g-dev bpftool \
    pkg-config meson ninja-build iproute2 ethtool
```

This is in addition to ElephantShrew's existing dependencies. Package names and availability vary across distributions; Debian publishes `bpftool` separately. [R8], [6]

Inspect the target before attaching anything:

```bash
uname -r
clang --print-targets | grep -i bpf
pkg-config --modversion libbpf
bpftool version

# Needed by the BTF/CO-RE workflow used below.
test -r /sys/kernel/btf/vmlinux && echo "Kernel BTF available"

# Kernel capabilities, map types, program types, and helper availability.
sudo bpftool feature probe kernel

# Existing programs and attachment state.
sudo bpftool prog show
sudo bpftool link show
sudo bpftool net show
```

For an explicitly selected interface:

```bash
IFACE=eth0
ip -details link show dev "$IFACE"
ethtool -i "$IFACE"
sudo tc qdisc show dev "$IFACE"
sudo tc filter show dev "$IFACE" ingress
sudo tc filter show dev "$IFACE" egress
```

`ethtool` operations may be unavailable for some virtual or wireless devices. A failed driver-information query is not itself proof that TC cannot work.

For the initial legacy-TC implementation, check classifier/action support, the required BPF map types and helpers, and successful attachment in an isolated test. Kernel configuration commonly involves `CONFIG_BPF`, `CONFIG_BPF_SYSCALL`, `CONFIG_NET_CLS_BPF`, `CONFIG_NET_CLS_ACT`, and `CONFIG_NET_SCH_INGRESS`. BTF-based portability additionally depends on usable target type information. [1], [2]

### Privileges and deployment

Use `sudo` in an isolated development lab. For deployment, give the loading component only the capabilities it actually needs. Networking BPF operations can require `CAP_BPF` and `CAP_NET_ADMIN`; packet sockets involve `CAP_NET_RAW`, and tracing adds its own requirements. Kernel versions and security policy affect these checks, so there is no universal capability command for every backend. [7]

Keep the unprivileged telemetry/storage worker separate from attachment management where practical. Do not disable system security controls to make a probe succeed. Report permission failures distinctly from unsupported helpers, verifier rejection, and an occupied attachment point.

### CO-RE and cross-compilation

Generate `vmlinux.h` from a chosen BTF baseline, compile the BPF source, and generate a libbpf skeleton. CO-RE adapts relevant type relocations; it does not manufacture missing helpers, driver actions, or kernel features. [1]

For cross-builds, run Clang and bpftool on the build machine, select BPF bytecode endianness for the target, and supply an appropriate BTF baseline explicitly. Do not silently use the build host's kernel as the target compatibility contract.

## 5. Minimal TC observer

This starter counts observed packets and bytes on each direction and emits one small metadata event for every N packets on each CPU. It does not parse protocols, copy payloads, drop traffic, or forward packets.

Use one loaded object per interface initially. That makes map ownership and cleanup straightforward. Each object can supply an ingress program and an egress program; one userspace ring-buffer manager can consume several map instances.

### Shared ABI: `bpf/elephantshrew_shared.h`

```c
#ifndef ELEPHANTSHREW_SHARED_H
#define ELEPHANTSHREW_SHARED_H

/* vmlinux.h supplies these types in the BPF translation unit. */
#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

#define ES_ABI_VERSION 1

enum es_direction {
    ES_INGRESS = 0,
    ES_EGRESS = 1,
};

struct es_counters {
    __u64 packets;
    __u64 bytes;
    __u64 events;
    __u64 ringbuf_lost;
};

struct es_event {
    __u64 timestamp_mono_ns;
    __u64 sequence;          /* Packet count within one CPU/direction. */
    __u32 ifindex;
    __u32 skb_len;
    __u32 cpu;
    __u16 abi_version;
    __u8 direction;
    __u8 reserved;
};

#ifdef __cplusplus
static_assert(sizeof(es_event) == 32, "Unexpected event ABI layout");
static_assert(sizeof(es_counters) == 32, "Unexpected counter ABI layout");
#else
_Static_assert(sizeof(struct es_event) == 32, "Unexpected event ABI layout");
_Static_assert(sizeof(struct es_counters) == 32, "Unexpected counter ABI layout");
#endif

#endif
```

The ABI is a local kernel/userspace interface, not a portable serialized file format. Use a separate serialization format for persisted or remotely transmitted records.

### Kernel program: `bpf/elephantshrew_tc.bpf.c`

```c
/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "elephantshrew_shared.h"

/* TC_ACT_UNSPEC: continue classification rather than override other policy. */
#define ES_TC_CONTINUE (-1)

/* Set through the skeleton's rodata before loading. Zero disables events. */
const volatile __u32 sample_every = 128;

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, struct es_counters);
} counters SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 8 * 1024 * 1024);
} events SEC(".maps");

static __always_inline int observe(struct __sk_buff *skb, __u32 direction)
{
    struct es_counters *stats;
    struct es_event *event;
    __u32 key = direction;
    __u32 n = sample_every;

    stats = bpf_map_lookup_elem(&counters, &key);
    if (!stats)
        return ES_TC_CONTINUE;

    stats->packets++;
    stats->bytes += skb->len;

    if (n == 0 || stats->packets % n != 0)
        return ES_TC_CONTINUE;

    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        stats->ringbuf_lost++;
        return ES_TC_CONTINUE;
    }

    event->timestamp_mono_ns = bpf_ktime_get_ns();
    event->sequence = stats->packets;
    event->ifindex = skb->ifindex;
    event->skb_len = skb->len;
    event->cpu = bpf_get_smp_processor_id();
    event->abi_version = ES_ABI_VERSION;
    event->direction = (__u8)direction;
    event->reserved = 0;

    bpf_ringbuf_submit(event, 0);
    stats->events++;
    return ES_TC_CONTINUE;
}

SEC("tc")
int es_ingress(struct __sk_buff *skb)
{
    return observe(skb, ES_INGRESS);
}

SEC("tc")
int es_egress(struct __sk_buff *skb)
{
    return observe(skb, ES_EGRESS);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
```

The example's BPF source declares its own license; it does not change the existing application source files.

### Semantics to preserve

`ES_TC_CONTINUE` is the legacy TC `TC_ACT_UNSPEC` value. A passive observer should not unconditionally return `TC_ACT_OK` and terminate classification before another policy program. The equivalent continuation verdict for TCX is `TCX_NEXT`; an XDP observer normally returns `XDP_PASS`. Select program sections and return semantics for the attachment API you actually use. [8], [9], [19]

A full ring buffer loses a telemetry event, not the network packet. Reservation is nonblocking; always count failed reservations. The configured ring size must satisfy the kernel's size requirements, including a power-of-two size. [10]

`sample_every` here is per CPU and per direction, not a globally exact “every 128th packet” sequence. Counters count all observed skbs regardless of sampling. Timestamps from `bpf_ktime_get_ns()` are monotonic and exclude suspended time; they are not Unix timestamps. [11]

The sample intentionally avoids direct packet-memory parsing. Add protocol parsing separately with explicit bounds checks and verifier tests, rather than obscuring attachment and lifecycle debugging with a parser in the first commit.

## 6. Meson integration

Build the kernel program with Clang targeting BPF. Continue building the application with its selected C++ compiler. Do not pass the application's `-march=native`, host LTO, or C++ flags into the BPF compilation command.

### Add options to `meson_options.txt`

```meson
option('ebpf', type: 'boolean', value: false,
       description: 'Build the optional eBPF observer')
option('bpf_btf', type: 'string', value: '/sys/kernel/btf/vmlinux',
       description: 'BTF baseline used to generate vmlinux.h')
```

A disabled build must not require Clang's BPF backend, bpftool, kernel BTF, or libbpf.

### Add generation targets before the executable definition

The following is an integration fragment, not a replacement for the entire current `meson.build`. It assumes the two BPF source/header files above and the new `EbpfManager.cpp` implementation exist.

```meson
ebpf_deps = []
ebpf_generated = []

if get_option('ebpf')
  if host_machine.system() != 'linux'
    error('ElephantShrew eBPF support requires a Linux target')
  endif

  bpf_clang = find_program('clang', native: true)
  bpftool = find_program('bpftool', native: true)
  libbpf_dep = dependency('libbpf', method: 'pkg-config')
  ebpf_deps = [libbpf_dep, dependency('libelf'), dependency('zlib')]

  vmlinux_h = custom_target(
    'es-vmlinux',
    input: get_option('bpf_btf'),
    output: 'vmlinux.h',
    command: [bpftool, 'btf', 'dump', 'file', '@INPUT@', 'format', 'c'],
    capture: true,
  )

  bpf_target = host_machine.endian() == 'little' ? 'bpfel' : 'bpfeb'
  bpf_includedir = libbpf_dep.get_variable(pkgconfig: 'includedir')

  tc_object = custom_target(
    'es-tc-object',
    input: 'bpf/elephantshrew_tc.bpf.c',
    output: 'elephantshrew_tc.bpf.o',
    command: [
      bpf_clang, '-target', bpf_target, '-O2', '-g', '-Wall', '-Werror',
      '-I' + meson.current_build_dir(),
      '-I' + join_paths(meson.current_source_dir(), 'bpf'),
      '-I' + bpf_includedir,
      '-c', '@INPUT@', '-o', '@OUTPUT@',
    ],
    depends: vmlinux_h,
    depend_files: files('bpf/elephantshrew_shared.h'),
  )

  tc_skeleton = custom_target(
    'es-tc-skeleton',
    input: tc_object,
    output: 'elephantshrew_tc.skel.h',
    command: [bpftool, 'gen', 'skeleton', '@INPUT@'],
    capture: true,
  )

  ebpf_generated = [tc_skeleton]
  sources += files('src/EbpfManager.cpp')
  add_project_arguments('-DELEPHANTSHREW_HAS_EBPF=1', language: 'cpp')
endif
```

Then extend the existing executable rather than defining a second one:

```meson
exe = executable(
  'elephantshrew',
  sources + ebpf_generated,
  include_directories: inc_dirs,
  dependencies: [openssl, spdlog_dep, pcapplusplus_dep, boost_dep] + ebpf_deps,
  install: true,
)
```

Meson generation dependencies ensure the skeleton exists before the C++ source includes it. `capture: true` writes bpftool's stdout to the output header without relying on shell redirection inside the Meson command. [12]

In `EbpfManager.cpp`, include `elephantshrew_tc.skel.h`; the root build directory contains the generated header. The skeleton embeds the BPF object, so that object does not need a separate runtime file lookup. Guard integration call sites for builds without eBPF. [1]

After implementing the files and build changes:

```bash
meson setup build/ebpf -Debpf=true
meson compile -C build/ebpf

# Verify the original, dependency-light build still works.
meson setup build/pcap -Debpf=false
meson compile -C build/pcap
```

For reproducible builds, pass `-Dbpf_btf=/path/to/versioned/baseline.btf` and record the compiler, libbpf, and bpftool versions. Add a configuration-time compile probe for Clang's BPF target; finding a binary named `clang` is insufficient.

## 7. Userspace loading and lifecycle

`EbpfManager` should own the objects, attachment records, map handles, consumer thread, and telemetry queue. Use RAII, but do not assume every attachment disappears merely because an object was destroyed.

### Startup sequence

1. Resolve configured names with `if_nametoindex()` in the intended network namespace. Reject missing or duplicate interfaces and inspect existing attachments.
2. Open one skeleton per interface with `elephantshrew_tc_bpf__open()`. Before loading, set `rodata->sample_every`, adjust map capacities, and disable any program variants not supported by the chosen attachment API.
3. Load with `elephantshrew_tc_bpf__load()`. Capture libbpf/verifier diagnostics and initialize required map values before activating the program.
4. Create the consumer with `ring_buffer__new()`; add additional event maps with `ring_buffer__add()`. Prepare the bounded userspace queue before attaching producers.
5. Attach ingress and/or egress programs to the exact interface. Record ownership immediately after each successful attachment.
6. Run `ring_buffer__poll()` with a bounded timeout, and aggregate counters on a separate periodic path. Publish the actual attachment mode, interface identity, and feature availability.

These names follow libbpf's generated skeleton and ring-buffer APIs. Interface-specific TC attachments are explicit; the generic skeleton `__attach()` call should not be assumed to choose the desired interfaces. [1], [3]

### Legacy TC versus TCX

For **legacy TC**, use `bpf_tc_hook_create()`, accepting an existing `clsact` only after inspection, then `bpf_tc_attach()` for each direction. Allocate and record a unique handle/priority; do not use replacement flags against an unknown program. On shutdown, query and detach only your own filters. Do not delete a shared qdisc. A legacy attachment can survive application exit, including a crash. [3]

For **TCX**, use appropriately typed programs, such as `SEC("tcx/ingress")` and `SEC("tcx/egress")`, and `bpf_program__attach_tcx()`. Retain and destroy the returned links. Unpinned links are tied to their references; pinning or passing a reference changes their lifetime. Do not load TCX-only variants on an unsupported kernel merely because the legacy path is available. [3], [9]

The starter source uses legacy `SEC("tc")` programs. A production `attach_api=auto` implementation therefore needs tested program selection or separate object variants; changing a string in the configuration is not sufficient.

### Event handling and storage

In the C callback, validate event size, ABI version, and direction before decoding. Copy the event into owned memory before returning; the ring-buffer record must not be retained as a borrowed pointer. Catch exceptions at the C++/C boundary.

The callback should attempt a bounded enqueue and return promptly. It must not wait for a database, format a log line per packet, or block on a full storage queue. Track userspace queue drops independently from kernel ring-buffer reservation failures.

For per-CPU maps, size the lookup buffer using the number of **possible** CPUs, not just online CPUs, and the per-CPU value stride expected by the BPF API. Aggregate cumulative counters and compute deltas; do not periodically zero live counters while the program is updating them. [3], [13]

Recommended operational counters are:

```text
observed_packets / observed_bytes
telemetry_events_submitted / telemetry_ringbuf_lost
telemetry_queue_dropped / telemetry_decode_errors
policy_dropped / redirect_errors / parser_unsupported
storage_queue_dropped / storage_write_errors
```

Keep these separate. “No telemetry event” does not necessarily mean “network packet lost.”

### Shutdown and failure handling

Stop accepting configuration changes, detach owned producers, finish a bounded telemetry drain, stop/join consumer workers, and only then destroy map/object resources and sinks. Cancel blocked storage work before joining workers that depend on it.

Unwind a partial startup in reverse order. Make shutdown idempotent. Monitor interface deletion and recreation: an interface name can acquire a different index, and an index is scoped to a network namespace.

Passive telemetry failure may leave normal networking intact while reporting degraded monitoring. Active filtering and forwarding need an explicit failure policy. Returning `XDP_PASS` is not a physical bypass between two separate ports, and detaching the only forwarding program can stop connectivity.

## 8. Configuration and CLI

**All additions in this section are proposed. The current executable does not implement them.** Keep `-c` explicit; the existing program loads JSON when a configuration path is supplied. [R6]

### Metrics-only observation: `configs/ebpf-observe.json`

```json
{
  "capture": {
    "interfaces": ["es-left", "es-right"],
    "backend": "none",
    "record_packets": false,
    "debug_packets": false
  },
  "ebpf": {
    "enabled": true,
    "hook": "tc",
    "attach_api": "legacy",
    "mode": "observe",
    "directions": ["ingress", "egress"],
    "required": true,
    "sample_every": 128,
    "snapshot_bytes": 0,
    "ring_buffer_bytes": 8388608,
    "event_queue_capacity": 8192,
    "stats_interval_ms": 1000
  }
}
```

This selects the starter's legacy-TC path and avoids opening pcap handles. The example ring size is **per interface** under the initial one-object-per-interface design; account for all instances and CPUs when setting memory budgets.

For hybrid operation, change `capture.backend` to `pcap` and enable `record_packets`. The eBPF observer remains a separate telemetry source; its metadata events do not replace full packets in the recorder. The existing storage configuration still applies.

Suggested future CLI additions:

| Proposed option | Meaning |
|---|---|
| `--ebpf-probe` | Report kernel, interface, hook, and attachment compatibility without starting the service |
| `--ebpf` | Enable the observer with explicit defaults |
| `--ebpf-hook tc` | Select TC rather than XDP |
| `--ebpf-mode observe` | Prohibit traffic-dropping and redirection rules |
| `--check-config` | Validate the final configuration and exit without attaching |
| `--help`, `--version` | Terminating, unprivileged inspection paths |

Additional policy, map-size, and backend options can remain JSON-only until their semantics are stable.

### Validation rules

Reject contradictory configurations before attaching: `backend=none` with packet recording; `mode=observe` with dropping or eBPF forwarding; a native-XDP requirement with silent generic fallback; egress observation requested from an ingress-only XDP program; and duplicate forwarding ownership.

Require positive queue sizes and poll intervals, a supported ring size, valid interface identities, and bounded snapshot lengths. Define `sample_every=0` as counters-only and `sample_every=1` as one attempted event per observed packet. `required=true` means startup fails if the requested observer cannot be established; it is not a packet-drop policy.

Report both requested and effective modes. Never quietly replace active filtering with plain pcap capture and claim the policy is still enforced.

## 9. Extend the observer into useful features

### Protocol classification

Add a shared, bounded parser that reports Ethernet type, VLAN identifiers, IP version, transport protocol, addresses, and ports when available.

Check every access against accessible packet bounds. Validate IPv4 header length; do not read transport ports from noninitial fragments. Walk IPv6 extension headers with an explicit work limit and report unsupported or truncated cases instead of guessing. Account for VLAN tags represented in metadata rather than inline bytes. TC skbs can be nonlinear; use an appropriate helper or a deliberately bounded linear-data strategy. Helpers that change packet storage can invalidate prior pointer checks. [2], [11], [14]

Keep `parser_unsupported` separate from `policy_dropped`. In observation mode, an unfamiliar header is not a reason to discard traffic.

### Flow summaries and live rule updates

A useful directional flow key includes the network namespace identity, interface, direction, IP version, protocol, source/destination addresses, and ports where valid. Add first/last-seen times and packet/byte counters. Label bidirectional aggregation as a separate userspace operation.

Use a bounded hash/LRU map for flow state. LRU bounds capacity through eviction; it does not guarantee exact long-term accounting or automatically implement a flow-expiration protocol. Concurrent shared values need correct synchronization. Per-CPU designs reduce sharing but change memory cost and aggregation semantics. [13]

For live configuration, use writable maps rather than the starter's load-time `rodata`. Validate a complete new rule set before activating it. For a multi-map policy, use a generation/indirection design rather than assuming several independent map updates form one transaction.

### Sampling, recording, and capture filters

Metadata-only sampling is the default. A later event schema can carry a bounded prefix with explicit `captured_len`, `original_len`, link type, hook, direction, and truncation flags. Do not describe a 128-byte prefix as a complete recorded packet.

The existing `PacketInfo` has a single `length`, hex data, and `timestamp_us`; it does not encode this richer distinction. Either extend it deliberately or create a separate telemetry record type. Never store the eBPF monotonic clock directly in a field whose consumers expect epoch time. [R9]

A telemetry rule that suppresses an eBPF event does **not** suppress the same packet from an independent pcap recorder. To reduce pcap delivery, add a capture-socket filter through the capture backend, or introduce a specifically designed selective mirroring/capture path. Do not substitute a network drop for a recording filter. [2]

### Active policies

Start with exact address/protocol/port matches and per-rule counters. Default to pass until the operator explicitly enables enforcement. CIDR lookup, bounded rate limiting, and more complex policies should follow tested rule precedence and update behavior.

A per-CPU token bucket is not automatically a single global rate limit: independently granting the full configured budget on every CPU can multiply the allowed traffic. Keep globally coordinated and approximate per-CPU rate limits distinct.

Use counters and rate-limited summaries for policy diagnostics. Do not put an unbounded application-protocol parser or per-packet text logger on the forwarding path.

## 10. Forwarding and AF_XDP

### TC forwarding and mirroring

For kernel forwarding, move the interface-pair decision into configuration-backed maps and redirect on the selected ingress path. TC can also clone a packet toward a dedicated monitoring interface, which is useful when the original must keep its normal path. Cloning and redirection are different operations. [11]

Preserve the current bridge-like scope: raw Layer-2 forwarding does not automatically implement IP routing, neighbor resolution, TTL/hop-limit updates, NAT, a learning bridge, or spanning-tree loop prevention. Specify which traffic is forwarded and how unsupported link types, MTU mismatches, and interface removal are handled.

Do not attach a kernel forwarder while the existing `route_device_->sendPacket()` path is active for the same pair. If recording forwarded traffic is required, design and test the copy path explicitly; a hook may consume the original before another observer sees it. [R2]

### Native XDP forwarding

Use XDP only after testing receive-side support and the destination device's transmit/redirection support. A redirect target in a map is not proof of successful transmission. Surface redirect errors and test the actual device pair. XDP redirect processing includes deferred work, and driver support matters on both sides. [4]

Do not assume pcap sees packets dropped or redirected before the ordinary stack. Use independent observation points and counters when validating policy effects. Avoid replacing an existing XDP program without ownership checks; consider libxdp-managed composition when coexistence is required. [15]

### Optional `AfXdpReceiver`

AF_XDP is a separate receiver implementation, not a ring-buffer setting. It needs UMEM allocation, RX/TX and fill/completion rings, queue binding, and an XSK map connecting XDP decisions to userspace sockets. Each redirected frame must match the receiving socket's interface/queue binding. [5]

Make packet-buffer ownership explicit. Do not return a buffer to the fill ring while the asynchronous recorder still references it; copy it or keep ownership until all consumers finish. Cover descriptor recycling, queue exhaustion, shutdown, and multi-buffer packets in tests.

Zero-copy requires suitable driver support. Report actual copy/zero-copy mode; do not infer it from native-XDP attachment. Redirected packets are not automatically also delivered to the host stack or pcap, so userspace must define whether to process, forward, or discard them. [5]

**Do not implement AF_XDP until measurements show that pcap delivery is a limiting factor rather than packet formatting or storage.**

## 11. USB and Wi-Fi interfaces

Probe every interface independently. Several adapters can expose several network interfaces, but that does not imply native XDP, redirect transmission, or AF_XDP zero-copy support on each driver.

For an initial multi-USB-adapter experiment, prefer TC observation with the existing capture backend as needed. Keep native XDP and zero-copy opt-in until they pass tests on the exact kernel/driver combination. Generic XDP is a compatibility possibility, not a promise of equivalent performance. [4], [5]

Wi-Fi managed-mode traffic and raw 802.11 monitor-mode traffic are different capture environments. A monitor interface may deliver radiotap/802.11 frames rather than Ethernet, so reject an incompatible link type instead of running an Ethernet parser over it. Bridging through a Wi-Fi station can require four-address support and compatible configuration at the other end; arbitrary source-MAC forwarding is not guaranteed. [16]

eBPF does not remove USB bandwidth/power limits, add missing firmware features, or provide monitor-mode capture by itself. The README should record tested adapters and modes only after actual validation, not infer compatibility from a product name.

## 12. Testing and acceptance criteria

Use virtual interfaces before attaching active programs to physical or remotely managed interfaces. The following lab creates two namespaces and a temporary Linux bridge so that traffic flows **without** ElephantShrew forwarding it.

### Create an isolated observation lab

Run in a test VM or development machine. The script refuses to reuse its namespace/interface names; it does not change the default route or management interface.

```bash
#!/usr/bin/env bash
set -euo pipefail

for ns in es-src es-dst; do
    if ip netns list | awk '{print $1}' | grep -qx "$ns"; then
        echo "Refusing to reuse namespace: $ns" >&2
        exit 1
    fi
done
for dev in es-left es-right es-src0 es-dst0 es-br; do
    if ip link show dev "$dev" >/dev/null 2>&1; then
        echo "Refusing to reuse interface: $dev" >&2
        exit 1
    fi
done

sudo ip netns add es-src
sudo ip netns add es-dst
sudo ip link add es-left type veth peer name es-src0
sudo ip link add es-right type veth peer name es-dst0
sudo ip link set es-src0 netns es-src
sudo ip link set es-dst0 netns es-dst
sudo ip link add es-br type bridge
sudo ip link set es-left master es-br
sudo ip link set es-right master es-br
sudo ip link set es-left up
sudo ip link set es-right up
sudo ip link set es-br up
sudo ip -n es-src link set lo up
sudo ip -n es-dst link set lo up
sudo ip -n es-src link set es-src0 up
sudo ip -n es-dst link set es-dst0 up
sudo ip -n es-src addr add 192.0.2.1/24 dev es-src0
sudo ip -n es-dst addr add 192.0.2.2/24 dev es-dst0
sudo ip netns exec es-src ping -c 5 192.0.2.2
```

After the proposed implementation is available, run the observer in one terminal:

```bash
sudo ./build/ebpf/elephantshrew -c configs/ebpf-observe.json
```

Generate traffic from another terminal:

```bash
sudo ip netns exec es-src ping -c 200 192.0.2.2
```

For a deterministic event-delivery test, temporarily set `sample_every` to `1` in the lab configuration; short tests with per-CPU sampling at `128` may emit no events. Verify connectivity is unchanged, statistics increase, and events carry the correct interface/direction. One forwarded packet can be observed at multiple interfaces and directions; summing all hook counters is not a unique-packet count.

Stop ElephantShrew before cleanup. Run this cleanup only for the lab created above; deleting these virtual devices also removes their device-bound networking configuration.

```bash
sudo ip link del es-br
sudo ip link del es-left
sudo ip link del es-right
sudo ip netns del es-src
sudo ip netns del es-dst
```

For later forwarding tests, remove the temporary bridge from the topology and let exactly one ElephantShrew forwarding engine connect the two host-side veth interfaces. Otherwise the lab may pass even when the proposed forwarding engine does nothing.

### Test matrix

| Area | Required check |
|---|---|
| Optional build | `ebpf=false` works without any BPF build dependencies. |
| Configuration | Unknown keys, invalid enums, illegal sizes, and contradictory modes fail before attachment. |
| Passive behavior | With existing policy installed, the observer neither drops packets nor bypasses that policy. |
| Multi-interface capture | Correct interface/direction labels and independent cleanup on each interface. |
| Verifier behavior | Each supported object loads on the declared kernel matrix; rejection reports a useful log. |
| Parsing | ARP, IPv4/IPv6, TCP/UDP, VLAN, fragments, extension headers, truncation, and malformed input. |
| Backpressure | A stalled consumer/storage sink increases telemetry-loss counters without blocking observation or forwarding. |
| Lifecycle | Ctrl+C, SIGTERM, partial startup, process crash, interface deletion, and repeated restart. |
| Ownership | An unrelated TC/XDP program and shared qdisc remain intact after startup failure and shutdown. |
| Active policy | Allowed traffic passes; matching denied traffic stops; nonmatching traffic and unrelated interfaces remain unaffected. |
| Forwarding | Both directions, ARP/IPv6 neighbor discovery, MTU cases, target removal, and no duplicate transmission. |
| Hardware mode | Requested versus actual native/generic XDP and copy/zero-copy AF_XDP modes are reported accurately. |

Use `BPF_PROG_TEST_RUN` / `bpf_prog_test_run_opts()` for deterministic supported program tests with explicit input/context. It does not replace namespace and hardware attachment tests. Keep ordinary CI unprivileged; run verifier/network tests in a dedicated privileged VM/job. [17]

### Measurements

Compare pcap-only, TC counters-only, TC sampled telemetry, hybrid recording, and any later XDP/AF_XDP backend with the same traffic and storage settings.

Measure received/delivered packets, CPU use, memory, tail latency, capture loss, telemetry loss, and storage loss separately. TC sees skbs, and segmentation/receive offloads can change the relationship between skbs and wire packets. Report offload settings and avoid claiming exact wire-level equivalence from skb counts. [18]

Do not merge the first release until passive behavior, bounded memory, attachment ownership, and shutdown under a stalled sink have been demonstrated.

### Validation of this document

The shared ABI header was compiled as both C and C++, including its size assertions. The JSON example, Bash syntax, Markdown code fences, section links, and reference labels were checked. The BPF program, Meson integration, libbpf loader, and network lab have not been built or run here; they still require validation on a BPF-capable Linux test system.

## 13. Delivery plan

| Change set | Deliverable | Done when |
|---|---|---|
| 1 — Build and configuration | Optional build, capability report, deterministic config checks | Existing pcap-only behavior stays intact; unsupported requests fail clearly |
| 2 — Passive TC observer | Shared ABI, counters, sampled metadata, loader, safe cleanup | Isolated multi-interface lab passes; no traffic-policy side effects |
| 3 — Reliable telemetry | Bounded queue, loss metrics, periodic reporting, lifecycle tests | Consumer/storage failure does not stall the packet path |
| 4 — Classification | Bounded parser, protocol counters, metadata filters | Malformed/fragmented/VLAN/IPv6 cases have defined, tested outcomes |
| 5 — Flow and recording integration | Flow summaries, explicit snapshot schema, capture-filter semantics | Metadata, sampled prefixes, and complete packets remain distinguishable |
| 6 — Optional active datapath | TC policies/mirroring, then measured XDP forwarding | Ownership, rollback, compatibility, and policy tests pass |
| 7 — Optional AF_XDP | Dedicated receiver and buffer lifecycle | Benchmarks justify it and exhaustion/shutdown tests pass |

**First useful milestone:** run ElephantShrew against two virtual interfaces, keep normal connectivity unchanged, show per-interface ingress/egress counters, emit sampled metadata, and remove only its own attachments on shutdown.

## References

Repository links are pinned to the inspected commit. External references document the mechanisms used by the proposal; they do not establish that ElephantShrew already implements them.

[1]: https://docs.kernel.org/bpf/libbpf/libbpf_overview.html
[2]: https://docs.kernel.org/networking/filter.html
[3]: https://github.com/libbpf/libbpf/blob/master/src/libbpf.h
[4]: https://docs.kernel.org/bpf/redirect.html
[5]: https://docs.kernel.org/networking/af_xdp.html
[6]: https://packages.debian.org/trixie/bpftool
[7]: https://man7.org/linux/man-pages/man7/capabilities.7.html
[8]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/pkt_cls.h
[9]: https://docs.kernel.org/bpf/libbpf/program_types.html
[10]: https://docs.kernel.org/bpf/ringbuf.html
[11]: https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
[12]: https://mesonbuild.com/Reference-manual_functions_custom_target.html
[13]: https://docs.kernel.org/bpf/map_hash.html
[14]: https://docs.kernel.org/bpf/verifier.html
[15]: https://github.com/xdp-project/xdp-tools/tree/main/lib/libxdp
[16]: https://wireless.docs.kernel.org/en/latest/en/users/documentation/iw.html
[17]: https://docs.kernel.org/bpf/bpf_prog_run.html
[18]: https://docs.kernel.org/networking/segmentation-offloads.html
[19]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/bpf.h
[R1]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/ElephantShrew.cpp
[R2]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/PcapReceiver.cpp
[R3]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/int/IReceiver.hpp
[R4]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/RedisPacketStore.cpp
[R5]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/RuntimeConfig.cpp
[R6]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/src/ElephantShrewMain.cpp
[R7]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/meson.build
[R8]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/README.md
[R9]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/int/IPacketStore.hpp

| Reference | Source |
|---|---|
| [1] | Linux kernel documentation — libbpf overview and CO-RE |
| [2] | Linux kernel documentation — socket filtering |
| [3] | libbpf public API definitions |
| [4] | Linux kernel documentation — XDP redirect |
| [5] | Linux kernel documentation — AF_XDP |
| [6] | Debian — bpftool package |
| [7] | Linux man-pages — capabilities |
| [8] | Linux UAPI — TC verdicts |
| [9] | Linux kernel documentation — BPF program sections |
| [10] | Linux kernel documentation — BPF ring buffer |
| [11] | Linux man-pages — BPF helper contracts |
| [12] | Meson — custom generation targets |
| [13] | Linux kernel documentation — hash, per-CPU, and LRU maps |
| [14] | Linux kernel documentation — verifier |
| [15] | XDP project — libxdp |
| [16] | Linux Wireless documentation — iw and interface modes |
| [17] | Linux kernel documentation — userspace BPF program testing |
| [18] | Linux kernel documentation — segmentation offloads |
| [19] | Linux UAPI — BPF contexts, actions, and attachment types |
| [R1]–[R9] | ElephantShrew implementation at the pinned commit |
