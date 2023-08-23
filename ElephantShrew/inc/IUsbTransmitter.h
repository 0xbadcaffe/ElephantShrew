/*
 * IUsbTransmitter.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IUSBTRANSMITTER_H_
#define IUSBTRANSMITTER_H_

#include "IElephantShrewTransmitter.h"

namespace ElephantShrew {


class IUsbTransmitter : public IElephantShrewTransmitter {

public:

	virtual ~IUsbTransmitter() = default;

};


}



#endif /* IUSBTRANSMITTER_H_ */
