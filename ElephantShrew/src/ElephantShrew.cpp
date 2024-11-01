/*
 * ElephantShrew.cpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */
#include "ElephantShrewCommonDefs.hpp"
#include "ElephantShrew.hpp"
#include "UdpTransmitter.hpp"
#include "UdpReceiver.hpp"
#include "ElephantShrewProcessor.hpp"

namespace ElephantShrew
{

std::shared_ptr<ElephantShrewOverseer> ElephantShrew::Execute()
{

	std::cout << "ElephantShrew bootstrapper" << std::endl;

	// Create transmitter according to configuration
	std::shared_ptr<IElephantShrewTransmitter> transmitter;
	if (USB_TRANSMIT)
		transmitter = std::make_shared<UsbTransmitter>();
	else // udp
		transmitter = std::make_shared<UdpTransmitter>();

	// Create receiver according to configuration
	std::shared_ptr<IElephantShrewReceiver> receiver;
	if (FILE_RECEIVE)
		receiver = std::make_shared<FileReceiver>();
	else // udp
		receiver = std::make_shared<UdpReceiver>();

	// Create ElephantShrew processor unit
	auto processor = std::make_shared<ElephantShrewProcessor>();

	// Create Inbound handler
	auto inboundHandler = std::make_shared<ElephantShrewInboundHandler>(receiver);

	// Create Outbound handler
	auto outboundHandler = std::make_shared<ElephantShrewOutboundHandler>(transmitter);


	auto overseer = std::make_shared<ElephantShrewOverseer>(
											processor,
											outboundHandler,
											inboundHandler);

	return overseer;
}


}

