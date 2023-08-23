/*
 * IFileReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IELEPHENTSHREW_FILERECEIVER_H_
#define IELEPHENTSHREW_FILERECEIVER_H_

#include "IElephantShrewReceiver.h"

namespace ElephantShrew {


class IElephantShrewFileReceiver : public IElephantShrewReceiver {

public:

	virtual ~ IElephantShrewFileReceiver() = default;

};


}



#endif /* IELEPHENTSHREW_FILERECEIVER_H_ */
