#ifndef UDPRECEIVER_H_
#define UDPRECEIVER_H_

#include <fstream>
#include "IUdpReceiver.hpp"

namespace ElephantShrew {

	class UdpReceiver : public IUdpReceiver {

	public:
		UdpReceiver();
		explicit UdpReceiver(const UdpReceiver& other) = delete;
		UdpReceiver& operator=(const UdpReceiver& rhs) = delete;
		explicit UdpReceiver(UdpReceiver&& other) = delete;
		UdpReceiver& operator=(const UdpReceiver&& rhs) = delete;
		virtual ~UdpReceiver() = default;

		virtual void Receive() override;

	private:

		std::ifstream m_inputFile;
		uint32_t m_receivedLines = 0;

	};

}




#endif /* UDPRECEIVER_H_ */
