#ifndef CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_
#define CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_

#include "win32.hpp"
#include "win32_debug.hpp"
#include "file_io.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <queue>

namespace calf {
namespace platform {
namespace windows {

class named_pipe
  : public file {

public:
  named_pipe(const std::wstring& pipe_name, io_mode mode)
    : connected_flag_(false) {
      switch (mode) {
      case io_mode::create:
        create(pipe_name);
        break;
      case io_mode::open:
        open(pipe_name);
        break;
      }
    }
  named_pipe(const std::wstring& pipe_name, io_mode mode, io_completion_service& io_service)
    : named_pipe(pipe_name, mode) {
    io_service.register_handle(handle_, this);
  }

  void connect(overlapped_io_context& io_context, const io_handler& handler) {
    io_context.handler = handler;

    if (connected_flag_.load(std::memory_order_relaxed)) {
      // 连接已完成
      // 通过 CreateFile 打开的管道不能调用 ConnectNamePipe。
      io_context.handler(io_context);
    } else {
      BOOL bret = ::ConnectNamedPipe(handle_, &io_context.overlapped);
      CALF_WIN32_CHECK(bret == FALSE, ConnectNamedPipe); // 异步总是返回 FALSE。
      if (bret == FALSE) {
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
        } else if (err == ERROR_PIPE_CONNECTED) {
          connected_flag_.store(true, std::memory_order_relaxed);
          // 连接已经完成了，这里在当前线程直接调用回调。
          // TODO: 考虑也移入 IO 线程调用。
          io_context.handler(io_context);
        }
      }
    }
  }

  // Override io_completion_handler methods.
  virtual void io_broken(overlapped_io_context* context, DWORD err) override {
    file::io_broken(context, err);
    // 管道错误，关闭管道
    CALF_WIN32_CHECK(err == ERROR_BROKEN_PIPE, GetQueuedCompletionStatus);
  }

protected:
  void create(const std::wstring& pipe_name) {
    handle_ = ::CreateNamedPipeW(
        pipe_name.c_str(), 
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
        PIPE_NOWAIT, 
        PIPE_UNLIMITED_INSTANCES, // 允许创建多重管道实例
        default_buffer_size,
        default_buffer_size,
        default_timeout,
        NULL);
    CALF_WIN32_CHECK(is_valid(), CreateNamedPipeW);
  }

  void open(const std::wstring& pipe_name, bool wait = true) {
    handle_ = ::CreateFileW(
        pipe_name.c_str(), 
        GENERIC_READ | GENERIC_WRITE, 
        0, 
        NULL, 
        OPEN_EXISTING, 
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION |
        FILE_FLAG_OVERLAPPED, 
        NULL);
    // 增加管道忙时等待
    if (wait && handle_ == INVALID_HANDLE_VALUE) {
      DWORD err = ::GetLastError();
      if (err == ERROR_PIPE_BUSY) {
        BOOL bret = ::WaitNamedPipeW(pipe_name.c_str(), default_timeout);
        if (bret != FALSE) {
          open(pipe_name, false);
          return;
        }
      }
    }

    CALF_WIN32_CHECK(handle_ != INVALID_HANDLE_VALUE, CreateFileW);
    // 连接者不需要等待
    if (is_valid()) {
      connected_flag_.store(true, std::memory_order_relaxed);
    }
  }

protected:
  std::atomic_bool connected_flag_;
};

struct pipe_message_head {
  std::uint32_t id;
  std::uint32_t size;
};

class pipe_message {
public:
  pipe_message(int id, std::size_t size) {
    message_.resize(sizeof(pipe_message_head) + size);
    pipe_message_head* head = reinterpret_cast<pipe_message_head*>(message_.data());
    head->id = static_cast<std::uint32_t>(id);
    head->size = static_cast<std::uint32_t>(message_.size());
  }

  std::uint8_t* data() { return message_.data() + sizeof(pipe_message_head); }

private:
  std::vector<std::uint8_t> message_;
};

class pipe_message_service {
public:
  void send(pipe_message* message) {

  }

private:
  std::deque<pipe_message*> send_queue_;
  std::deque<pipe_message*> receive_queue_;
  io_context read_context_;
  io_context write_context_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_