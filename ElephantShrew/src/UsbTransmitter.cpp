/*
 * UsbTransmitter.cpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */
#include <iostream>
#include "UsbTransmitter.h"

namespace ElephantShrew {

bool UsbTransmitter::Transmit(const uint64_t output) {
	std::cout << "USB Transmit - " << output << std::endl;
	return true;
}


}



