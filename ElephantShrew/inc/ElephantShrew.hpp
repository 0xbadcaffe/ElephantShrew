#ifndef _ELEPHENTSHREW_H_
#define _ELEPHENTSHREW_H_

#include <iostream>
#include <memory>
#include "IElephantShrew.hpp"
#include "IElephantShrewReceiver.hpp"
#include "IElephantShrewTransmitter.hpp"
#include "IProcessor.hpp"
#include "ElephantShrewInboundHandler.hpp"
#include "ElephantShrewOutboundHandler.hpp"
#include "ElephantShrewOverseer.hpp"

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



#endif // _ELEPHENTSHREW_H_
