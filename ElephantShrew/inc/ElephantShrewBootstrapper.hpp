#ifndef _ELEPHANTSHREWBOOTSTRAPPER_
#define _ELEPHANTSHREWBOOTSTRAPPER_

#include "ElephantShrew.h"


namespace ElephantShrew
{

	class ElephantShrewBootstrapper : public IElephantShrew {

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


#endif /* _ELEPHANTSHREWBOOTSTRAPPER* /