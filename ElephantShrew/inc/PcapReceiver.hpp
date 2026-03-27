#ifndef _PCAPRECEIVER_H_
#define _PCAPRECEIVER_H_

#include "IReceiver.hpp"
#include "IPacketStore.hpp"
#include <pcapplusplus/PcapLiveDevice.h>
#include <atomic>
#include <memory>
#include <string>

namespace ElephantShrew {

class PcapReceiver : public IReceiver {
public:
    explicit PcapReceiver(std::shared_ptr<IPacketStore> store,
                          const std::string& iface = "");
    ~PcapReceiver() override;

    // Starts live capture on the configured interface (non-blocking).
    void Receive() override;

    // Stops the capture and closes the device.
    void Stop();

private:
    static void onPacketArrives(pcpp::RawPacket* packet,
                                pcpp::PcapLiveDevice* dev,
                                void* cookie);
    void processPacket(pcpp::RawPacket* packet);

    std::shared_ptr<IPacketStore> store_;
    pcpp::PcapLiveDevice*         device_{nullptr};
    std::string                   iface_;
    std::atomic<bool>             capturing_{false};
};

} // namespace ElephantShrew

#endif // _PCAPRECEIVER_H_
