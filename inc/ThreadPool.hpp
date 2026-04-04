#ifndef _THREADPOOL_H_
#define _THREADPOOL_H

#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <vector>
#include <deque>
#include <type_traits>

#include "IThreadPool.hpp"

namespace ElephantShrew 
{

class ThreadPool : public IThreadPool {
    public:
        explicit ThreadPool(int num_threads);
        //copying is disabled
        explicit ThreadPool(const ThreadPool& other) = delete;
        ThreadPool& operator=(const ThreadPool& rhs) = delete;
        explicit ThreadPool(ThreadPool&& other) = delete;
        ThreadPool& operator=(const ThreadPool&& rhs) = delete;
        virtual ~ThreadPool();

        void Post(std::packaged_task<void()> job) override;

    private:

        void Run() noexcept; 

        std::atomic_bool m_is_active {true};
        std::vector<std::thread> m_pool;
        std::condition_variable m_cv;
        std::mutex guard;
        std::deque<std::packaged_task<void()>> m_pending_jobs;

    };

}

#endif // _THREADPOOL_H
