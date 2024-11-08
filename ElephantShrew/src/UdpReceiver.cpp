#include <memory>
#include "UdpReceiver.hpp"

namespace ElephantShrew {


UdpReceiver::UdpReceiver(){
	m_inputFile.open("ElephantShrew_input.txt");
}


std::shared_ptr<std::array<uint32_t,ElephantShrew_LINE_SIZE>> UdpReceiver::Receive() {

	if(ElephantShrew_MAX_COUNTS == ++m_receivedLines || m_inputFile.eof())
		return NULL;

	auto buffer = std::make_shared<std::array<uint32_t, ElephantShrew_LINE_SIZE>>();
	uint32_t num = 0;
	/* read ElephantShrew_LINE_SIZE numbers from input file */
	for(int i = 0; i < ElephantShrew_LINE_SIZE; ++i){
		num = 0;
		m_inputFile >> num;
		buffer->at(i) = num;
	}

	std::cout<< "UDP Receiver - ";
	for(uint32_t num : *buffer) {
		std::cout << num << ",";
	}
	std::cout << std::endl;

	return buffer;

}


}



