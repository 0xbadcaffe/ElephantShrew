#ifndef ELEPHANTSHREW_EBPF_OBJECT_HPP
#define ELEPHANTSHREW_EBPF_OBJECT_HPP
#include <cstdint>
#include <memory>

struct bpf_program;

namespace ElephantShrew {

enum class TcApi { 
    Legacy,
    Tcx
};

struct ObjectOptions {
    std::uint32_t sample_every{128};
    std::uint32_t snapshot_bytes{0};
    std::uint32_t ring_buffer_bytes{8u * 1024u * 1024u};
    std::uint32_t max_flows{4096};
    std::uint8_t select_l4_proto{0};
    bool classify{true}, flow_enabled{false};
    bool ingress{true}, egress{true};
};

struct LoadedObserver {
    std::shared_ptr<void> owner;
    bpf_program* ingress{};
    bpf_program* egress{};
    int events_fd{-1}, counters_fd{-1}, flows_fd{-1};
};

LoadedObserver LoadObserver(const ObjectOptions& options, TcApi api);
} // namespace ElephantShrew
#endif
