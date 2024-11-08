#ifndef _OUTBOUNDHANDLER_H_
#define _OUTBOUNDHANDLER_H_

#include <memory>
#include "ITransmitter.hpp"

namespace ElephantShrew {

class OutboundHandler {

public:

    OutboundHandler(std::shared_ptr<OutboundHandler> ElephantShrewTransmitter);
    explicit OutboundHandler(const OutboundHandler& other) = delete;
    OutboundHandler& operator=(const OutboundHandler& rhs) = delete;
    explicit OutboundHandler(OutboundHandler&& other) = delete;
    OutboundHandler& operator=(const OutboundHandler&& rhs) = delete;
    virtual ~OutboundHandler() = default;

    void SendOutbound(uint64_t output);

private:

    std::shared_ptr<ITransmitter> m_Transmitter;
};


}

#endif // _OUTBOUNDHANDLER_H_
