#ifndef _UDPRECEIVER_H_
#define _UDPRECEIVER_H_

#include "IUdpReceiver.hpp"

namespace ElephantShrew {

    class UdpReceiver : public IUdpReceiver {

    public:
        UdpReceiver();
        explicit UdpReceiver(const UdpReceiver& other) = delete;
        UdpReceiver& operator=(const UdpReceiver& rhs) = delete;
        explicit UdpReceiver(UdpReceiver&& other) = delete;
        UdpReceiver& operator=(const UdpReceiver&& rhs) = delete;
        virtual ~UdpReceiver() = default;

        virtual void Receive() override;

    };

}
#endif /* UDPRECEIVER_H_ */
