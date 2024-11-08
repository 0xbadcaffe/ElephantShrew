#ifndef _IPROCESSOR_H_
#define _IPROCESSOR_H_

#include <cstdint>
#include <memory>

namespace ElephantShrew {

class IProcessor {

public:

    virtual ~IProcessor() = default;
    virtual std::uint64_t Process(void) = 0;

    };
}



#endif /* IPROCESSOR_H_ */
