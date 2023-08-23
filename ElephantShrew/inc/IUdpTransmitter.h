/*
 * IUdpTransmitter.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IUDPTRANSMITTER_H_
#define IUDPTRANSMITTER_H_

#include "IElephantShrewTransmitter.h"

namespace ElephantShrew {

class IUdpTransmitter : public IElephantShrewTransmitter {

public:

	virtual ~IUdpTransmitter() = default;

};
}

#endif /* IUDPTRANSMITTER_H_ */
