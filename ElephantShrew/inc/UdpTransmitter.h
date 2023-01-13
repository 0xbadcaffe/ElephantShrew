/*
 * UdpTransmitter.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef UDPTRANSMITTER_H_
#define UDPTRANSMITTER_H_

#include "IUdpTransmitter.h"

namespace ElephantShrew {

	class UdpTransmitter : public IUdpTransmitter {

	public:
		UdpTransmitter() = default;
		//copying is disabled
		explicit UdpTransmitter(const UdpTransmitter& other) = delete;
		UdpTransmitter& operator=(const UdpTransmitter& rhs) = delete;
		explicit UdpTransmitter(UdpTransmitter&& other) = delete;
		UdpTransmitter& operator=(const UdpTransmitter&& rhs) = delete;
		virtual ~UdpTransmitter() = default;

		virtual bool Transmit(const uint64_t output) override;

	};

}



#endif /* UDPTRANSMITTER_H_ */
