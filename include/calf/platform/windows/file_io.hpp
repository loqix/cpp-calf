#ifndef CALF_PLATFORM_WINDOWS_FILE_IO_HPP_
#define CALF_PLATFORM_WINDOWS_FILE_IO_HPP_

#include "win32.hpp"
#include "win32_debug.hpp"
#include "kernel_object.hpp"
#include "../../worker_service.hpp"

#include <mutex>
#include <atomic>
#include <sstream>
#include <iostream>
#include <functional>

namespace calf {
namespace platform {
namespace windows {

using io_handler = std::function<void(void)>;

struct overlapped_io_context {
  overlapped_io_context() {
    // 重叠结构使用前必须主动置零。
    memset(&overlapped, 0, sizeof(overlapped));
  }

  OVERLAPPED overlapped;
  io_handler handler;
};

class io_completion_port
  : public kernel_object {
public:
  io_completion_port() { create(); }

  void associate(HANDLE file, ULONG_PTR key) {
    win32_assert(is_valid());

    HANDLE port = ::CreateIoCompletionPort(
        file, 
        handle_, 
        key,
        0);
    win32_assert(port == handle_);
  }

  void wait(
      LPOVERLAPPED* overlapped,
      PULONG_PTR key,
      LPDWORD bytes_transferred,
      DWORD timeout = INFINITE) {
    win32_assert(is_valid());

    BOOL bret = ::GetQueuedCompletionStatus(
        handle_,
        bytes_transferred, 
        key, 
        overlapped,
        timeout);

    win32_assert(bret != FALSE);
  }

  void notify(
      DWORD bytes_transferred,
      ULONG_PTR key, 
      LPOVERLAPPED overlapped) {
    win32_assert(is_valid());

    BOOL bret = ::PostQueuedCompletionStatus(
        handle_, 
        bytes_transferred,
        key, 
        overlapped);

    win32_assert(bret != FALSE);
  }

protected:
  void create() {
    handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    win32_assert(is_valid()) << "Create IoCompletionPort Failed.";
  }
};

class io_completion_handler {
public:
  virtual void io_completed(overlapped_io_context* context) = 0;
  virtual ~io_completion_handler() {}
};

class io_completion_service {
public:
  io_completion_service() : quit_flag_(false) {}

  void run_loop() {
    DWORD bytes_transferred = 0;
    ULONG_PTR key = NULL;
    LPOVERLAPPED overlapped = NULL;
    while (!quit_flag_.load(std::memory_order_relaxed)) {
      iocp_.wait(&overlapped, &key, &bytes_transferred);
      io_completion_handler* handler = reinterpret_cast<io_completion_handler*>(key);
      overlapped_io_context* context = reinterpret_cast<overlapped_io_context*>(overlapped);
      if (handler != nullptr) {
        handler->io_completed(context);
      }
    }
  }

  void dispatch(io_completion_handler* handler, overlapped_io_context* context) {
    iocp_.notify(
        0,
        reinterpret_cast<ULONG_PTR>(handler), 
        reinterpret_cast<OVERLAPPED*>(context));
  }

protected:
  void register_handle(HANDLE handle, io_completion_handler* handler) {
    iocp_.associate(handle, reinterpret_cast<ULONG_PTR>(handler));
  }

protected:
  io_completion_port iocp_;
  std::mutex mtx_;
  std::atomic_bool quit_flag_;

friend class named_pipe;
};

class file
  : public kernel_object,
    public io_completion_handler {
protected:
  static const int default_buffer_size = 4 * 1024;
  static const int default_timeout = 5 * 1000;

public:
  // Override io_completion_handler
  virtual void io_completed(overlapped_io_context* context) {}

public:
  void read(
      overlapped_io_context& context, 
      const io_handler& handler) {
    win32_assert(is_valid());

  }
};

class io_completion_worker
  : public io_completion_handler {
public:
  io_completion_worker(io_completion_service& service)
    : service_(service) {}

  template<typename ...Args>
  void dispatch(Args&&... args) {
    worker_.dispatch(std::forward<Args>(args)...);
    service_.dispatch(this, nullptr);
  }

  template<typename ...Args>
  decltype(auto) package_dispatch(Args&&... args) {
    auto result = worker_.package_dispatch(std::forward<Args>(args)...);
    service_.dispatch(this, nullptr);
    return result;
  }

  // Override io_completion_handler
  virtual void io_completed(overlapped_io_context* context) {
    worker_.run_one();
  }

private:
  worker_service worker_;
  io_completion_service& service_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_ASYNC_IO_HPP_