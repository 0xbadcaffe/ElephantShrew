/*
 * IProcessor.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef IPROCESSOR_H_
#define IPROCESSOR_H_

#include "ElephantShrewCommonDefs.h"
#include <array>
#include <memory>

namespace ElephantShrew {

class IProcessor {

public:

	virtual ~IProcessor() = default;
	virtual uint64_t Process(std::shared_ptr<std::array<uint32_t, ElephantShrew_LINE_SIZE>> buffer) = 0;

	};
}



#endif /* IPROCESSOR_H_ */
