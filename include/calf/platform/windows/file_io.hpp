#ifndef CALF_PLATFORM_WINDOWS_FILE_IO_HPP_
#define CALF_PLATFORM_WINDOWS_FILE_IO_HPP_

#include "win32.hpp"
#include "debugging.hpp"
#include "kernel_object.hpp"
#include "../../worker_service.hpp"

#include <cstdint>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iostream>
#include <functional>
#include <vector>
#include <list>
#include <thread>
#include <codecvt>

namespace calf {
namespace platform {
namespace windows {

struct overlapped_io_context;

using io_handler = std::function<void(overlapped_io_context&)>;
using io_buffer = std::vector<std::uint8_t>;

enum struct io_type {
  unknown,
  create,
  open,
  write, 
  read,
  close,
  broken
};

enum struct io_mode {
  open,
  create,
  create_multiple_instance
};

struct overlapped_io_context {
  overlapped_io_context() 
    : bytes_transferred(0),
      is_pending(false),
      type(io_type::unknown) {
    // 重叠结构使用前必须主动置零。
    memset(&overlapped, 0, sizeof(overlapped));
  }

  // 特殊尾随数据，必须保证 OVERLAPPED 结构体在内存最前。
  OVERLAPPED overlapped;
  std::size_t bytes_transferred;
  io_handler handler;
  bool is_pending;
  io_type type;
};

struct io_context
  : public overlapped_io_context {
  io_buffer buffer;
  std::size_t offset;
};

class io_completion_port
  : public kernel_object {
public:
  io_completion_port() { create(); }

  void associate(HANDLE file, ULONG_PTR key) {
    CALF_WIN32_ASSERT(is_valid());

    HANDLE port = ::CreateIoCompletionPort(
        file, 
        handle_, 
        key,
        0);
    CALF_WIN32_API_CHECK(port == handle_, CreateIoCompletionPort);
  }

  bool wait(
      LPOVERLAPPED* overlapped,
      PULONG_PTR key,
      LPDWORD bytes_transferred,
      DWORD* err,
      DWORD timeout = INFINITE) {
    CALF_WIN32_ASSERT(is_valid());

    BOOL bret = ::GetQueuedCompletionStatus(
        handle_,
        bytes_transferred, 
        key, 
        overlapped,
        timeout);

    //CALF_WIN32_API_CHECK(bret != FALSE, GetQueuedCompletionStatus);
    if (bret == FALSE) {
      *err = ::GetLastError();
    }
    return bret != FALSE;
  }

  void notify(
      DWORD bytes_transferred,
      ULONG_PTR key, 
      LPOVERLAPPED overlapped) {
    CALF_WIN32_ASSERT(is_valid());

    BOOL bret = ::PostQueuedCompletionStatus(
        handle_, 
        bytes_transferred,
        key, 
        overlapped);

    CALF_WIN32_API_CHECK(bret != FALSE, PostQueuedCompletionStatus);
  }

protected:
  void create() {
    handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    CALF_WIN32_API_ASSERT(is_valid(), CreateIoCompletionPort);
  }
};

class io_completion_handler {
public:
  virtual void io_completed(overlapped_io_context* context) {}
  virtual void io_broken(overlapped_io_context* context, DWORD err) {}
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

friend class system_pipe;
friend class socket;
};

class file
  : public kernel_object,
    protected io_completion_handler {
public:
  static const std::size_t default_buffer_size = 4 * 1024;
  static const std::size_t max_buffer_size = 128 * 1024 * 1024;
  static const int default_timeout = 5 * 1000;

public:
  file() {}

  file(const std::wstring& file_path) {
    create(file_path);
  }

  // 托管了文件句柄对象生命周期，所以禁止拷贝构造。
  file(const file& other) = delete;

  // 提供移动语义。
  file(file&& other) {
    handle_ = other.handle_;
    other.handle_ = NULL;
  }

  void read(
      io_context& context,
      const io_handler& handler) {
    context.handler = handler;
    read(context);
  }

  void read(io_context& context) {
    CALF_WIN32_ASSERT(is_valid());
    
    context.type = io_type::read;

    context.offset = context.buffer.size();
    context.buffer.resize(context.offset + default_buffer_size);
    DWORD bytes_read = 0;
    BOOL bret = ::ReadFile(
        handle_, 
        reinterpret_cast<VOID*>(context.buffer.data() + context.offset),
        default_buffer_size,
        &bytes_read,
        &context.overlapped);

    if (bret != FALSE) {
      // 读取成功也由 IOCP 通知。
    } else {
      DWORD err = ::GetLastError();
      CALF_WIN32_API_CHECK(err == ERROR_IO_PENDING, ReadFile);
      if (err = ERROR_IO_PENDING) {
        context.is_pending = true;
      }
    }
  }

  void write(
      io_context& context,
      const io_handler& handler) {
    context.handler = handler;
    write(context);
  }

  void write(
      io_context& context, 
      std::uint8_t* data, 
      std::size_t size) {
    context.buffer.resize(size);
    memcpy(context.buffer.data(), data, size);
    write(context);
  }

  void write(io_context& context) {
    CALF_WIN32_ASSERT(is_valid());

    context.type = io_type::write;

    DWORD bytes_written = 0;
    BOOL bret = ::WriteFile(
        handle_, 
        reinterpret_cast<LPCVOID>(context.buffer.data()),
        static_cast<DWORD>(context.buffer.size()),
        &bytes_written,
        &context.overlapped);
    
    if (bret == FALSE) {
      DWORD err = ::GetLastError();
      CALF_WIN32_API_CHECK(err == ERROR_IO_PENDING, WriteFile);
      if (err == ERROR_IO_PENDING) {
        context.is_pending = true;
      }
    }
  }

  // Override io_completion_handler method.
  virtual void io_completed(overlapped_io_context* context) override {
    io_context* ioc = static_cast<io_context*>(context);
    if (ioc != nullptr) {
      switch (ioc->type) {
      case io_type::read: 
        ioc->buffer.resize(ioc->offset + ioc->bytes_transferred);
        ioc->offset = ioc->buffer.size();
        ioc->is_pending = false;
        break;
      case io_type::write:
        ioc->is_pending = false;
        break;
      case io_type::broken:
        ioc->is_pending = false;
        ioc->buffer.clear();
        ioc->offset = 0;
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
    CALF_WIN32_CHECK(bret != FALSE);
    io_context* ioc = static_cast<io_context*>(context);
    if (ioc != nullptr) {
      ioc->type = io_type::broken;
      ioc->buffer.clear();
      ioc->offset = 0;
      if (ioc->handler) {
        ioc->handler(*ioc);
      }
    }
  }

private:
  void create(const std::wstring& file_path) {
    handle_ = ::CreateFileW(
        file_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,  // 不共享
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    CALF_WIN32_API_CHECK(is_valid(), CreateFileW);
  }
};

class io_completion_worker
  : protected io_completion_handler {
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

class file_channel {
public:
  using file_handler =
      std::function<void(file_channel& channel)>;

public:
  file_channel(
      const std::wstring& file_path,
      io_completion_worker& io_worker,
      const file_handler& handler) 
    : io_worker_(io_worker),
      file_(file_path), 
      handler_(handler) {}

  void write(const std::uint8_t* data, std::size_t size) {
    std::unique_lock<std::mutex> lock(write_mutex_);

    std::size_t offset = write_buffer_.size();
    CALF_WIN32_CHECK(offset + size < file::max_buffer_size);

    write_buffer_.resize(offset + size);
    memcpy(write_buffer_.data() + offset, data, size);
    lock.unlock();

    write();
  }

  void write(const std::string& data) {
    write(reinterpret_cast<const std::uint8_t*>(data.c_str()), data.size());
  }

private: 
  void write() {
    if (write_context_.is_pending) {
      return;
    }

    std::unique_lock<std::mutex> lock(write_mutex_);
    if (write_buffer_.empty()) {
      return;
    }

    write_context_.buffer.swap(write_buffer_);
    write_buffer_.clear();
    lock.unlock();

    file_.write(write_context_, std::bind(&file_channel::write_completed, this));
  }

  void write_completed() {
    if (write_context_.type != io_type::write) {
      closed();
      return;
    }

    write();
  }

  void closed() {
    if (handler_) {
      handler_(*this);
    }
  }

private:
  io_completion_worker& io_worker_;
  file file_;
  io_context read_context_;
  io_context write_context_;
  io_buffer read_buffer_;
  io_buffer write_buffer_;
  std::mutex read_mutex_;
  std::mutex write_mutex_;
  file_handler handler_;
};

class file_io_service {
public:
  file_io_service()
    : io_worker_(io_service_) {}

  void run() {
    io_service_.run_loop();
  }

  file_channel& create_file(const std::wstring& file_path, const file_channel::file_handler& handler = nullptr) {
    std::unique_lock<std::mutex> lock(channels_mutex_);
    channels_.emplace_back(file_path, io_worker_, handler);
    return channels_.back();
  }
  
private:
  io_completion_service io_service_;
  io_completion_worker io_worker_;
  std::list<file_channel> channels_;
  std::mutex channels_mutex_;
};

namespace logging {

class log_file_target 
  : public log_target {
public:
  log_file_target(const std::wstring& file_name) 
    : thread_(&file_io_service::run, &service_) {
    channel_ = &(service_.create_file(file_name));
  }

  void output(const std::wstring& data) override {
    if (channel_ != nullptr) {
      channel_->write(convert_.to_bytes(data));
    }
  }

  void sync() override {

  }

private:
  file_channel* channel_;
  file_io_service service_;
  std::thread thread_;
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert_;
};

} // namespace logging

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_ASYNC_IO_HPP_