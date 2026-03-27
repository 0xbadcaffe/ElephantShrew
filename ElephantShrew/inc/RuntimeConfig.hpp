#ifndef _RUNTIMECONFIG_H_
#define _RUNTIMECONFIG_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ElephantShrew {

struct CaptureOptions {
    std::vector<std::string> ifaces;
    bool record_packets{false};
    bool debug_packets{false};
};

struct RedisStoreOptions {
    std::string host{"127.0.0.1"};
    std::string port{"6379"};
    std::string stream_key{"elephantshrew:packets"};
    std::size_t max_pending_writes{1024};
    std::uint64_t connect_timeout_ms{5000};
    std::uint64_t drain_timeout_ms{5000};
};

struct SupervisorOptions {
    std::uint64_t restart_delay_ms{2000};
    std::uint64_t poll_interval_ms{200};
};

struct UiOptions {
    bool show_startup_art{true};
};

struct RuntimeConfig {
    CaptureOptions capture;
    RedisStoreOptions redis;
    SupervisorOptions supervisor;
    UiOptions ui;
};

RuntimeConfig LoadRuntimeConfigFromJson(const std::string& path);

} // namespace ElephantShrew

#endif // _RUNTIMECONFIG_H_
