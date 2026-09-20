#ifndef ELEPHANTSHREW_POLICY_SHARED_H
#define ELEPHANTSHREW_POLICY_SHARED_H
#include "elephantshrew_shared.h"

enum es_policy_action {
    ES_POLICY_NEXT = 0,
    ES_POLICY_DROP = 1,
    ES_POLICY_MIRROR = 2,
    ES_POLICY_REDIRECT = 3
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
#endif /* ELEPHANTSHREW_POLICY_SHARED_H */