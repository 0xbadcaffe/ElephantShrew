#ifndef _OUTBOUNDHANDLER_H_
#define _OUTBOUNDHANDLER_H_

#include <memory>
#include "ITransmitter.hpp"

namespace ElephantShrew {

class OutboundHandler {

public:

    explicit OutboundHandler(std::shared_ptr<ITransmitter> transmitter);
    explicit OutboundHandler(const OutboundHandler& other) = delete;
    OutboundHandler& operator=(const OutboundHandler& rhs) = delete;
    explicit OutboundHandler(OutboundHandler&& other) = delete;
    OutboundHandler& operator=(const OutboundHandler&& rhs) = delete;
    virtual ~OutboundHandler() = default;

    void SendOutbound(void);

private:

    std::shared_ptr<ITransmitter> transmitter_;
};


}

#endif // _OUTBOUNDHANDLER_H_
