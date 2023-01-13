/*
 * ElephantShrewInboundHandler.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef ElephantShrewINBOUNDHANDLER_H_
#define ElephantShrewINBOUNDHANDLER_H_

#include "IElephantShrewReceiver.h"

namespace ElephantShrew {

class ElephantShrewInboundHandler {

public:

	ElephantShrewInboundHandler(std::shared_ptr<IElephantShrewReceiver> ElephantShrewReceiver);
	explicit ElephantShrewInboundHandler(const ElephantShrewInboundHandler& other) = delete;
	ElephantShrewInboundHandler& operator=(const ElephantShrewInboundHandler& rhs) = delete;
	explicit ElephantShrewInboundHandler(ElephantShrewInboundHandler&& other) = delete;
	ElephantShrewInboundHandler& operator=(const ElephantShrewInboundHandler&& rhs) = delete;
	virtual ~ElephantShrewInboundHandler() = default;

	std::shared_ptr<std::array<uint32_t,ElephantShrew_LINE_SIZE>> GetInbound();

private:

	std::shared_ptr<IElephantShrewReceiver> m_ElephantShrewReceiver;

	};


}



#endif /* ElephantShrewINBOUNDHANDLER_H_ */
