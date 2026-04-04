#ifndef _DATAPROCESSOR_H_
#define _DATAPROCESSOR_H_

#include "ITask.hpp"

namespace ElephantShrew {

class DataProcessor : public ITask {

public:

    DataProcessor() = default;
    explicit DataProcessor(const DataProcessor& other) = delete;
    DataProcessor& operator=(const DataProcessor& rhs) = delete;
    explicit DataProcessor(DataProcessor&& other) = delete;
    DataProcessor& operator=(const DataProcessor&& rhs) = delete;
    virtual ~DataProcessor() = default;

    void DoTask(void) override;
    void GetTaskStatus(void) override;
    void KillTask(void) override;
    void RestartTask(void) override;

    };


}

#endif /* DATAPROCESSOR_H__ */
