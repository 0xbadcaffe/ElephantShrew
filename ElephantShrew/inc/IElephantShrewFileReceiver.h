/*
 * IFileReceiver.h
 *
 *  Created on: 31 May 2022
 *      Author: misteroy
 */

#ifndef IELEPHENTSHREW_FILERECEIVER_H_
#define IELEPHENTSHREW_FILERECEIVER_H_

#include "IElephantShrewReceiver.h"

namespace elephantShrew {


class IElephantShrewFileReceiver : public IElephantShrewReceiver {

public:

	virtual ~ IElephantShrewFileReceiver() = default;

};


}



#endif /* IELEPHENTSHREW_FILERECEIVER_H_ */
