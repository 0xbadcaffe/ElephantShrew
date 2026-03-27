/*
 * IElephantShrew.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef _IELEPHENTSHREW_H_
#define _IELEPHENTSHREW_H_

#include <memory>
#include <string>
#include <vector>

namespace ElephantShrew {

struct CaptureOptions {
    std::vector<std::string> ifaces;
    bool record_packets{false};
    bool debug_packets{false};
};

class IElephantShrew {

public :

    virtual ~IElephantShrew() = default;
    virtual void Init(const CaptureOptions& options) = 0;

};



}



#endif /* IELEPHENTSHREW_H_ */
