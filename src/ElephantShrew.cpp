#include <algorithm>
#include <pcapplusplus/PcapLiveDevice.h>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include "ElephantShrew.hpp"
#include "PcapReceiver.hpp"
#include "RedisPacketStore.hpp"

namespace {

pcpp::PcapLiveDevice* ResolveCaptureDevice(const std::string& iface)
{
    if (iface.empty()) {
        const auto& dev_list = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
        if (dev_list.empty())
            throw std::runtime_error("ElephantShrew: no network interfaces found");

        auto* device = dev_list.front();
        spdlog::info("ElephantShrew: auto-selected interface '{}'", device->getName());
        return device;
    }

    auto* device = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(iface);
    if (!device)
        throw std::runtime_error("ElephantShrew: interface '" + iface + "' not found");

    return device;
}

void OpenDevice(pcpp::PcapLiveDevice& device, pcpp::PcapLiveDevice::PcapDirection direction)
{
    if (device.isOpened())
        return;

    const pcpp::PcapLiveDevice::DeviceConfiguration config(
        pcpp::PcapLiveDevice::Promiscuous,
        1,
        0,
        direction
    );

    if (!device.open(config))
        throw std::runtime_error("ElephantShrew: failed to open interface '" + device.getName() + "'");
}

void TrackOpenedDevice(std::vector<pcpp::PcapLiveDevice*>& opened_devices, pcpp::PcapLiveDevice* device)
{
    if (std::find(opened_devices.begin(), opened_devices.end(), device) == opened_devices.end())
        opened_devices.push_back(device);
}

} // namespace

namespace ElephantShrew
{

ElephantShrew::~ElephantShrew()
{
    receivers_.clear();
    CloseDevices();
}

void ElephantShrew::CloseDevices() noexcept
{
    for (auto it = opened_devices_.rbegin(); it != opened_devices_.rend(); ++it) {
        if (*it && (*it)->isOpened())
            (*it)->close();
    }

    opened_devices_.clear();
}

void ElephantShrew::Init(const RuntimeConfig& config)
{
    spdlog::info("ElephantShrew::Init");

    receivers_.clear();
    CloseDevices();

    const auto& capture_options = config.capture;
    const auto& routing_options = config.routing;
    const auto receiver_count = routing_options.enabled
                              ? (routing_options.bidirectional ? 2U : 1U)
                              : (capture_options.ifaces.empty() ? 1U : capture_options.ifaces.size());

    receivers_.reserve(receiver_count);

    std::shared_ptr<IPacketStore> store;
    if (capture_options.record_packets) {
        // A single store is shared across all capture interfaces so packets from
        // every interface land in the same DragonFly stream, tagged by iface name.
        store = std::make_shared<RedisPacketStore>(config.redis);
        spdlog::info("Packet recording enabled");
    } else {
        spdlog::info("Packet recording disabled");
    }

    if (capture_options.debug_packets)
        spdlog::info("Packet debug logging enabled");

    if (!capture_options.record_packets && !capture_options.debug_packets)
        spdlog::warn("Packets will be captured but neither recorded nor logged");

    if (routing_options.enabled) {
        auto* ingress_device = ResolveCaptureDevice(routing_options.ingress_iface);
        auto* egress_device = ResolveCaptureDevice(routing_options.egress_iface);

        // Capture only inbound traffic on bridged interfaces so frames injected
        // by ElephantShrew are not reflected back into the bridge loop.
        OpenDevice(*ingress_device, pcpp::PcapLiveDevice::PCPP_IN);
        TrackOpenedDevice(opened_devices_, ingress_device);
        OpenDevice(*egress_device, pcpp::PcapLiveDevice::PCPP_IN);
        TrackOpenedDevice(opened_devices_, egress_device);

        spdlog::info("ElephantShrew: routing traffic {} {} {}",
                     ingress_device->getName(),
                     routing_options.bidirectional ? "<->" : "->",
                     egress_device->getName());

        auto receiver = std::make_shared<PcapReceiver>(store,
                                                       ingress_device,
                                                       egress_device,
                                                       capture_options.record_packets,
                                                       capture_options.debug_packets);
        receiver->Receive();
        receivers_.push_back(std::move(receiver));

        if (routing_options.bidirectional) {
            auto reverse_receiver = std::make_shared<PcapReceiver>(store,
                                                                   egress_device,
                                                                   ingress_device,
                                                                   capture_options.record_packets,
                                                                   capture_options.debug_packets);
            reverse_receiver->Receive();
            receivers_.push_back(std::move(reverse_receiver));
        }

        return;
    }

    if (capture_options.ifaces.empty()) {
        auto* device = ResolveCaptureDevice("");
        OpenDevice(*device, pcpp::PcapLiveDevice::PCPP_INOUT);
        TrackOpenedDevice(opened_devices_, device);

        auto receiver = std::make_shared<PcapReceiver>(store,
                                                       device,
                                                       nullptr,
                                                       capture_options.record_packets,
                                                       capture_options.debug_packets);
        receiver->Receive();
        receivers_.push_back(std::move(receiver));
        return;
    }

    for (const auto& iface : capture_options.ifaces) {
        auto* device = ResolveCaptureDevice(iface);
        OpenDevice(*device, pcpp::PcapLiveDevice::PCPP_INOUT);
        TrackOpenedDevice(opened_devices_, device);

        auto receiver = std::make_shared<PcapReceiver>(store,
                                                       device,
                                                       nullptr,
                                                       capture_options.record_packets,
                                                       capture_options.debug_packets);
        receiver->Receive();
        receivers_.push_back(std::move(receiver));
    }
}

}
