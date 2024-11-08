/*
 * IElephantShrew.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef _IELEPHENTSHREW_H_
#define _IELEPHENTSHREW_H_

#include <memory>

namespace ElephantShrew {

class IElephantShrew {

public :

    virtual ~IElephantShrew() = default;
    virtual std::unique_ptr<IElephantShrew> Init() = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
