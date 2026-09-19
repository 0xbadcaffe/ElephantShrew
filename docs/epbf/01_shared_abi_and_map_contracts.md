# 01 — Shared ABI and map contracts

ElephantShrew eBPF implementation series · lowest layer: kernel/userspace data contract

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Define one C-compatible ABI before writing either producer or consumer. This chapter has no libbpf dependency. Keep all paths relative to the repository root. The recommended location for these ten documents is `docs/ebpf/`; their next/previous links are relative to each other.

The original design used a 32-byte version-1 event. This series deliberately introduces **version 2**, a fixed 216-byte event with a tuple and optional prefix. Do not silently keep the version-1 number while changing the layout. The original packet recorder remains a different data model.

## Reading order

01. [Shared ABI and map contracts](01_shared_abi_and_map_contracts.md)
02. [Bounded packet parser](02_bounded_packet_parser.md)
03. [TC observer, sampling, and flow accounting](03_tc_observer_sampling_and_flows.md)
04. [Policy, mirroring, and XDP datapaths](04_policy_mirroring_and_xdp.md)
05. [Meson and the BPF build pipeline](05_meson_bpf_build_pipeline.md)
06. [libbpf loader and attachment lifecycle](06_libbpf_loader_and_attachment_lifecycle.md)
07. [Telemetry queues, counters, and sinks](07_telemetry_queues_counters_and_sinks.md)
08. [Runtime configuration and CLI](08_runtime_config_and_cli.md)
09. [Application and capture integration](09_application_and_capture_integration.md)
10. [Tests, deployment, and delivery](10_tests_deployment_and_delivery.md)

The numbering is architectural, from kernel contracts to service operation. For the first runnable passive milestone, implement 01–03 and 05–09, then run 10. Chapter 04 and the AF_XDP subsection of 09 are explicit later milestones; do not enable their configuration values before their acceptance tests pass.

## File changes

```text
ElephantShrew/
├── bpf/                              [ADD]
│   └── elephantshrew_shared.h         [ADD]
├── tests/ebpf/                        [ADD]
│   ├── abi_c.c                       [ADD]
│   └── abi_cpp.cpp                   [ADD]
└── int/IPacketStore.hpp              [KEEP: complete-packet storage contract]
```

No C++ class, STL object, pointer, `bool`, `size_t`, or compiler-packed structure belongs in this ABI. Explicitly zero unused address bytes, padding, and reserved members before creating a hash key or publishing an event.

### `bpf/elephantshrew_shared.h` — complete file

```c
#ifndef ELEPHANTSHREW_SHARED_H
#define ELEPHANTSHREW_SHARED_H
#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

#define ES_ABI_VERSION 2u
#define ES_PARSE_BYTES 128u
#define ES_SNAPSHOT_MAX 128u
#define ES_LINKTYPE_ETHERNET 1u
#define ES_TC_NEXT (-1)
#define ES_TC_DROP 2
#define ES_TC_REDIRECT 7

enum es_direction { ES_INGRESS = 0, ES_EGRESS = 1, ES_DIRECTIONS = 2 };
enum es_hook { ES_HOOK_TC = 1, ES_HOOK_XDP = 2 };
enum es_parse_status {
    ES_PARSE_OK = 0, ES_PARSE_TRUNCATED = 1, ES_PARSE_UNSUPPORTED = 2,
    ES_PARSE_MALFORMED = 3, ES_PARSE_FRAGMENT = 4, ES_PARSE_SKIPPED = 5
};
enum es_tuple_flags {
    ES_T_L2_VALID = 1u << 0, ES_T_ADDR_VALID = 1u << 1,
    ES_T_PORTS_VALID = 1u << 2, ES_T_FRAGMENT = 1u << 3
};
enum es_event_flags {
    ES_E_PREFIX_TRUNCATED = 1u << 0, ES_E_COPY_FAILED = 1u << 1,
    ES_E_VLAN_STRIPPED = 1u << 2
};
enum es_counter_id {
    ES_C_PACKETS, ES_C_BYTES, ES_C_SELECTED, ES_C_EVENTS, ES_C_RING_LOST,
    ES_C_PARSE_PARTIAL, ES_C_PARSE_MALFORMED, ES_C_SNAPSHOT_FAILED,
    ES_C_FLOW_UPDATE_FAILED, ES_C_POLICY_DROP, ES_C_MIRROR_ATTEMPT,
    ES_C_MIRROR_ERROR, ES_C_REDIRECT_ATTEMPT, ES_C_MAX
};

/* Address bytes and ports retain network order. Other integers use host order. */
struct es_tuple {
    __u8 src[16];
    __u8 dst[16];
    __be16 sport;
    __be16 dport;
    __u16 ethertype;
    __u16 vlan[2];
    __u8 ip_version;
    __u8 l4_proto;
    __u8 flags;
    __u8 vlan_count;
    __u16 reserved;
};

struct es_event {
    __u64 timestamp_mono_ns;
    __u64 sequence;
    __u32 ifindex;
    __u32 original_len;      /* skb length for TC; not promised wire length. */
    __u32 cpu;
    __u16 abi_version;
    __u16 record_size;
    __u16 captured_len;
    __u16 link_type;
    __u8 direction;
    __u8 hook;
    __u8 parse_status;
    __u8 flags;
    struct es_tuple tuple;
    __u8 prefix[ES_SNAPSHOT_MAX];
};

struct es_counters { __u64 v[ES_C_MAX]; };
struct es_flow_key {
    struct es_tuple tuple;
    __u32 ifindex;
    __u8 direction;
    __u8 reserved[3];
};
struct es_flow_value {
    __u64 first_ns;
    __u64 last_ns;
    __u64 packets;
    __u64 bytes;
};

#ifdef __cplusplus
#define ES_ASSERT(c, m) static_assert(c, m)
#else
#define ES_ASSERT(c, m) _Static_assert(c, m)
#endif
ES_ASSERT(sizeof(struct es_tuple) == 48, "tuple ABI changed");
ES_ASSERT(sizeof(struct es_event) == 216, "event ABI changed");
ES_ASSERT(__builtin_offsetof(struct es_event, tuple) == 40, "tuple offset changed");
ES_ASSERT(__builtin_offsetof(struct es_event, prefix) == 88, "prefix offset changed");
ES_ASSERT(sizeof(struct es_flow_key) == 56, "flow key ABI changed");
ES_ASSERT(sizeof(struct es_flow_value) == 32, "flow value ABI changed");
ES_ASSERT(sizeof(struct es_counters) == ES_C_MAX * 8, "counter ABI changed");
#undef ES_ASSERT
#endif
```

