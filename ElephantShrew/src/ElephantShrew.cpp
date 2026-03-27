#include <spdlog/spdlog.h>
#include "ElephantShrew.hpp"
#include "PcapReceiver.hpp"
#include "RedisPacketStore.hpp"

namespace ElephantShrew
{

void ElephantShrew::Init(const RuntimeConfig& config)
{
    spdlog::info("ElephantShrew::Init");

    const auto& options = config.capture;

    receivers_.clear();
    receivers_.reserve(options.ifaces.empty() ? 1U : options.ifaces.size());

    std::shared_ptr<IPacketStore> store;
    if (options.record_packets) {
        // A single store is shared across all capture interfaces so packets from
        // every interface land in the same DragonFly stream, tagged by iface name.
        store = std::make_shared<RedisPacketStore>(config.redis);
        spdlog::info("Packet recording enabled");
    } else {
        spdlog::info("Packet recording disabled");
    }

    if (options.debug_packets)
        spdlog::info("Packet debug logging enabled");

    if (!options.record_packets && !options.debug_packets)
        spdlog::warn("Packets will be captured but neither recorded nor logged");

    if (options.ifaces.empty()) {
        // No interface specified — auto-select the first available one.
        auto receiver = std::make_shared<PcapReceiver>(store, "", options.record_packets, options.debug_packets);
        receiver->Receive();
        receivers_.push_back(std::move(receiver));
    } else {
        for (const auto& iface : options.ifaces) {
            auto receiver = std::make_shared<PcapReceiver>(store, iface, options.record_packets, options.debug_packets);
            receiver->Receive();
            receivers_.push_back(std::move(receiver));
        }
    }
}

}
