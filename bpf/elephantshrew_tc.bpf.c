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