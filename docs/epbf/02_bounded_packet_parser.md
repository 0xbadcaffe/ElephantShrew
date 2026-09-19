# 02 — Bounded packet parser

ElephantShrew eBPF implementation series · kernel parsing with a host-testable core

**Baseline:** `57b6fa0cf4ccda9315603a661ea50d1d2cbdf55f` on `master`, rechecked September 19, 2026.
**Purpose:** implement the preceding `README_EBPF.md` design; these are proposed changes, not features already present in the repository.
**Snippet convention:** “complete file” means the whole proposed file is supplied; “integration fragment” must be inserted at the named location; “pseudocode” describes orchestration, not compilable source. Chapter 10 records which examples were actually checked.

## Outcome and dependencies

Depend on chapter 01. Add a bounded Ethernet/IP classifier that can run both inside BPF and in ordinary unit tests. This implementation deliberately parses at most 128 bytes, two VLAN tags in total, and four IPv6 extension headers. It does not reassemble fragments, validate checksums, parse application payloads, or claim to be a firewall-grade protocol validator.

## File changes

```text
bpf/
├── elephantshrew_shared.h             [USE]
└── packet_parser.bpf.h                [ADD]
tests/ebpf/
├── parser_test.cpp                    [ADD in chapter 10]
└── parser_fuzz.cpp                    [ADD in chapter 10]
```

Separate **access safety**, **classification completeness**, and **traffic policy**. The parser returns a status; chapter 03 always continues traffic even for malformed input. Active code in chapter 04 must define its own unsupported-traffic policy.

### `bpf/packet_parser.bpf.h` — complete file

