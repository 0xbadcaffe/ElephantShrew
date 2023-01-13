/*
 * FileReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef ELEPHENTSHREW_FILERECEIVER_H_
#define ELEPHENTSHREW_FILERECEIVER_H_

#include <fstream>
#include "IElephantShrewFileReceiver.h"

namespace ElephantShrew {

	class ElephantShrewFileReceiver : public IElephantShrewFileReceiver {

	public:
		ElephantShrewFileReceiver();
		explicit ElephantShrewFileReceiver(const ElephantShrewFileReceiver& other) = delete;
		ElephantShrewFileReceiver& operator=(const ElephantShrewFileReceiver& rhs) = delete;
		explicit ElephantShrewFileReceiver(ElephantShrewFileReceiver&& other) = delete;
		ElephantShrewFileReceiver& operator=(const ElephantShrewFileReceiver&& rhs) = delete;
		virtual ~ElephantShrewFileReceiver() = default;
		virtual std::shared_ptr<std::array<uint32_t,ELEPHANTSHREW_LINE_SIZE>> Receive() override;

	private:

		std::ifstream m_inputFile;
		uint32_t m_receivedLines = 0;
	};
}

#endif /* ELEPHENTSHREW_FILERECEIVER_H_ */
