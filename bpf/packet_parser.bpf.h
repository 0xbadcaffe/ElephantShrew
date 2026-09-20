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