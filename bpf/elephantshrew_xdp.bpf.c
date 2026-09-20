/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "elephantshrew_shared.h"

const volatile __u8 forwarding_enabled = 0;

struct {
    __uint(type, BPF_MAP_TYPE_DEVMAP);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} tx_port SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct es_counters);
} counters SEC(".maps");
SEC("xdp")

int es_xdp(struct xdp_md *ctx)
{
    __u32 key = 0;
    struct es_counters *s = bpf_map_lookup_elem(&counters, &key);
    void *data = (void *)(long)ctx->data;
    void *end = (void *)(long)ctx->data_end;
    if (s) {
        __sync_fetch_and_add(&s->v[ES_C_PACKETS], 1);
        __sync_fetch_and_add(&s->v[ES_C_BYTES], (__u64)(end - data));
    }
    if (!forwarding_enabled) return XDP_PASS;
    if (data + 14 > end) return XDP_DROP;
    if (s) __sync_fetch_and_add(&s->v[ES_C_REDIRECT_ATTEMPT], 1);
    return bpf_redirect_map(&tx_port, 0, XDP_DROP);
}
char LICENSE[] SEC("license") = "Dual BSD/GPL";