```c
#ifndef ELEPHANTSHREW_PACKET_PARSER_H
#define ELEPHANTSHREW_PACKET_PARSER_H
#include "elephantshrew_shared.h"
#if defined(__BPF__)
#define ES_INLINE static __attribute__((always_inline)) inline
#else
#define ES_INLINE static inline
#endif

ES_INLINE __u16 es_be16(const __u8 *p)
{
    return ((__u16)p[0] << 8) | p[1];
}

/* Bounds distinguish a missing prefix from a structurally impossible length. */
ES_INLINE int es_need(__u32 off, __u32 n, __u32 cap, __u32 end)
{
    if (off > end || n > end - off) return ES_PARSE_MALFORMED;
    if (off > cap || n > cap - off) return ES_PARSE_TRUNCATED;
    return ES_PARSE_OK;
}
ES_INLINE int es_vlan(__u16 type)
{
    return type == 0x8100 || type == 0x88a8;
}
ES_INLINE int es_ext(__u8 next)
{
    return next == 0 || next == 43 || next == 60 || next == 51 || next == 44;
}

/* Inputs: Ethernet prefix, valid prefix length, complete observed skb length.
 * stripped_vlan is a boolean, stripped_vid a host-order VLAN ID.
 * No packet pointer, dynamic allocation, or kernel helper is used here.
 */
ES_INLINE int es_parse(const __u8 *p, __u32 cap, __u32 original_len,
                      int stripped_vlan, __u16 stripped_vid,
                      struct es_tuple *t)
{
    __u32 off = 14, end = original_len, l4 = 0;
    __u16 type;
    int rc;
    __builtin_memset(t, 0, sizeof(*t));
    if (cap > ES_PARSE_BYTES) cap = ES_PARSE_BYTES;
    if (cap > original_len) cap = original_len;
    rc = es_need(0, 14, cap, end);
    if (rc) return rc;
    type = es_be16(p + 12);
    t->flags = ES_T_L2_VALID;
    if (stripped_vlan) t->vlan[t->vlan_count++] = stripped_vid & 0x0fff;

#ifdef __BPF__
#pragma unroll
#endif
    for (int i = 0; i < 2; ++i) {
        if (!es_vlan(type)) break;
        if (t->vlan_count >= 2) return ES_PARSE_UNSUPPORTED;
        rc = es_need(off, 4, cap, end);
        if (rc) return rc;
        t->vlan[t->vlan_count++] = es_be16(p + off) & 0x0fff;
        type = es_be16(p + off + 2);
        off += 4;
    }
    t->ethertype = type;
    if (es_vlan(type)) return ES_PARSE_UNSUPPORTED;

    if (type == 0x0806) { /* Ethernet/IPv4 ARP only. */
        rc = es_need(off, 28, cap, end);
        if (rc) return rc;
        if (es_be16(p + off) != 1 || es_be16(p + off + 2) != 0x0800 ||
            p[off + 4] != 6 || p[off + 5] != 4)
            return ES_PARSE_UNSUPPORTED;
        __builtin_memcpy(t->src, p + off + 14, 4);
        __builtin_memcpy(t->dst, p + off + 24, 4);
        t->ip_version = 4;
        t->flags |= ES_T_ADDR_VALID;
        return ES_PARSE_OK;
    }

    if (type == 0x0800) {
        __u32 ihl, total;
        __u16 frag;
        rc = es_need(off, 20, cap, end);
        if (rc) return rc;
        if (p[off] >> 4 != 4) return ES_PARSE_MALFORMED;
        ihl = (p[off] & 0x0f) * 4u;
        total = es_be16(p + off + 2);
        if (ihl < 20 || total < ihl || total > end - off)
            return ES_PARSE_MALFORMED;
        end = off + total; /* Ethernet padding must not become transport data. */
        rc = es_need(off, ihl, cap, end);
        if (rc) return rc;
        t->ip_version = 4;
        t->l4_proto = p[off + 9];
        __builtin_memcpy(t->src, p + off + 12, 4);
        __builtin_memcpy(t->dst, p + off + 16, 4);
        t->flags |= ES_T_ADDR_VALID;
        frag = es_be16(p + off + 6);
        if (frag & 0x3fff) { /* MF or any fragment offset; DF is not fragmentation. */
            t->flags |= ES_T_FRAGMENT;
            return ES_PARSE_FRAGMENT;
        }
        l4 = off + ihl;
    } else if (type == 0x86dd) {
        __u32 payload;
        __u8 next;
        rc = es_need(off, 40, cap, end);
        if (rc) return rc;
        if (p[off] >> 4 != 6) return ES_PARSE_MALFORMED;
        payload = es_be16(p + off + 4);
        if (payload == 0) return ES_PARSE_UNSUPPORTED; /* No jumbogram parser. */
        if (payload > end - off - 40) return ES_PARSE_MALFORMED;
        end = off + 40 + payload;
        next = p[off + 6];
        t->ip_version = 6;
        __builtin_memcpy(t->src, p + off + 8, 16);
        __builtin_memcpy(t->dst, p + off + 24, 16);
        t->flags |= ES_T_ADDR_VALID;
        off += 40;
#ifdef __BPF__
#pragma unroll
#endif
        for (int i = 0; i < 4; ++i) {
            __u32 ext_len;
            if (!es_ext(next)) break;
            rc = es_need(off, 2, cap, end);
            if (rc) return rc;
            if (next == 44) {
                rc = es_need(off, 8, cap, end);
                if (rc) return rc;
                t->l4_proto = p[off];
                t->flags |= ES_T_FRAGMENT;
                return ES_PARSE_FRAGMENT; /* Atomic fragments also excluded. */
            }
            ext_len = next == 51 ? ((__u32)p[off + 1] + 2) * 4
                                 : ((__u32)p[off + 1] + 1) * 8;
            if (ext_len < 8) return ES_PARSE_MALFORMED;
            rc = es_need(off, ext_len, cap, end);
            if (rc) return rc;
            next = p[off];
            off += ext_len;
        }
        t->l4_proto = next;
        if (es_ext(next) || next == 50) return ES_PARSE_UNSUPPORTED;
        l4 = off;
    } else {
        return ES_PARSE_UNSUPPORTED;
    }

    if (t->l4_proto == 6) { /* TCP base header plus declared header length. */
        __u32 hlen;
        rc = es_need(l4, 20, cap, end);
        if (rc) return rc;
        hlen = (p[l4 + 12] >> 4) * 4u;
        if (hlen < 20 || hlen > end - l4) return ES_PARSE_MALFORMED;
    } else if (t->l4_proto == 17) {
        __u32 ulen;
        rc = es_need(l4, 8, cap, end);
        if (rc) return rc;
        ulen = es_be16(p + l4 + 4);
        if (ulen < 8 || ulen > end - l4) return ES_PARSE_MALFORMED;
    } else {
        return ES_PARSE_OK; /* Known L3, no transport ports promised. */
    }
    __builtin_memcpy(&t->sport, p + l4, 2);
    __builtin_memcpy(&t->dport, p + l4 + 2, 2);
    t->flags |= ES_T_PORTS_VALID;
    return ES_PARSE_OK;
}
#undef ES_INLINE
#endif
```

