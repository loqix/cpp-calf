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
#include <vector>

namespace calf {
namespace platform {
namespace windows {

struct overlapped_io_context;

using io_handler = std::function<void(overlapped_io_context&)>;
using io_buffer = std::vector<std::uint8_t>;

enum struct io_type {
  unknown,
  write, 
  read,
  broken
};

enum struct io_mode {
  open,
  create
};

struct overlapped_io_context {
  overlapped_io_context() 
    : bytes_transferred(0),
      is_pending(false),
      type(io_type::unknown) {
    // 重叠结构使用前必须主动置零。
    memset(&overlapped, 0, sizeof(overlapped));
  }

  // 必须保证 OVERLAPPED 结构体在内存最前。
  OVERLAPPED overlapped;
  std::size_t bytes_transferred;
  io_handler handler;
  bool is_pending;
  io_type type;
};

struct io_context
  : public overlapped_io_context {
  io_buffer buffer;
};

class io_completion_port
  : public kernel_object {
public:
  io_completion_port() { create(); }

  void associate(HANDLE file, ULONG_PTR key) {
    CALF_ASSERT(is_valid());

    HANDLE port = ::CreateIoCompletionPort(
        file, 
        handle_, 
        key,
        0);
    CALF_WIN32_CHECK(port == handle_, CreateIoCompletionPort);
  }

  bool wait(
      LPOVERLAPPED* overlapped,
      PULONG_PTR key,
      LPDWORD bytes_transferred,
      DWORD* err,
      DWORD timeout = INFINITE) {
    CALF_ASSERT(is_valid());

    BOOL bret = ::GetQueuedCompletionStatus(
        handle_,
        bytes_transferred, 
        key, 
        overlapped,
        timeout);

    //CALF_WIN32_CHECK(bret != FALSE, GetQueuedCompletionStatus);
    if (bret == FALSE) {
      *err = ::GetLastError();
    }
    return bret != FALSE;
  }

  void notify(
      DWORD bytes_transferred,
      ULONG_PTR key, 
      LPOVERLAPPED overlapped) {
    CALF_ASSERT(is_valid());

    BOOL bret = ::PostQueuedCompletionStatus(
        handle_, 
        bytes_transferred,
        key, 
        overlapped);

    CALF_WIN32_CHECK(bret != FALSE, PostQueuedCompletionStatus);
  }

protected:
  void create() {
    handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    CALF_WIN32_ASSERT(is_valid(), CreateIoCompletionPort);
  }
};

class io_completion_handler {
public:
  virtual void io_completed(overlapped_io_context* context) {};
  virtual void io_broken(overlapped_io_context* context, DWORD err) {};
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
      DWORD err = ERROR_SUCCESS;
      bool completed = iocp_.wait(&overlapped, &key, &bytes_transferred, &err);
      io_completion_handler* handler = reinterpret_cast<io_completion_handler*>(key);
      overlapped_io_context* context = reinterpret_cast<overlapped_io_context*>(overlapped);
      if (context != nullptr) {
        context->bytes_transferred = bytes_transferred;
      }
      if (handler != nullptr) {
        if (completed) {
          handler->io_completed(context);
        } else {
          handler->io_broken(context, err);
        }
      }
    }
  }

  void dispatch(io_completion_handler* handler, overlapped_io_context* context) {
    iocp_.notify(
        0,
        reinterpret_cast<ULONG_PTR>(handler), 
        reinterpret_cast<OVERLAPPED*>(context));
  }

  void quit() {
    quit_flag_.store(true, std::memory_order_relaxed);
    iocp_.close();
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
friend class named_pipe_server;
friend class named_pipe_client;
};

class file
  : public kernel_object,
    public io_completion_handler {
protected:
  static const int default_buffer_size = 4 * 1024;
  static const int default_timeout = 5 * 1000;

public:
  void read(
      io_context& context, 
      const io_handler& handler) {
    context.handler = handler;
    read(context);
  }

  void read(
      io_context& context) {
    CALF_ASSERT(is_valid());
    
    context.type = io_type::read;

    context.buffer.resize(default_buffer_size);
    DWORD bytes_read = 0;
    BOOL bret = ::ReadFile(
        handle_, 
        reinterpret_cast<VOID*>(context.buffer.data()),
        default_buffer_size,
        &bytes_read,
        &context.overlapped);

    if (bret != FALSE) {
      // 读取成功也由 IOCP 通知。
    } else {
      DWORD err = ::GetLastError();
      CALF_WIN32_CHECK(err == ERROR_IO_PENDING, ReadFile);
    }
  }

  void write(
      io_context& context, 
      std::uint8_t* data, 
      std::size_t bytes_to_write) {
    CALF_ASSERT(is_valid());

    context.type = io_type::write;

    DWORD bytes_written = 0;
    BOOL bret = ::WriteFile(
        handle_, 
        reinterpret_cast<LPCVOID>(data),
        static_cast<DWORD>(bytes_to_write),
        &bytes_written,
        &context.overlapped);
    
    if (bret == FALSE) {
      DWORD err = ::GetLastError();
      CALF_WIN32_CHECK(err == ERROR_IO_PENDING, WriteFile);
    }
  }

public:
  // Override io_completion_handler method.
  virtual void io_completed(overlapped_io_context* context) override {
    io_context* ioc = static_cast<io_context*>(context);
    if (ioc != nullptr) {
      switch (ioc->type) {
      case io_type::read: 
        ioc->buffer.resize(ioc->bytes_transferred);
        break;
      case io_type::write:
        break;
      case io_type::broken:
        break;
      default:
        break;
      }

      if (ioc->handler) {
        ioc->handler(*ioc);
      }
    }
  }

  virtual void io_broken(overlapped_io_context* context, DWORD err) override {
    // 出错误了。
    BOOL bret = ::CancelIo(handle_);
    CALF_CHECK(bret != FALSE);
    io_context* ioc = static_cast<io_context*>(context);
    if (ioc != nullptr) {
      ioc->type = io_type::broken;
      if (ioc->handler) {
        ioc->handler(*ioc);
      }
    }
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
  decltype(auto) packaged_dispatch(Args&&... args) {
    auto result = worker_.packaged_dispatch(std::forward<Args>(args)...);
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