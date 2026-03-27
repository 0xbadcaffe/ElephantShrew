#include "RuntimeConfig.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace ElephantShrew {
namespace {

using json = nlohmann::json;

const json* FindObject(const json& parent, const char* key)
{
    const auto it = parent.find(key);
    if (it == parent.end())
        return nullptr;

    if (!it->is_object())
        throw std::runtime_error("Invalid '" + std::string(key) + "': expected object");

    return &(*it);
}

template <typename T>
void ReadValue(const json& object, const std::string& prefix, const char* key, T& value)
{
    const auto it = object.find(key);
    if (it == object.end())
        return;

    try {
        value = it->get<T>();
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Invalid '" + prefix + "." + key + "': " + e.what());
    }
}

template <typename T>
void RequirePositive(const std::string& key, T value)
{
    if (value == 0)
        throw std::runtime_error("Invalid '" + key + "': value must be greater than 0");
}

} // namespace

RuntimeConfig LoadRuntimeConfigFromJson(const std::string& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Failed to open runtime config '" + path + "'");

    json root;
    try {
        input >> root;
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse runtime config '" + path + "': " + e.what());
    }

    if (!root.is_object())
        throw std::runtime_error("Runtime config '" + path + "' must contain a JSON object");

    RuntimeConfig config;

    if (const auto* capture = FindObject(root, "capture")) {
        ReadValue(*capture, "capture", "interfaces", config.capture.ifaces);
        ReadValue(*capture, "capture", "ifaces", config.capture.ifaces);
        ReadValue(*capture, "capture", "record_packets", config.capture.record_packets);
        ReadValue(*capture, "capture", "debug_packets", config.capture.debug_packets);
    }

    if (const auto* redis = FindObject(root, "redis")) {
        ReadValue(*redis, "redis", "host", config.redis.host);
        ReadValue(*redis, "redis", "port", config.redis.port);
        ReadValue(*redis, "redis", "stream_key", config.redis.stream_key);
        ReadValue(*redis, "redis", "max_pending_writes", config.redis.max_pending_writes);
        ReadValue(*redis, "redis", "connect_timeout_ms", config.redis.connect_timeout_ms);
        ReadValue(*redis, "redis", "drain_timeout_ms", config.redis.drain_timeout_ms);
    }

    if (const auto* supervisor = FindObject(root, "supervisor")) {
        ReadValue(*supervisor, "supervisor", "restart_delay_ms", config.supervisor.restart_delay_ms);
        ReadValue(*supervisor, "supervisor", "poll_interval_ms", config.supervisor.poll_interval_ms);
    }

    if (const auto* ui = FindObject(root, "ui")) {
        ReadValue(*ui, "ui", "show_startup_art", config.ui.show_startup_art);
    }

    if (config.redis.host.empty())
        throw std::runtime_error("Invalid 'redis.host': value must not be empty");
    if (config.redis.port.empty())
        throw std::runtime_error("Invalid 'redis.port': value must not be empty");
    if (config.redis.stream_key.empty())
        throw std::runtime_error("Invalid 'redis.stream_key': value must not be empty");

    RequirePositive("redis.max_pending_writes", config.redis.max_pending_writes);
    RequirePositive("redis.connect_timeout_ms", config.redis.connect_timeout_ms);
    RequirePositive("redis.drain_timeout_ms", config.redis.drain_timeout_ms);
    RequirePositive("supervisor.restart_delay_ms", config.supervisor.restart_delay_ms);
    RequirePositive("supervisor.poll_interval_ms", config.supervisor.poll_interval_ms);

    return config;
}

} // namespace ElephantShrew
