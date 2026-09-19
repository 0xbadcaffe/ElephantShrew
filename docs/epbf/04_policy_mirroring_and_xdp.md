# 04 — Policy, mirroring, and XDP datapaths

ElephantShrew eBPF implementation series · optional active kernel datapaths

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on 01–03; implement this **after** the passive milestone passes. Add exact-tuple rules, optional mirroring, and explicit interface-pair redirection. Keep the passive observer intact as its own TC object. The active object below is a second, separately owned ingress attachment with independent policy counters; do not sum its counters as additional network packets.

This chapter is not a claim that ElephantShrew is a complete firewall, router, or learning bridge. It supplies a deliberately bounded first active datapath plus its required control-plane contracts.

## File changes

```text
bpf/
├── elephantshrew_policy_shared.h      [ADD]
├── elephantshrew_active_tc.bpf.c      [ADD: optional]
└── elephantshrew_xdp.bpf.c            [ADD: optional]
inc/
├── EbpfPolicyManager.hpp              [ADD when enabling live policy]
└── XdpAttachment.hpp                 [ADD when enabling XDP]
src/
├── EbpfPolicyManager.cpp              [ADD]
└── XdpAttachment.cpp                 [ADD]
configs/
├── ebpf-policy.json                   [ADD after validation supports it]
└── ebpf-forward.json                  [ADD after forwarding tests]
```

## Policy keys and actions

The first rule language is **exact tuple or default**, not arbitrary CIDR/port wildcard policy. Exact matches include interface, direction, VLAN metadata, addresses, IP version, protocol, and valid ports. The default applies to ARP, fragments, unsupported headers, and nonmatching IP traffic. Configuration must state that default explicitly.

`NEXT` means continue to other host policy, not unconditional host acceptance. `MIRROR` copies toward a dedicated monitor port and preserves the original path. `REDIRECT` consumes the original path. Only ingress attachment is supported by this active starter.

### `bpf/elephantshrew_policy_shared.h` — complete file

```c
#ifndef ELEPHANTSHREW_POLICY_SHARED_H
#define ELEPHANTSHREW_POLICY_SHARED_H
#include "elephantshrew_shared.h"
enum es_policy_action {
    ES_POLICY_NEXT = 0, ES_POLICY_DROP = 1,
    ES_POLICY_MIRROR = 2, ES_POLICY_REDIRECT = 3
};
struct es_policy_key {
    struct es_flow_key flow;
    __u32 kind;             /* 0: all-zero default key; 1: exact tuple. */
    __u32 reserved;
};
struct es_rule {
    __u32 action;
    __u32 out_ifindex;
    __u64 generation;
};
#endif
```

### `bpf/elephantshrew_active_tc.bpf.c` — complete file

```c
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
```

## Publish one complete policy generation

Use a fresh inner hash for each update. Populate the default and all exact keys, validate values, then atomically replace outer key zero with the new inner-map FD. Every lookup for one packet uses the inner-map pointer captured at entry. Do not rewrite a live rule table row by row and describe that as an atomic policy update [maps].

Integration fragment for `src/EbpfPolicyManager.cpp`, after constructing a correctly sized inner map and validating every rule:

```cpp
// All syscall failures below must leave the previous outer entry unchanged.
// new_inner_fd is owned by a local RAII FD wrapper until publication succeeds.
es_policy_key default_key{};
es_rule default_rule{};
default_rule.action = ES_POLICY_NEXT; // Explicit operator choice, not implied.
default_rule.generation = generation;
if (bpf_map_update_elem(new_inner_fd, &default_key, &default_rule, BPF_NOEXIST) < 0)
    throw std::system_error(errno, std::generic_category(), "insert default policy");
for (const auto& [key, value] : validated_exact_rules) {
    if (bpf_map_update_elem(new_inner_fd, &key, &value, BPF_NOEXIST) < 0)
        throw std::system_error(errno, std::generic_category(), "insert exact policy");
}
// Freeze is defense-in-depth against later userspace mutation of this generation.
if (bpf_map_freeze(new_inner_fd) < 0)
    throw std::system_error(errno, std::generic_category(), "freeze rule generation");
std::uint32_t slot = 0;
if (bpf_map_update_elem(outer_map_fd, &slot, &new_inner_fd, BPF_ANY) < 0)
    throw std::system_error(errno, std::generic_category(), "activate rule generation");
// The outer map now holds a reference. Closing our inner FD is safe.
```

