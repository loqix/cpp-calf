#ifndef CALF_SINGLE_INSTANCE_H_
#define CALF_SINGLE_INSTANCE_H_

#include <atomic>
#include <mutex>

namespace calf {

// 线程安全单例，延迟初始化
template<typename T>
class SingleInstanceThreadSafe {
public:
  static T* GetInstance() {
    // 双重检查锁定模式 DCLP
    // 补充了内存序保障
    T* pt = instance_.load(std::memory_order_acquire);
    if (pt == nullptr) {
      std::unique_lock<std::mutex> lock(mutex_);
      pt = instance_.load(std::memory_order_relaxed);
      if (pt == nullptr) {
        pt = new T();
        instance_.store(pt, std::memory_order_release);
      }
    }
    return pt;
  }

  static void ReleaseInstance() {
    T* pt = instance_.load(std::memory_order_acquire);
    if (pt != nullptr) {
      std::unique_lock<std::mutex> lock(mutex_);
      if (instance_.compare_exchange_strong(pt, nullptr)) {
        delete pt;
        pt = nullptr;
      }
    }
  }

private:
  static std::mutex mutex_;
  static std::atomic<T*> instance_;
};

template<typename T>
std::atomic<T*> SingleInstanceThreadSafe<T>::instance_ = nullptr;

template<typename T>
std::mutex SingleInstanceThreadSafe<T>::mutex_;

} // namespace calf

#endif // CALF_SINGLE_INSTANCE_H_
