#ifndef CALF_MULTI_THREAD_HPP
#define CALF_MULTI_THREAD_HPP

#include <atomic>
#include <functional>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <future>
#include <type_traits>


namespace calf {

class worker_service {
public:
  using task_t = std::function<void(void)>;

public:
  worker_service() : quit_flag_(false) {}
  ~worker_service() {
    quit_flag_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
  }

  void run_loop() {
    while (!quit_flag_.load(std::memory_order_relaxed)) {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() -> bool {
        return !task_queue_.empty() || quit_flag_.load(std::memory_order_relaxed);
      });
      do_work(lock);
    }
  }

  void run_one() {
    std::unique_lock<std::mutex> lock(mutex_);
    do_work(lock);
  }

  template<typename Fn, 
      typename ...Args,
      typename Ret = typename std::result_of<Fn>::type>
  std::future<Ret> package_dispatch(Fn&& fn, Args&&... args) {
    std::unique_lock<std::mutex> lock(mutex_);
    std::packaged_task<Ret(Args...)> pkg_task(std::forward<Fn>(fn));
    auto task_future = pkg_task.get_future();
    task_queue_.emplace_back(
        std::bind(std::move(pkg_task), std::forward<Args>(args)...));
    cv_.notify_one();
    lock.unlock();
    return std::move(task_future);
  }

  template<typename Fn, typename ...Args>
  void dispatch(Fn&& fn, Args&&... args) {
    std::unique_lock<std::mutex> lock(mutex_);
    task_queue_.emplace_back(std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
    cv_.notify_one();
  }

private: 
  void do_work(std::unique_lock<std::mutex>& lock) {
    while (!task_queue_.empty() && 
        !quit_flag_.load(std::memory_order_relaxed)) {
      task_t task = task_queue_.front();
      task_queue_.pop_front();
      lock.unlock();
      task();
      lock.lock();
    }
  }

private: 
  std::deque<task_t> task_queue_;
  std::condition_variable cv_;
  std::mutex mutex_;
  std::atomic_bool quit_flag_;
};

} // namespace calf

#endif // CALF_MULTI_THREAD_HPP