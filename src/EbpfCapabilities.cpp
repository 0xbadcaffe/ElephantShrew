#include "EbpfCapabilities.hpp"
#include <bpf/libbpf.h>
namespace ElephantShrew {
std::vector<CapabilityResult> ProbeEbpfCapabilities() {
    return {
        {"sched_cls", libbpf_probe_bpf_prog_type(BPF_PROG_TYPE_SCHED_CLS, nullptr)},
        {"ringbuf", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_RINGBUF, nullptr)},
        {"percpu_array", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_PERCPU_ARRAY, nullptr)},
        {"lru_percpu_hash", libbpf_probe_bpf_map_type(BPF_MAP_TYPE_LRU_PERCPU_HASH, nullptr)},
        {"tc_ringbuf_reserve", libbpf_probe_bpf_helper(BPF_PROG_TYPE_SCHED_CLS,
                                           BPF_FUNC_ringbuf_reserve, nullptr)},
        {"tc_skb_load_bytes", libbpf_probe_bpf_helper(BPF_PROG_TYPE_SCHED_CLS,
                                           BPF_FUNC_skb_load_bytes, nullptr)}
    };
}
}
