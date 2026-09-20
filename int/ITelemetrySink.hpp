#ifndef ELEPHANTSHREW_I_TELEMETRY_SINK_HPP
#define ELEPHANTSHREW_I_TELEMETRY_SINK_HPP
#include "TelemetryTypes.hpp"
#include <chrono>
namespace ElephantShrew {
class ITelemetrySink {
public:
    virtual ~ITelemetrySink() = default;
    // Must copy/enqueue owned data and return promptly. true != persisted.
    virtual bool TryPublish(const TelemetryRecord& record) noexcept = 0;
    // Implementation must cancel its I/O cooperatively before this deadline.
    // false reports records that could not be drained/persisted.
    virtual bool Stop(std::chrono::steady_clock::time_point deadline) noexcept = 0;
};
}
#endif
