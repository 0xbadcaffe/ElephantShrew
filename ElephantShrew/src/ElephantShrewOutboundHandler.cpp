/*
 * ElephantShrewOutboundHandler.cpp
 *
 *  Created on: 3 Jun 2022
 *      Author: misteroy
 */
#include <thread>
#include "ElephantShrewOutboundHandler.h"

namespace ElephantShrew {

ElephantShrewOutboundHandler::ElephantShrewOutboundHandler(std::shared_ptr<IElephantShrewTransmitter> ElephantShrewTransmitter)
		: m_ElephantShrewTransmitter(ElephantShrewTransmitter) {

}

void ElephantShrewOutboundHandler::SendOutbound(uint64_t output) {
	//std::this_thread::sleep_for(std::chrono::milliseconds(200));
	m_ElephantShrewTransmitter->Transmit(output);
}

}


