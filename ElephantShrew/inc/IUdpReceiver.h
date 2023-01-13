/*
 * IUdpReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef IUDPRECEIVER_H_
#define IUDPRECEIVER_H_

#include "IElephantShrewReceiver.h"

namespace ElephantShrew {


class IUdpReceiver : public IElephantShrewReceiver {

public:

	virtual ~IUdpReceiver() = default;

};


}



#endif /* IUDPRECEIVER_H_ */
