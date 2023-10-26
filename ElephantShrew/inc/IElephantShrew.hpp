/*
 * IElephantShrew.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IELEPHENTSHREW_H_
#define IELEPHENTSHREW_H_

#include "ElephantShrewOverseer.hpp"

namespace ElephantShrew {

class IElephantShrew {

public :

	virtual ~IElephantShrew() = default;
	virtual std::shared_ptr<ElephantShrewOverseer> Init() = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
