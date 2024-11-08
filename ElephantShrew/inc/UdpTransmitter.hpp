#ifndef _UDPTRANSMITTER_H_
#define _UDPTRANSMITTER_H_

#include "IUdpTransmitter.hpp"

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

		virtual bool Transmit() override;

	};

}



#endif /* UDPTRANSMITTER_H_ */
