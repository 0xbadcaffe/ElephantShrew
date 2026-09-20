#ifndef ELEPHANTSHREW_EBPF_TELEMETRY_HPP
#define ELEPHANTSHREW_EBPF_TELEMETRY_HPP
#include "TelemetryTypes.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace ElephantShrew {
class EbpfTelemetry {
    struct Impl;
    std::unique_ptr<Impl> p_;
public:
    explicit EbpfTelemetry(std::size_t queue_capacity);
    ~EbpfTelemetry();
    EbpfTelemetry(const EbpfTelemetry&) = delete;
    EbpfTelemetry& operator=(const EbpfTelemetry&) = delete;
    void AddSource(int ring_fd, std::uint32_t ifindex,
                   std::uint64_t netns_inode, std::uint64_t session_id);
    void Start();
    // Producers must be detached first. Keep sources and map owners alive.
    int FinishAfterDetach(std::chrono::milliseconds drain_timeout) noexcept;
    bool TryPop(TelemetryRecord& record) noexcept;
    TelemetryLoss Loss() const noexcept;
};
}
#endif
