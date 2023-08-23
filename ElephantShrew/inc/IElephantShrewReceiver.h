/*
 * IElephantShrewReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IELEPHENTSHREW_RECEIVER_H_
#define IELEPHENTSHREW_RECEIVER_H_

#include <memory>
#include <array>
#include <iostream>
#include "ElephantShrewCommonDefs.h"

namespace ElephantShrew {

class IElephantShrewReceiver {

public:

	virtual ~IElephantShrewReceiver() = default;
	virtual std::shared_ptr<std::array<uint32_t,ELEPHANTSHRW_LINE_SIZE>> Receive() = 0;

};

}




#endif /* IELEPHENTSHREW_RECEIVER_H_ */