## Field semantics

| Field | Contract |
|---|---|
| `timestamp_mono_ns` | Kernel monotonic nanoseconds, not epoch time. Keep the original value when exporting. |
| `sequence` | Observed packet sequence for this object, CPU, and direction; resets on reload. It is not globally ordered. |
| `ifindex` | Index in the object's network namespace; userspace adds namespace and session identity. |
| `original_len` | Length observed at this hook. TC offloads can prevent one-to-one correspondence with wire frames. |
| `captured_len` | Actual valid bytes in `prefix`; zero for metadata-only records. |
| `link_type` | Initially Ethernet only. A monitor-mode radiotap frame is not Ethernet. |
| `tuple.src/dst` | First 4 bytes for IPv4, all 16 for IPv6; remaining IPv4 bytes are zero. |
| `tuple.sport/dport` | Network byte order; only meaningful with `ES_T_PORTS_VALID`. Port zero is a possible value, not a validity marker. |
| `tuple.vlan` | Outer-to-inner VLAN IDs in host byte order, including stripped metadata when available. |
| `parse_status` | A parser result, never a traffic verdict. |

The timestamp and hook semantics come from the kernel helper and offload contracts [helpers], [offloads]. All other field choices above are this project's proposed interface.

## Map contract

| Map | Type | Scope and ownership | Initial capacity |
|---|---|---|---|
| `counters` | Per-CPU array | One map per interface object; key is direction | 2 |
| `events` | Ring buffer | Shared by CPUs of that object; one userspace reader | 8 MiB per interface |
| `flows` | LRU per-CPU hash | Directional flow segments, not durable totals | 4096 when enabled; 1 when disabled |
| `active_rules` | Array of maps, later | One immutable rule generation per outer slot | 1 outer slot |
| `tx_port` | Device map, later | Explicit XDP destination | 1 |
| `xsks_map` | XSK map, later | Queue ID to socket on that interface | Sized for configured queue IDs |

Choose **one loaded object per interface** for the first release. This avoids a shared global ownership problem. Direction lives in the key; namespace identity and load/session generation are attached in userspace. Never aggregate two reloaded objects just because their interface name is the same.

Per-CPU map memory grows with possible CPUs. For four interfaces, 4096 flow entries, 32-byte values, and 64 possible CPUs, values alone cost `4 × 4096 × 32 × 64 = 33,554,432` bytes; keys and map overhead are additional. Four 8-MiB rings add another 32 MiB. Reject a configuration exceeding an operator-specified memory budget rather than silently reducing it.

An LRU map provides bounded state through eviction. It is not an exact packet archive or a lossless flow-expiration protocol [hash]. Use interface counters for cumulative totals, and label exported flow rows as observations of a resident flow segment.

## ABI checks

### `tests/ebpf/abi_c.c` — complete file

```c
#include "elephantshrew_shared.h"
int main(void) { return ES_ABI_VERSION == 2u ? 0 : 1; }
```

### `tests/ebpf/abi_cpp.cpp` — complete file

```cpp
#include "elephantshrew_shared.h"
#include <type_traits>
static_assert(std::is_trivially_copyable_v<es_event>);
static_assert(std::is_standard_layout_v<es_event>);
int main() { return 0; }
```
```bash
cc -std=c11 -Wall -Wextra -Werror -Ibpf tests/ebpf/abi_c.c -o /tmp/es-abi-c
c++ -std=c++20 -Wall -Wextra -Werror -Ibpf tests/ebpf/abi_cpp.cpp -o /tmp/es-abi-cpp
/tmp/es-abi-c && /tmp/es-abi-cpp
```

Add an ABI-change test to code review: changing any field type, offset, interpretation, or record size requires a version decision and coordinated producer/consumer changes. Do not persist the raw C struct as the public on-disk format; encode named fields with a separate schema version.

## Acceptance gate

Both language builds must pass. The consumer in chapter 07 must reject version 1, an incorrect `record_size`, an unknown direction/hook, and `captured_len > 128`. A packet with no valid ports must not match a port-specific policy merely because zeroed fields happen to match.

## References

[helpers]: https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
[offloads]: https://docs.kernel.org/networking/segmentation-offloads.html
[hash]: https://docs.kernel.org/bpf/map_hash.html
[packet-store]: https://github.com/0xbadcaffe/ElephantShrew/blob/57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f/int/IPacketStore.hpp

Source baseline: [existing packet record type][packet-store]. The new ABI above is an implementation proposal, not a change to that existing type.
---

Start of series · [02 — Bounded packet parser](02_bounded_packet_parser.md)
