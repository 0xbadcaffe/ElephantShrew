/*
 * IElephantShrewTransmitter.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IElephantShrewTRANSMITTER_H_
#define IElephantShrewTRANSMITTER_H_

#include <string>

namespace ElephantShrew {

class IElephantShrewTransmitter {

public:

	virtual ~IElephantShrewTransmitter() = default;
	virtual bool Transmit(const uint64_t output) = 0;

	};
}




#endif /* IElephantShrewTRANSMITTER_H_ */
