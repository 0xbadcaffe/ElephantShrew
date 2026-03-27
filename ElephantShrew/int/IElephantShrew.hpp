/*
 * IElephantShrew.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef _IELEPHENTSHREW_H_
#define _IELEPHENTSHREW_H_

#include "RuntimeConfig.hpp"

namespace ElephantShrew {

class IElephantShrew {

public :

    virtual ~IElephantShrew() = default;
    virtual void Init(const RuntimeConfig& config) = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
