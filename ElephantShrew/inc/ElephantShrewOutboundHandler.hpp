/*
 * ElephantShrewOutboundHandler.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef ElephantShrewOUTBOUNDHANDLER_H_
#define ElephantShrewOUTBOUNDHANDLER_H_

#include <memory>
#include "IElephantShrewTransmitter.h"

namespace ElephantShrew {

class ElephantShrewOutboundHandler {

public:

	ElephantShrewOutboundHandler(std::shared_ptr<IElephantShrewTransmitter> ElephantShrewTransmitter);
	explicit ElephantShrewOutboundHandler(const ElephantShrewOutboundHandler& other) = delete;
	ElephantShrewOutboundHandler& operator=(const ElephantShrewOutboundHandler& rhs) = delete;
	explicit ElephantShrewOutboundHandler(ElephantShrewOutboundHandler&& other) = delete;
	ElephantShrewOutboundHandler& operator=(const ElephantShrewOutboundHandler&& rhs) = delete;
	virtual ~ElephantShrewOutboundHandler() = default;

	void SendOutbound(uint64_t output);

private:

	std::shared_ptr<IElephantShrewTransmitter> m_ElephantShrewTransmitter;
	};


}



#endif /* ElephantShrewOUTBOUNDHANDLER_H_ */
