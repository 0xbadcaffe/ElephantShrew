/*
 * UdpTransmitter.cpp
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */
#include <UdpTransmitter.h>
#include <iostream>

namespace  ElephantShrew {

bool UdpTransmitter::Transmit(const uint64_t output) {
	std::cout << "UDP Transmit - " << output << std::endl;
	return true;
}


}


