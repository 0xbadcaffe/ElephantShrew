/*
 * IElephantShrew.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef _IELEPHENTSHREW_H_
#define _IELEPHENTSHREW_H_

#include "ElephantShrewBootstrapper.hpp"

namespace ElephantShrew {

class IElephantShrew {

public :

	virtual ~ElephantShrewBootstrapper() = default;
	virtual std::unique_ptr<ElephantShrewBootstrapper> Init() = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
