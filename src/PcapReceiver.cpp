#include "PcapReceiver.hpp"
#include <pcapplusplus/ArpLayer.h>
#include <pcapplusplus/EthLayer.h>
#include <pcapplusplus/IPv4Layer.h>
#include <pcapplusplus/IPv6Layer.h>
#include <pcapplusplus/Packet.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ElephantShrew {

PcapReceiver::PcapReceiver(std::shared_ptr<IPacketStore> store,
                           pcpp::PcapLiveDevice* device,
                           pcpp::PcapLiveDevice* route_device,
                           bool record_packets,
                           bool debug_packets)
    : store_(std::move(store)),
      device_(device),
      route_device_(route_device),
      record_packets_(record_packets),
      debug_packets_(debug_packets)
{
}

PcapReceiver::~PcapReceiver()
{
    Stop();
}

void PcapReceiver::Receive()
{
    if (!device_)
        throw std::runtime_error("PcapReceiver: capture device is not set");
    if (!device_->isOpened())
        throw std::runtime_error("PcapReceiver: capture device '" + device_->getName() + "' is not open");
    if (route_device_ && !route_device_->isOpened()) {
        throw std::runtime_error(
            "PcapReceiver: route device '" + route_device_->getName() + "' is not open"
        );
    }

    if (route_device_) {
        spdlog::info("PcapReceiver: starting capture on '{}' and forwarding to '{}'",
                     device_->getName(),
                     route_device_->getName());
    } else {
        spdlog::info("PcapReceiver: starting capture on '{}'", device_->getName());
    }

    if (!device_->startCapture(onPacketArrives, this)) {
        throw std::runtime_error("PcapReceiver: failed to start capture on '" + device_->getName() + "'");
    }

    capturing_ = true;
}

void PcapReceiver::Stop()
{
    if (device_ && capturing_) {
        device_->stopCapture();
        capturing_ = false;
        spdlog::info("PcapReceiver: capture stopped on '{}'", device_->getName());
    }
}

void PcapReceiver::onPacketArrives(pcpp::RawPacket* packet,
                                   pcpp::PcapLiveDevice* /*dev*/,
                                   void* cookie)
{
    auto* receiver = static_cast<PcapReceiver*>(cookie);

    try {
        receiver->processPacket(packet);
    }
    catch (const std::exception& e) {
        const auto iface = receiver->device_ ? receiver->device_->getName() : std::string("unknown");
        spdlog::error("PcapReceiver: exception while processing packet on '{}': {}", iface, e.what());
    }
    catch (...) {
        const auto iface = receiver->device_ ? receiver->device_->getName() : std::string("unknown");
        spdlog::error("PcapReceiver: unknown exception while processing packet on '{}'", iface);
    }
}

void PcapReceiver::processPacket(pcpp::RawPacket* rawPacket)
{
    pcpp::Packet parsed(rawPacket);

    PacketInfo info;
    info.iface  = device_->getName();
    info.length = static_cast<uint32_t>(rawPacket->getRawDataLen());
    const auto packet_timestamp = rawPacket->getPacketTimeStamp();
    info.timestamp_us = static_cast<uint64_t>(packet_timestamp.tv_sec) * 1000000ULL +
                        static_cast<uint64_t>(packet_timestamp.tv_nsec / 1000);

    // Network layer — extract source/destination addresses
    if (auto* ip4 = parsed.getLayerOfType<pcpp::IPv4Layer>()) {
        info.src_ip = ip4->getSrcIPAddress().toString();
        info.dst_ip = ip4->getDstIPAddress().toString();
    } else if (auto* ip6 = parsed.getLayerOfType<pcpp::IPv6Layer>()) {
        info.src_ip = ip6->getSrcIPAddress().toString();
        info.dst_ip = ip6->getDstIPAddress().toString();
    } else if (auto* arp = parsed.getLayerOfType<pcpp::ArpLayer>()) {
        info.src_ip = arp->getSenderIpAddr().toString();
        info.dst_ip = arp->getTargetIpAddr().toString();
    } else {
        info.src_ip = "N/A";
        info.dst_ip = "N/A";
    }

    // Transport layer — determine protocol name
    if (parsed.isPacketOfType(pcpp::ARP))
        info.protocol = "ARP";
    else if (parsed.isPacketOfType(pcpp::TCP))
        info.protocol = "TCP";
    else if (parsed.isPacketOfType(pcpp::UDP))
        info.protocol = "UDP";
    else if (parsed.isPacketOfType(pcpp::ICMP))
        info.protocol = "ICMP";
    else
        info.protocol = "OTHER";

    if (debug_packets_) {
        if (route_device_) {
            spdlog::debug("PcapReceiver: packet on '{}' {} -> {} proto={} len={} routed_to='{}'",
                          info.iface,
                          info.src_ip,
                          info.dst_ip,
                          info.protocol,
                          info.length,
                          route_device_->getName());
        } else {
            spdlog::debug("PcapReceiver: packet on '{}' {} -> {} proto={} len={}",
                          info.iface,
                          info.src_ip,
                          info.dst_ip,
                          info.protocol,
                          info.length);
        }
    }

    if (route_device_) {
        if (!route_device_->sendPacket(*rawPacket)) {
            throw std::runtime_error(
                "PcapReceiver: failed to forward packet from '" + device_->getName() + "' to '" +
                route_device_->getName() + "'"
            );
        }
    }

    if (!record_packets_)
        return;

    if (!store_)
        throw std::runtime_error("PcapReceiver: packet recording enabled without a packet store");

    // Hex-encode the raw packet bytes only when recording is enabled.
    const uint8_t* data = rawPacket->getRawData();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint32_t i = 0; i < info.length; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    info.data_hex = oss.str();

    store_->Store(info);
}

} // namespace ElephantShrew
