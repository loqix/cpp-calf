#ifndef CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_
#define CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_

#include "win32.hpp"
#include "win32_debug.hpp"
#include "file_io.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace calf {
namespace platform {
namespace windows {

class named_pipe
  : public file {

public:
  named_pipe(const std::wstring& pipe_name)
    : connected_flag_(false) { create(pipe_name); }
  named_pipe(const std::wstring& pipe_name, io_completion_service& io_service)
    : named_pipe(pipe_name) {
    io_service.register_handle(handle_, this);
  }

  void write(overlapped_io_context& io_context, std::uint8_t data, std::size_t bytes_to_write) {
    win32_assert(is_valid());

    DWORD bytes_written = 0;
    BOOL bret = ::WriteFile(
        handle_, 
        reinterpret_cast<LPCVOID>(data),
        static_cast<DWORD>(bytes_to_write),
        &bytes_written,
        &io_context.overlapped);
    // 异步写入总是返回 FALSE。
    win32_assert(bret == FALSE);
    
    if (bret != FALSE) {
      
    }
  }

  bool connect(overlapped_io_context& io_context, const io_handler& handler) {
    io_context.handler = handler;
    BOOL bret = ::ConnectNamedPipe(handle_, &io_context.overlapped);
    if (bret == FALSE) {
      DWORD err = ::GetLastError();
      if (err == ERROR_IO_PENDING) {
        return false;
      } else if (err == ERROR_PIPE_CONNECTED) {
        connected_flag_.store(true, std::memory_order_relaxed);
        // 连接已经完成了，这里在当前线程直接调用回调。
        // TODO: 考虑也移入 IO 线程调用。
        handler();
        return true;
      }
    }

    return false;
  }

protected:
  void create(const std::wstring& pipe_name) {
    handle_ = ::CreateNamedPipeW(
        pipe_name.c_str(), 
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
        1, 
        default_buffer_size,
        default_buffer_size,
        default_timeout,
        NULL);

    // 如果管道已经存在，尝试打开。
    if (handle_ == INVALID_HANDLE_VALUE) {
      handle_ = ::CreateFileW(
          pipe_name.c_str(), 
          GENERIC_READ | GENERIC_WRITE, 
          0, 
          NULL, 
          OPEN_EXISTING, 
          SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION |
          FILE_FLAG_OVERLAPPED, 
          NULL);
      win32_assert(handle_ != INVALID_HANDLE_VALUE);

      // 连接者不需要等待
      connected_flag_.store(true, std::memory_order_relaxed);
    }
  }


  // Override io_completion_handler method.
  virtual void io_completed(overlapped_io_context* context) override {
    context->handler();
  }

protected:
  std::atomic_bool connected_flag_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_