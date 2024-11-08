#ifndef _ITHREADPOOL_H_
#define _ITHREADPOOL_H_

#include <future>

namespace ElephantShrew {

class IThreadPool {

public :

	virtual ~IThreadPool() = default;
    virtual void Post(std::packaged_task<void()> job) = 0;

};

}


#endif // _ITHREADPOOL_H_
