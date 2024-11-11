#ifndef _PROCESSOR_H_
#define _PROCESSOR_H_

#include "ITask.hpp"

namespace ElephantShrew {

class DataProcessor : public IProcessor {

public:

    ElephantShrewProcessor() = default;
    explicit ElephantShrewProcessor(const ElephantShrewProcessor& other) = delete;
    ElephantShrewProcessor& operator=(const ElephantShrewProcessor& rhs) = delete;
    explicit ElephantShrewProcessor(ElephantShrewProcessor&& other) = delete;
    ElephantShrewProcessor& operator=(const ElephantShrewProcessor&& rhs) = delete;
    virtual ~ElephantShrewProcessor() = default;

    virtual uint64_t Process(void) override;

    };


}


#endif /* ElephantShrewPROCESSOR_H_ */
