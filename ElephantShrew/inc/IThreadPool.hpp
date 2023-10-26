/*
 * IThreadPool.hpp
 *
 *  Created on: 31 May 2022
 *      Author: Roy Cohen
 */

#ifndef ITHREADPOOL_H_
#define ITHREADPOOL_H_


namespace ElephantShrew {

class IThreadPool {

public :

	virtual ~IThreadPool() = default;
    virtual void Post(std::packaged_task<void()> job) = 0;

};

}


#endif /* ITHREADPOOL_H_ */
