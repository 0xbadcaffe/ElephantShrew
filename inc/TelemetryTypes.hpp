#ifndef ELEPHANTSHREW_TELEMETRY_TYPES_HPP
#define ELEPHANTSHREW_TELEMETRY_TYPES_HPP
#include "elephantshrew_shared.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace ElephantShrew {
struct TelemetryRecord {
    es_event event{};
    std::uint64_t netns_inode{};
    std::uint64_t session_id{};
};
inline bool DecodeEvent(const void* data, std::size_t size, es_event& out) noexcept {
    if (!data || size != sizeof(es_event)) return false;
    es_event e{};
    std::memcpy(&e, data, sizeof(e)); // No alignment assumptions about C callback data.
    if (e.abi_version != ES_ABI_VERSION || e.record_size != sizeof(e) ||
        e.ifindex == 0 || e.direction >= ES_DIRECTIONS ||
        (e.hook != ES_HOOK_TC && e.hook != ES_HOOK_XDP) ||
        e.link_type != ES_LINKTYPE_ETHERNET ||
        e.parse_status > ES_PARSE_SKIPPED ||
        e.captured_len > ES_SNAPSHOT_MAX || e.captured_len > e.original_len ||
        e.tuple.vlan_count > 2 || e.tuple.reserved != 0 ||
        (e.flags & ~(ES_E_PREFIX_TRUNCATED | ES_E_COPY_FAILED | ES_E_VLAN_STRIPPED)) ||
        (e.tuple.flags & ~(ES_T_L2_VALID | ES_T_ADDR_VALID | ES_T_PORTS_VALID | ES_T_FRAGMENT)))
        return false;
    if ((e.flags & ES_E_COPY_FAILED) && e.captured_len != 0) return false;
    if ((e.tuple.flags & ES_T_PORTS_VALID) &&
        (!(e.tuple.flags & ES_T_ADDR_VALID) ||
         (e.tuple.flags & ES_T_FRAGMENT) ||
         (e.tuple.l4_proto != 6 && e.tuple.l4_proto != 17))) return false;
    out = e;
    return true;
}
struct TelemetryLoss {
    std::uint64_t decode_errors{}, queue_dropped{}, shutdown_discarded{};
    int poll_error{};
};
}
#endif