## TC wrapper — integration fragment for chapter 03

At TC, `skb->len` is not the amount of directly accessible linear packet memory. Copy a bounded prefix using `bpf_skb_load_bytes()` rather than indexing through nonlinear data. This avoids modifying the packet and avoids invalidating packet pointers through a pull operation [helpers].

```c
__u8 bytes[ES_PARSE_BYTES] = {};
__u32 cap = skb->len;
if (cap > ES_PARSE_BYTES) cap = ES_PARSE_BYTES;
int status = ES_PARSE_TRUNCATED;
if (cap && bpf_skb_load_bytes(skb, 0, bytes, cap) == 0)
    status = es_parse(bytes, cap, skb->len, skb->vlan_present != 0,
                      (__u16)skb->vlan_tci, &tuple);
```

Require an Ethernet interface and the normal ingress/egress TC hook for this wrapper. Do not reinterpret raw-IP tunnels or wireless monitor frames as Ethernet. A stripped VLAN tag contributes to metadata but is absent from the copied prefix; set `ES_E_VLAN_STRIPPED` in the event.

The C parser's bounds checks are necessary but do not prove that the BPF verifier will accept the compiled program. Inspect the actual generated bytecode and verifier log on the target kernel; simplify compiler transformations if the verifier cannot establish a range [verifier]. Do not remove bounds checks just to make a verifier error disappear.

## Defined edge cases

| Input | Result |
|---|---|
| Ethernet frame shorter than its claimed mandatory header | `MALFORMED` |
| Header exists in the original packet but beyond the 128-byte prefix | `TRUNCATED` |
| Third VLAN, fifth extension header, ESP, or IPv6 jumbogram | `UNSUPPORTED` |
| IPv4 MF/offset or IPv6 fragment header | `FRAGMENT`, no port validity |
| IPv4 DF only | Continue normal parsing |
| TCP declared header shorter than 20 bytes | `MALFORMED` |
| ARP with non-Ethernet/non-IPv4 address sizes | `UNSUPPORTED` |
| ICMP/ICMPv6 | Addresses and protocol available; no TCP/UDP ports |
| Valid UDP with destination port 0 | `PORTS_VALID` is set; zero is not treated as missing |

The parser does not inspect TCP options beyond their declared size, enforce every IPv6 extension-order rule, or reconstruct offload-adjusted wire framing. `OK` means the fields this parser promises were decoded within its supported scope.

## How to extend it

Add a field only after updating the shared ABI and decoder. Add an encapsulation only after defining its maximum depth and whether the exported tuple is outer or inner. Keep an explicit tunnel identifier if an inner tuple might collide with another tenant's tuple. Never silently treat an inner flow as a host-native flow.

For a future XDP parser, reuse the parsing decisions but add a context-specific reader. Do not pass an `xdp_md` pointer to an skb helper. A direct XDP parser must check each access against `data_end`; copy mode is not a synonym for safe access.

## Acceptance gate

Run deterministic IPv4/IPv6/ARP/VLAN/fragment vectors and randomized prefix tests under AddressSanitizer and UndefinedBehaviorSanitizer. Check that bytes after `cap` cannot affect the returned tuple. Then load the complete TC object under the kernel verifier. Host tests alone are not verifier tests.

## References

[helpers]: https://man7.org/linux/man-pages/man7/bpf-helpers.7.html
[verifier]: https://docs.kernel.org/bpf/verifier.html

Primary contracts: [skb helper behavior][helpers] and [verifier bounds checking][verifier]. The parser implementation and limits above are project design choices.
---

[01 — Shared ABI and map contracts](01_shared_abi_and_map_contracts.md) · [03 — TC observer, sampling, and flow accounting](03_tc_observer_sampling_and_flows.md)
