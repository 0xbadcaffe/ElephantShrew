#ifndef _ITASK_H_
#define _ITASK_H_

#include <memory>

namespace ElephantShrew {

class ITask {

public:

    virtual ~ITask() = default;
    virtual void DoTask(void) = 0;
    virtual void GetTaskStatus(void) = 0;
    virtual void KillTask(void) = 0;
    virtual void RestartTask(void) = 0;

    };
}

#endif /* _ITASK_H_ */
