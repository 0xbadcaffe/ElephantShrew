#include "ThreadPool.hpp"


namespace ElephantShrew 
{

ThreadPool::ThreadPool(int num_threads = 1) 
{
    for (int i = 0; i < num_threads; i++) {
      m_pool.emplace_back(&ThreadPool::Run, this);
    }
}

void ThreadPool::Run() noexcept 
{
  while (m_is_active)
  {
    thread_local std::packaged_task<void()> job;
      {
        std::unique_lock lock(guard);
        m_cv.wait(lock, [&]{ return !m_pending_jobs.empty() || !m_is_active; });
        if (!m_is_active) break;
        job.swap(m_pending_jobs.front());
        m_pending_jobs.pop_front();
      }
      job();
    }
}

void ThreadPool::Post(std::packaged_task<void()> job) 
{
  std::unique_lock lock(guard);
  m_pending_jobs.emplace_back(std::move(job));
  m_cv.notify_one();
}

ThreadPool::~ThreadPool() 
{
    m_is_active = false;
    m_cv.notify_all();
    for (auto& th : m_pool) 
    {
      th.join();
    }
  }
}

#if 0

class thread_pool
{
private:
  std::atomic_bool is_active {true};
  std::vector<std::thread> pool;
  std::condition_variable cv;
  std::mutex guard;
  std::deque<std::packaged_task<void()>> pending_jobs;
public:
  explicit thread_pool(int num_threads = 1) 
  {
    for (int i = 0; i < num_threads; i++) {
      pool.emplace_back(&thread_pool::run, this);
    }
  }
  ~thread_pool() {
    is_active = false;
    cv.notify_all();
    for (auto& th : pool) {
      th.join();
    }
  }

  void post(std::packaged_task<void()> job) {
    std::unique_lock lock(guard);
    pending_jobs.emplace_back(std::move(job));
    cv.notify_one();
  }
private:
  void run() noexcept 
  {
    while (is_active)
    {
      thread_local std::packaged_task<void()> job;
      {
        std::unique_lock lock(guard);
        cv.wait(lock, [&]{ return !pending_jobs.empty() || !is_active; });
        if (!is_active) break;
        job.swap(pending_jobs.front());
        pending_jobs.pop_front();
      }
      job();
    }
  }
};



int main()
{
    using namespace std::chrono_literals;
    thread_pool pool {2};
    auto waiter = 
        post(pool, use_future([] 
        {
            std::this_thread::sleep_for(1s);
            return 2;
        }));

    auto test_lmbda = [] {
        thread_local int count = 1;
        std::cout 
        << "working thread: " << std::this_thread::get_id()
        << "\tcount: " << count++ << std::endl;
        std::this_thread::sleep_for(50ms);
    };
    for (size_t i = 0; i < 10; i++)
    {
        post(pool, test_lmbda);
    }
    return waiter.get();
}


#endif