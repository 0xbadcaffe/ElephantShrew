# 03 — TC observer, sampling, and flow accounting

ElephantShrew eBPF implementation series · passive kernel datapath

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on 01 and 02. Produce a TC program with per-direction counters, deterministic per-CPU sampling, optional 128-byte prefixes, and optional directional flow segments. The only verdict is `ES_TC_NEXT`. Even a full ring, failed copy, malformed header, or full flow map must not become a network drop.

## File changes

```text
bpf/
├── elephantshrew_shared.h             [USE]
├── packet_parser.bpf.h                [USE]
└── elephantshrew_tc.bpf.c              [ADD]
```

Chapter 05 compiles this same source twice: ordinary `tc` sections for legacy attachment, and `tcx/ingress` / `tcx/egress` sections with `ES_TCX` defined. Only the selected object is loaded. Do not load unsupported TCX programs and hope that a legacy attachment choice later fixes the failure.

### `bpf/elephantshrew_tc.bpf.c` — complete file

```c
/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "elephantshrew_shared.h"
#include "packet_parser.bpf.h"

/* Set before load. Changing these settings requires an orderly reload. */
const volatile __u32 sample_every = 128;
const volatile __u32 snapshot_bytes = 0;
const volatile __u8 classify = 1;
const volatile __u8 flow_enabled = 0;
const volatile __u8 select_l4_proto = 0; /* 0 selects all; not a network filter. */

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, ES_DIRECTIONS);
    __type(key, __u32);
    __type(value, struct es_counters);
} counters SEC(".maps");
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 8 * 1024 * 1024);
} events SEC(".maps");
struct {
    __uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, struct es_flow_key);
    __type(value, struct es_flow_value);
} flows SEC(".maps");

static __always_inline void es_count_flow(struct __sk_buff *skb, __u32 direction,
        const struct es_tuple *tuple, struct es_counters *s, __u64 now)
{
    struct es_flow_key key = {};
    struct es_flow_value zero = {};
    struct es_flow_value *f;
    /* First implementation tracks nonfragmented TCP/UDP tuples only. */
    if (!(tuple->flags & ES_T_PORTS_VALID)) return;
    key.tuple = *tuple;
    key.ifindex = skb->ifindex;
    key.direction = (__u8)direction;
    f = bpf_map_lookup_elem(&flows, &key);
    if (!f) {
        bpf_map_update_elem(&flows, &key, &zero, BPF_NOEXIST);
        f = bpf_map_lookup_elem(&flows, &key); /* Also covers a concurrent insert. */
    }
    if (!f) {
        __sync_fetch_and_add(&s->v[ES_C_FLOW_UPDATE_FAILED], 1);
        return;
    }
    if (f->packets == 0) f->first_ns = now;
    f->last_ns = now;
    f->packets++;
    f->bytes += skb->len;
}

static __always_inline int es_observe(struct __sk_buff *skb, __u32 direction)
{
    __u32 key = direction, cap = skb->len, n = sample_every;
    struct es_counters *s = bpf_map_lookup_elem(&counters, &key);
    struct es_tuple tuple = {};
    struct es_event *e;
    __u8 bytes[ES_PARSE_BYTES] = {};
    __u64 seq, selected, now;
    int status = ES_PARSE_SKIPPED;
    if (!s) return ES_TC_NEXT;
    seq = __sync_fetch_and_add(&s->v[ES_C_PACKETS], 1) + 1;
    __sync_fetch_and_add(&s->v[ES_C_BYTES], skb->len);

    if (classify || flow_enabled || select_l4_proto != 0) {
        if (cap > ES_PARSE_BYTES) cap = ES_PARSE_BYTES;
        status = ES_PARSE_TRUNCATED;
        if (cap && bpf_skb_load_bytes(skb, 0, bytes, cap) == 0)
            status = es_parse(bytes, cap, skb->len, skb->vlan_present != 0,
                              (__u16)skb->vlan_tci, &tuple);
        if (status == ES_PARSE_MALFORMED)
            __sync_fetch_and_add(&s->v[ES_C_PARSE_MALFORMED], 1);
        else if (status != ES_PARSE_OK)
            __sync_fetch_and_add(&s->v[ES_C_PARSE_PARTIAL], 1);
    }
    now = bpf_ktime_get_ns();
    if (flow_enabled && status == ES_PARSE_OK)
        es_count_flow(skb, direction, &tuple, s, now);

    /* This selection controls telemetry only. No packet is dropped here. */
    if (select_l4_proto &&
        (status != ES_PARSE_OK || tuple.l4_proto != select_l4_proto))
        return ES_TC_NEXT;
    selected = __sync_fetch_and_add(&s->v[ES_C_SELECTED], 1) + 1;
    if (!n || selected % n != 0) return ES_TC_NEXT;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        __sync_fetch_and_add(&s->v[ES_C_RING_LOST], 1);
        return ES_TC_NEXT;
    }
    __builtin_memset(e, 0, sizeof(*e));
    e->timestamp_mono_ns = now;
    e->sequence = seq;
    e->ifindex = skb->ifindex;
    e->original_len = skb->len;
    e->cpu = bpf_get_smp_processor_id();
    e->abi_version = ES_ABI_VERSION;
    e->record_size = sizeof(*e);
    e->link_type = ES_LINKTYPE_ETHERNET;
    e->direction = (__u8)direction;
    e->hook = ES_HOOK_TC;
    e->parse_status = (__u8)status;
    e->tuple = tuple;
    if (skb->vlan_present) e->flags |= ES_E_VLAN_STRIPPED;

    __u32 want = snapshot_bytes;
    if (want > ES_SNAPSHOT_MAX) want = ES_SNAPSHOT_MAX;
    if (want > skb->len) want = skb->len;
    if (want) {
        if (bpf_skb_load_bytes(skb, 0, e->prefix, want) == 0) {
            e->captured_len = (__u16)want;
            if (want < skb->len) e->flags |= ES_E_PREFIX_TRUNCATED;
        } else {
            /* A failed copy must not publish partially initialized data. */
            __builtin_memset(e->prefix, 0, sizeof(e->prefix));
            e->flags |= ES_E_COPY_FAILED;
            __sync_fetch_and_add(&s->v[ES_C_SNAPSHOT_FAILED], 1);
        }
    }
    bpf_ringbuf_submit(e, 0);
    __sync_fetch_and_add(&s->v[ES_C_EVENTS], 1);
    return ES_TC_NEXT;
}

#ifdef ES_TCX
#define ES_INGRESS_SECTION "tcx/ingress"
#define ES_EGRESS_SECTION "tcx/egress"
#else
#define ES_INGRESS_SECTION "tc"
#define ES_EGRESS_SECTION "tc"
#endif
SEC(ES_INGRESS_SECTION)
int es_ingress(struct __sk_buff *skb) { return es_observe(skb, ES_INGRESS); }
SEC(ES_EGRESS_SECTION)
int es_egress(struct __sk_buff *skb) { return es_observe(skb, ES_EGRESS); }
char LICENSE[] SEC("license") = "Dual BSD/GPL";
```

