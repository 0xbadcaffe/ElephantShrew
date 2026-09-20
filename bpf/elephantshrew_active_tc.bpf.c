/* SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0-only) */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "elephantshrew_policy_shared.h"
#include "packet_parser.bpf.h"

struct es_rules_map {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, struct es_policy_key);
    __type(value, struct es_rule);
};
struct es_rules_map rule_template SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
    __uint(max_entries, 1);
    __type(key, __u32);
    __array(values, struct es_rules_map);
} active_rules SEC(".maps") = { .values = { [0] = &rule_template } };

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct es_counters);
} policy_counters SEC(".maps");

#ifdef ES_TCX
SEC("tcx/ingress")
#else
SEC("tc")
#endif
int es_active_ingress(struct __sk_buff *skb)
{
    __u32 zero = 0, cap = skb->len;
    void *rules = bpf_map_lookup_elem(&active_rules, &zero);
    struct es_counters *s = bpf_map_lookup_elem(&policy_counters, &zero);
    struct es_policy_key key = {};
    struct es_rule *r, selected;
    __u8 bytes[ES_PARSE_BYTES] = {};
    int status = ES_PARSE_TRUNCATED;
    if (!rules || !s) return ES_TC_NEXT; /* Explicit initial fail-open policy. */
    if (cap > ES_PARSE_BYTES) cap = ES_PARSE_BYTES;
    if (cap && bpf_skb_load_bytes(skb, 0, bytes, cap) == 0)
        status = es_parse(bytes, cap, skb->len, skb->vlan_present != 0,
                          (__u16)skb->vlan_tci, &key.flow.tuple);
    r = 0;
    if (status == ES_PARSE_OK && (key.flow.tuple.flags & ES_T_PORTS_VALID)) {
        key.kind = 1;
        key.flow.ifindex = skb->ifindex;
        key.flow.direction = ES_INGRESS;
        r = bpf_map_lookup_elem(rules, &key);
    }
    if (!r) {
        __builtin_memset(&key, 0, sizeof(key));
        r = bpf_map_lookup_elem(rules, &key);
    }
    if (!r) return ES_TC_NEXT;
    selected = *r; /* Do not consult a second generation for this packet. */
    if (selected.action == ES_POLICY_NEXT) return ES_TC_NEXT;
    if (selected.action == ES_POLICY_DROP) {
        __sync_fetch_and_add(&s->v[ES_C_POLICY_DROP], 1);
        return ES_TC_DROP;
    }
    if (!selected.out_ifindex || selected.out_ifindex == skb->ifindex) {
        /* Invalid active configuration must have been rejected before attach. */
        __sync_fetch_and_add(&s->v[ES_C_POLICY_DROP], 1);
        return ES_TC_DROP;
    }
    if (selected.action == ES_POLICY_MIRROR) {
        __sync_fetch_and_add(&s->v[ES_C_MIRROR_ATTEMPT], 1);
        if (bpf_clone_redirect(skb, selected.out_ifindex, 0) < 0)
            __sync_fetch_and_add(&s->v[ES_C_MIRROR_ERROR], 1);
        return ES_TC_NEXT;
    }
    if (selected.action == ES_POLICY_REDIRECT) {
        __sync_fetch_and_add(&s->v[ES_C_REDIRECT_ATTEMPT], 1);
        return bpf_redirect(selected.out_ifindex, 0); /* Target egress. */
    }
    return ES_TC_DROP; /* Unknown action is never silently accepted. */
}
char LICENSE[] SEC("license") = "Dual BSD/GPL";