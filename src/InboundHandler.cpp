#include "InboundHandler.hpp"

namespace ElephantShrew {

InboundHandler::InboundHandler(std::shared_ptr<IReceiver>	ElephantShrewReceiver)
		: receiver_(ElephantShrewReceiver) {

}

void InboundHandler::GetInbound() {
	receiver_->Receive();
}

}



