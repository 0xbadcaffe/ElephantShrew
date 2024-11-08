#include <numeric>
#include "Processor.hpp"

namespace ElephantShrew {

uint64_t ElephantShrewProcessor::Process(std::shared_ptr<std::array<uint32_t, ElephantShrew_LINE_SIZE>> buffer) {
	if(NULL == buffer)
		return 0;

	uint64_t bufferSum = 0;
	return std::accumulate(buffer->begin(),buffer->end(), bufferSum );
}

}



