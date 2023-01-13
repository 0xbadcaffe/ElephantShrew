/*
 * IElephantShrew.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef IELEPHENTSHREW_H_
#define IELEPHENTSHREW_H_

#include "ElephantShrewOverseer.h"

namespace elephantShrew {

class IElephantShrew {

public :

	virtual ~IElephantShrew() = default;
	virtual std::shared_ptr<ElephantShrewOverseer> Init() = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
