#include "OutboundHandler.hpp"

namespace ElephantShrew {

OutboundHandler::OutboundHandler(std::shared_ptr<ITransmitter> transmitter)
        : transmitter_(transmitter) {

}

void OutboundHandler::SendOutbound() {
    transmitter_->Transmit();
}

}