## Sampling and accounting

The counter sequence advances for every observed skb. The selection counter advances only for packets eligible for telemetry. `sample_every=N` attempts an event on every Nth eligible packet **per CPU and direction**, not globally. `0` disables events, `1` attempts an event for every eligible packet. A short test at `128` can legitimately produce no events.

The counters are independent of storage. `RING_LOST` counts failed reservations; `EVENTS` counts submissions, not persisted rows. Userspace adds decode, queue, and sink-drop counters. Do not subtract ring losses from interface packets and call the result network delivery.

The fixed-size event reserves 216 bytes even with `snapshot_bytes=0`. This is a deliberate first implementation trade-off. Later, add separate versioned metadata and prefix record kinds only after measuring ring pressure; do not vary record length without changing decoder validation.

`ES_TC_NEXT` maps to legacy continuation and TCX continuation. A passive program must not unconditionally return an action that prevents later filters from running. Confirm coexistence against an existing policy in chapter 10 [sections], [uapi]. Ring reservation is nonblocking; it never waits for the user process [ring].

## Flow scope and concurrency

The flow map accounts only successfully parsed, nonfragmented TCP/UDP tuples. Each CPU updates its own value for the key. First/last timestamps describe that CPU's resident segment; userspace takes the minimum nonzero first time and maximum last time when aggregating.

An insertion race is handled by looking up again after `BPF_NOEXIST`; the code does not overwrite an existing flow with zero. LRU eviction can discard a segment before export. Userspace must not claim exact flow totals, add repeated cumulative snapshots as deltas, or equate LRU residency with connection lifetime [hash].

The per-CPU flow-update routine assumes the supported TC execution paths do not concurrently mutate the same per-CPU flow value reentrantly. Validate that assumption for any new attachment context. Interface counters use atomic additions; timestamps and flow tuples are observational snapshots, not multi-field atomic transactions. A shared cross-CPU hash alternative requires explicit atomics/locking and a separately reviewed value layout.

## Minimal milestone versus extensions

For the earliest attachment test, set `classify=0`, `flow_enabled=0`, `select_l4_proto=0`, and `snapshot_bytes=0` before load. After lifecycle tests pass, enable classification, then prefixes, then flows. Resize `flows` to one entry when disabled so a dormant feature does not consume a large per-CPU allocation.

Protocol totals can be added as a separate per-CPU array indexed by a bounded bucket enum. Update it before the sampling decision; otherwise the UI would accidentally label sampled-event counts as all-packet protocol counts. Add the bucket labels and aggregation in chapter 07 in the same commit.

## Verification commands

After chapter 05 has built the object, inspect its structure without attaching it:

```bash
llvm-objdump -h build/ebpf/es_tc_legacy.bpf.o
llvm-objdump -d build/ebpf/es_tc_legacy.bpf.o > /tmp/es-tc.disassembly.txt
bpftool btf dump file build/ebpf/es_tc_legacy.bpf.o format raw
```

Use the loader test in chapter 10 to execute the verifier without touching a physical interface. Inspect BPF stack usage and instruction growth after parser changes. These examples require a BPF-target Clang and target-kernel tests; no line-rate claim follows from merely compiling the source.

## Acceptance gate

Counters-only produces no ring events. Sampling at one produces decodable v2 events. Prefix lengths never exceed either 128 or the observed skb length. Protocol selection changes only event output, not connectivity or the independent pcap recorder. Ring exhaustion and flow eviction remain visible and cannot block the networking path.

## References

[sections]: https://docs.kernel.org/bpf/libbpf/program_types.html
[uapi]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/bpf.h
[ring]: https://docs.kernel.org/bpf/ringbuf.html
[hash]: https://docs.kernel.org/bpf/map_hash.html

Primary contracts: [program sections][sections], [BPF context/action definitions][uapi], [ring-buffer behavior][ring], and [per-CPU/LRU maps][hash].
---

[02 — Bounded packet parser](02_bounded_packet_parser.md) · [04 — Policy, mirroring, and XDP datapaths](04_policy_mirroring_and_xdp.md)
