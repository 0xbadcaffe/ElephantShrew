#include "InboundHandler.h"

namespace ElephantShrew {

ElephantShrewInboundHandler::ElephantShrewInboundHandler(std::shared_ptr<IElephantShrewReceiver>	ElephantShrewReceiver)
		: m_ElephantShrewReceiver(ElephantShrewReceiver) {

}

std::shared_ptr<std::array<uint32_t,ElephantShrew_LINE_SIZE>> ElephantShrewInboundHandler::GetInbound() {
	return m_ElephantShrewReceiver->Receive();
}

}



