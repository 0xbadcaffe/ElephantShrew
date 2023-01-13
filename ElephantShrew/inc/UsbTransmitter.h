/*
 * UsbTransmitter.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef USBTRANSMITTER_H_
#define USBTRANSMITTER_H_

#include "IUsbTransmitter.h"

namespace ElephantShrew {

	class UsbTransmitter : public IUsbTransmitter {

	public:
		UsbTransmitter() = default;
		//copying is disabled
		explicit UsbTransmitter(const UsbTransmitter& other) = delete;
		UsbTransmitter& operator=(const UsbTransmitter& rhs) = delete;
		explicit UsbTransmitter(UsbTransmitter&& other) = delete;
		UsbTransmitter& operator=(const UsbTransmitter&& rhs) = delete;
		virtual ~UsbTransmitter() = default;

		virtual bool Transmit(const uint64_t output) override;

	};



}



#endif /* USBTRANSMITTER_H_ */
