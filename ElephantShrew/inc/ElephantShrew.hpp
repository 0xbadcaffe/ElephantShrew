#ifndef _ELEPHENTSHREW_H_
#define _ELEPHENTSHREW_H_

#include <iostream>
#include <memory>
#include "int/IElephantShrew.hpp"
#include "IReceiver.hpp"
#include "ITransmitter.hpp"
#include "IProcessor.hpp"
#include "InboundHandler.hpp"
#include "OutboundHandler.hpp"
#include "ElephantShrewOverseer.hpp"

namespace ElephantShrew {

	class ElephantShrew : public IElephantShrew {

	public:

		explicit ElephantShrew() = default;
		explicit ElephantShrew(const ElephantShrew& other) = delete;
		ElephantShrew& operator=(const ElephantShrew& rhs) = delete;
		explicit ElephantShrew(ElephantShrew&& other) = delete;
		ElephantShrew& operator=(const ElephantShrew&& rhs) = delete;

		virtual void Init() override;

		virtual ~ElephantShrew() = default;

	};

}



#endif // _ELEPHENTSHREW_H_
