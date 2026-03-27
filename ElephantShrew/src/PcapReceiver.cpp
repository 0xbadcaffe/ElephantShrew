#include "PcapReceiver.hpp"
#include <pcapplusplus/EthLayer.h>
#include <pcapplusplus/IPv4Layer.h>
#include <pcapplusplus/IPv6Layer.h>
#include <pcapplusplus/Packet.h>
#include <pcapplusplus/PcapLiveDeviceList.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace ElephantShrew {

PcapReceiver::PcapReceiver(std::shared_ptr<IPacketStore> store, const std::string& iface)
    : store_(std::move(store)), iface_(iface)
{
}

PcapReceiver::~PcapReceiver()
{
    Stop();
}

void PcapReceiver::Receive()
{
    if (iface_.empty()) {
        const auto& devList = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
        if (devList.empty()) {
            spdlog::error("PcapReceiver: no network interfaces found");
            return;
        }
        device_ = devList.front();
        spdlog::info("PcapReceiver: auto-selected interface '{}'", device_->getName());
    } else {
        device_ = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDeviceByName(iface_);
        if (!device_) {
            spdlog::error("PcapReceiver: interface '{}' not found", iface_);
            return;
        }
    }

    if (!device_->open()) {
        spdlog::error("PcapReceiver: failed to open interface '{}'", device_->getName());
        return;
    }

    spdlog::info("PcapReceiver: starting capture on '{}'", device_->getName());
    capturing_ = true;
    device_->startCapture(onPacketArrives, this);
}

void PcapReceiver::Stop()
{
    if (device_ && capturing_) {
        device_->stopCapture();
        device_->close();
        capturing_ = false;
        spdlog::info("PcapReceiver: capture stopped");
    }
}

void PcapReceiver::onPacketArrives(pcpp::RawPacket* packet,
                                   pcpp::PcapLiveDevice* /*dev*/,
                                   void* cookie)
{
    static_cast<PcapReceiver*>(cookie)->processPacket(packet);
}

void PcapReceiver::processPacket(pcpp::RawPacket* rawPacket)
{
    pcpp::Packet parsed(rawPacket);

    PacketInfo info;
    info.iface  = device_->getName();
    info.length = static_cast<uint32_t>(rawPacket->getRawDataLen());
    info.timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    // Network layer — extract source/destination addresses
    if (auto* ip4 = parsed.getLayerOfType<pcpp::IPv4Layer>()) {
        info.src_ip = ip4->getSrcIPAddress().toString();
        info.dst_ip = ip4->getDstIPAddress().toString();
    } else if (auto* ip6 = parsed.getLayerOfType<pcpp::IPv6Layer>()) {
        info.src_ip = ip6->getSrcIPAddress().toString();
        info.dst_ip = ip6->getDstIPAddress().toString();
    } else {
        info.src_ip = "N/A";
        info.dst_ip = "N/A";
    }

    // Transport layer — determine protocol name
    if (parsed.isPacketOfType(pcpp::TCP))
        info.protocol = "TCP";
    else if (parsed.isPacketOfType(pcpp::UDP))
        info.protocol = "UDP";
    else if (parsed.isPacketOfType(pcpp::ICMP))
        info.protocol = "ICMP";
    else
        info.protocol = "OTHER";

    // Hex-encode the raw packet bytes
    const uint8_t* data = rawPacket->getRawData();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint32_t i = 0; i < info.length; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    info.data_hex = oss.str();

    store_->Store(info);
}

} // namespace ElephantShrew
