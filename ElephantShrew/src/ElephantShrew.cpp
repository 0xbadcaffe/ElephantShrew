#include <spdlog/spdlog.h>
#include "ElephantShrew.hpp"
#include "PcapReceiver.hpp"
#include "RedisPacketStore.hpp"

namespace ElephantShrew
{

void ElephantShrew::Init(const std::vector<std::string>& ifaces)
{
    spdlog::info("ElephantShrew::Init");

    // A single store is shared across all capture interfaces so packets from
    // every interface land in the same DragonFly stream, tagged by iface name.
    auto store = std::make_shared<RedisPacketStore>();

    if (ifaces.empty()) {
        // No interface specified — auto-select the first available one.
        auto receiver = std::make_shared<PcapReceiver>(store);
        receiver->Receive();
        receivers_.push_back(std::move(receiver));
    } else {
        for (const auto& iface : ifaces) {
            auto receiver = std::make_shared<PcapReceiver>(store, iface);
            receiver->Receive();
            receivers_.push_back(std::move(receiver));
        }
    }
}

}

