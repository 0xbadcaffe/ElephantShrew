/*
 * ElephantShrew.h
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef ELEPHENTSHREW_H_
#define ELEPHENTSHREW_H_

#include <iostream>
#include <memory>
#include "IElephantShrew.h"
#include "IElephantShrewReceiver.h"
#include "IElephantShrewTransmitter.h"
#include "IProcessor.h"
#include "ElephantShrewInboundHandler.h"
#include "ElephantShrewOutboundHandler.h"
#include <ElephantShrewOverseer.h>

namespace ElephantShrew {

	class ElephantShrew : public IElephantShrew {

	public:

		ElephantShrew() = default;
		explicit ElephantShrew(const ElephantShrew& other) = delete;
		ElephantShrew& operator=(const ElephantShrew& rhs) = delete;
		explicit ElephantShrew(ElephantShrew&& other) = delete;
		ElephantShrew& operator=(const ElephantShrew&& rhs) = delete;

		virtual std::shared_ptr<ElephantShrewOverseer> Init() override;

		virtual ~ElephantShrew() = default;

	};

}



#endif /* ELEPHENTSHREW_H_ */
