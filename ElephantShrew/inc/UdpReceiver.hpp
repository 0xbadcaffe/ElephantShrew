/*
 * UdpReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef UDPRECEIVER_H_
#define UDPRECEIVER_H_

#include <fstream>
#include "IUdpReceiver.h"

namespace ElephantShrew {

	class UdpReceiver : public IUdpReceiver {

	public:
		UdpReceiver();
		explicit UdpReceiver(const UdpReceiver& other) = delete;
		UdpReceiver& operator=(const UdpReceiver& rhs) = delete;
		explicit UdpReceiver(UdpReceiver&& other) = delete;
		UdpReceiver& operator=(const UdpReceiver&& rhs) = delete;
		virtual ~UdpReceiver() = default;

		virtual std::shared_ptr<std::array<uint32_t,ElephantShrew_LINE_SIZE>> Receive() override;

	private:

		std::ifstream m_inputFile;
		uint32_t m_receivedLines = 0;

	};

}




#endif /* UDPRECEIVER_H_ */