Construct the new map with `bpf_map_create(BPF_MAP_TYPE_HASH, ...)`, key size `sizeof(es_policy_key)`, value size `sizeof(es_rule)`, capacity 1024, and flags compatible with the inner template. Use a noncopyable FD owner from creation onward. Reserve one entry for the default, reject duplicate exact keys, and close the new FD on every failure. Retain a reference to the previous generation when operator rollback is required.

A single outer update is atomic **for that interface object**. Updating two interfaces is not a global transaction. Report per-interface generation, stage all new maps first, and specify rolling activation or an explicit traffic-quiescing procedure. Never claim a simultaneous whole-host switch.

The empty-map fallback in the kernel starter is explicitly fail-open. Startup must populate a default before attachment. Deployments requiring fail-closed missing-map behavior need a different reviewed program and out-of-band recovery plan. `NEXT` cannot physically connect two separate ports after their only forwarder is detached.

## Layer-2 forwarding

A default `REDIRECT` rule sends traffic toward the peer's egress. Create the reverse policy on the other ingress for bidirectional forwarding. Do not put either device into an independent Linux bridge during the forwarding correctness test; that bridge could hide a broken forwarder.

Keep all normal NIC receive-mode setup explicit. A transparent inline bridge may need promiscuous receive mode so frames addressed to downstream MACs reach the host at all. TC attachment alone does not configure that. Track and restore only the receive-mode reference/change owned by the service; reject unsupported interface kinds, monitor-mode Wi-Fi, conflicting kernel bridging, and unsupported station-mode MAC forwarding.

Before enabling an active path, validate both targets, distinct indices, the network namespace, MTUs, link types, direction, loop-free topology, and that `PcapReceiver::sendPacket()` is disabled for the same traffic. Define how reserved bridge-control multicast, ARP, IPv6 neighbor discovery, and locally addressed traffic are treated. The starter has no STP, MAC learning, TTL decrement, neighbor lookup, NAT, or checksum rewriting.

Mirror success and redirect return values are not end-to-end delivery acknowledgements. Track attempts, synchronous clone errors, destination counters, and redirect failures independently [helpers], [redirect]. In particular, mirroring onto another observed ingress/egress path can create extra records or loops; use a dedicated sink path and do not attach a reciprocal mirror rule to it.

## Optional XDP object

This separate starter counts ingress frames and optionally redirects to one device-map entry. It does not implement the TC parser, policy language, sampled event stream, or flow map. Capability reporting must expose that reduced feature set; selecting XDP must reject those unsupported requested features rather than pretend equivalence.

### `bpf/elephantshrew_xdp.bpf.c` — complete file

```c
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
```

Populate `tx_port[0]` with the validated destination ifindex before attaching with forwarding enabled. A missing destination selects `XDP_DROP` deliberately. Support native and generic attachment as distinct operator choices, report the effective mode, and do not replace an existing XDP owner. A libxdp-managed dispatcher is a separate coexistence implementation, not a boolean that makes overwrites safe.

Use `bpf_xdp_attach()` with a selected mode and update-if-no-existing behavior for an explicitly managed netlink attachment. Hold the program FD until detach; use the expected old program FD when detaching so a new owner's program is not removed. Native link-based attachment is another path, but do not assume its API selects generic mode. Probe the exact RX/TX pair and test deferred redirect errors [redirect], [api].

AF_XDP's queue-redirection object and userspace buffer lifecycle are in chapter 09. Do not combine an XSK redirect and a device redirect for the same frame unless a separately designed packet-disposition mechanism exists.

## Acceptance gate

Exact drop blocks only the intended tuple; default policy is tested with fragments, ARP, IPv6 neighbor discovery, and malformed headers. Mirroring produces one intended copy without changing original delivery. Both forwarding directions work without an alternate bridge. Rule publication failures preserve the previous generation. Existing unrelated TC/XDP ownership remains intact. Destination removal is reported as a failure, not a successful redirect count.

## References

[maps]: https://docs.kernel.org/bpf/map_of_maps.html
[helpers]: https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
[redirect]: https://docs.kernel.org/bpf/redirect.html
[api]: https://github.com/libbpf/libbpf/blob/master/src/libbpf.h

Primary contracts: [map-in-map replacement][maps], [clone/redirect helpers][helpers], [XDP redirect behavior][redirect], and [attachment APIs][api].
---

[03 — TC observer, sampling, and flow accounting](03_tc_observer_sampling_and_flows.md) · [05 — Meson and the BPF build pipeline](05_meson_bpf_build_pipeline.md)
