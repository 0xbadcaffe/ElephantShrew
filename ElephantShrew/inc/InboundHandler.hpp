#ifndef _INBOUNDHANDLER_H_
#define _INBOUNDHANDLER_H_

#include "IReceiver.hpp"

namespace ElephantShrew {

class InboundHandler {

public:

    InboundHandler(std::shared_ptr<IReceiver> ElephantShrewReceiver);
    explicit InboundHandler(const InboundHandler& other) = delete;
    InboundHandler& operator=(const InboundHandler& rhs) = delete;
    explicit InboundHandler(InboundHandler&& other) = delete;
    InboundHandler& operator=(const InboundHandler&& rhs) = delete;
    virtual ~InboundHandler() = default;

    void GetInbound();

private:

    std::shared_ptr<IReceiver> m_Receiver;

    };


}

#endif /* _INBOUNDHANDLER_H_ */
