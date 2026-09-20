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