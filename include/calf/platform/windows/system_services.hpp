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
      io_context.type = io_type::create;
      BOOL bret = ::ConnectNamedPipe(handle_, &io_context.overlapped);
      CALF_WIN32_CHECK(bret == FALSE, ConnectNamedPipe); // 异步总是返回 FALSE。
      if (bret == FALSE) {
        DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
          io_context.is_pending = true;
        } else if (err == ERROR_PIPE_CONNECTED) {
          connected_flag_.store(true, std::memory_order_relaxed);
          // 连接已经完成了，这里在当前线程直接调用回调。
          io_context.handler(io_context);
        }
      }
    }
  }

  // Override io_completion_handler methods.
  virtual void io_completed(overlapped_io_context* context) override {
    if (context != nullptr && context->type == io_type::create) {
      context->is_pending = false;
    }
    file::io_completed(context);
  }

  virtual void io_broken(overlapped_io_context* context, DWORD err) override {
    // 管道错误，关闭管道
    CALF_WIN32_CHECK(err == ERROR_BROKEN_PIPE, GetQueuedCompletionStatus);

    file::io_broken(context, err);
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
    head->size = static_cast<std::uint32_t>(size);
  }

  std::uint8_t* data() { return message_.data() + sizeof(pipe_message_head); }
  std::size_t size() { 
    pipe_message_head* head = reinterpret_cast<pipe_message_head*>(message_.data());
    return static_cast<std::size_t>(head->size);
  }
  io_buffer& buffer() { return message_; }

private:
  io_buffer message_;
};

class pipe_message_service {
public:
  using message_received_handler = std::function<void(pipe_message*)>;

public:
  pipe_message_service(
      const std::wstring& pipe_name, 
      io_mode mode, 
      message_received_handler& handler) 
    : handler_(handler),
      io_worker_(io_service_),
      pipe_(pipe_name, mode, io_service_) {}

  void run() {
    io_worker_.dispatch(
        &named_pipe::connect, 
        &pipe_, 
        std::ref(read_context_), 
        std::bind(&pipe_message_service::receive, this));

    io_service_.run_loop();
  }

  void send_message(std::unique_ptr<pipe_message> message) {
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_queue_.emplace_back(std::move(message));
    lock.unlock();
    io_worker_.dispatch(&pipe_message_service::send, this);
  }

private:
  void receive() {
    if (read_context_.is_pending) {
      return;
    }

    pipe_.read(
        read_context_, 
        std::bind(&pipe_message_service::receive_completed, this));
  }

  void receive_completed() {
    std::size_t buffer_size = read_context_.buffer.size();
    if (buffer_size >= sizeof(pipe_message_head)) {
      pipe_message_head* head = reinterpret_cast<pipe_message_head*>(read_context_.buffer.data());
    }

    receive();
  }
  
  void send() {
    if (write_context_.is_pending) {
      return;
    }

    std::unique_lock<std::mutex> lock(send_mutex_);
    if (!send_queue_.empty()) {
      std::unique_ptr<pipe_message> message = std::move(send_queue_.front());
      send_queue_.pop_front();
      lock.unlock();

      write_context_.buffer = std::move(message->buffer());
      pipe_.write( 
          write_context_, 
          std::bind(&pipe_message_service::send_completed, this));
    }
  }

  void send_completed() {
    send();
  }

private:
  calf::io_completion_service io_service_;
  calf::io_completion_worker io_worker_;
  calf::named_pipe pipe_;
  std::deque<std::unique_ptr<pipe_message>> send_queue_;
  std::mutex send_mutex_;
  std::deque<std::unique_ptr<pipe_message>> receive_queue_;
  std::mutex receive_mutex_;
  io_context read_context_;
  io_context write_context_;
  message_received_handler handler_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_