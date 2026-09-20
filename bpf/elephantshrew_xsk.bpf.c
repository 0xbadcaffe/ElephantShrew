/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} xsks SEC(".maps");
SEC("xdp")
int es_xsk(struct xdp_md *ctx) {
    __u32 queue = ctx->rx_queue_index;
    /* Unregistered queues continue to the normal stack. Registered queues
     * are redirected, not cloned: the AF_XDP consumer owns their disposition. */
    return bpf_redirect_map(&xsks, queue, XDP_PASS);
}
char LICENSE[] SEC("license") = "Dual BSD/GPL";
