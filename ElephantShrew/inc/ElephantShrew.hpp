#ifndef _ELEPHENTSHREW_H_
#define _ELEPHENTSHREW_H_

#include <iostream>
#include <memory>
#include "IElephantShrew.hpp"
#include "IReceiver.hpp"
#include "ITransmitter.hpp"
#include "IProcessor.hpp"
#include "InboundHandler.hpp"
#include "OutboundHandler.hpp"
#include "Overseer.hpp"

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
