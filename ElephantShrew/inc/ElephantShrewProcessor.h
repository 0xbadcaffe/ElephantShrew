/*
 * ElephantShrewProcessor.h
 *
 *  Created on: 1 Jun 2022
 *      Author: misteroy
 */

#ifndef ElephantShrewPROCESSOR_H_
#define ElephantShrewPROCESSOR_H_

#include "IProcessor.h"

namespace ElephantShrew {

class ElephantShrewProcessor : public IProcessor {

public:

	ElephantShrewProcessor() = default;
	explicit ElephantShrewProcessor(const ElephantShrewProcessor& other) = delete;
	ElephantShrewProcessor& operator=(const ElephantShrewProcessor& rhs) = delete;
	explicit ElephantShrewProcessor(ElephantShrewProcessor&& other) = delete;
	ElephantShrewProcessor& operator=(const ElephantShrewProcessor&& rhs) = delete;
	virtual ~ElephantShrewProcessor() = default;

	virtual uint64_t Process(std::shared_ptr<std::array<uint32_t, ElephantShrew_LINE_SIZE>> buffer) override;

	};


}


#endif /* ElephantShrewPROCESSOR_H_ */
