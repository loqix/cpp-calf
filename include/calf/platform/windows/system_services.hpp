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
  named_pipe(
      const std::wstring& pipe_name, 
      io_mode mode, 
      io_completion_service& io_service)
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
        CALF_WIN32_CHECK(err == ERROR_IO_PENDING || err == ERROR_PIPE_CONNECTED, ConnectNamedPipe);
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

  bool is_connected() { return connected_flag_.load(std::memory_order_relaxed); }

  // Override io_completion_handler methods.
  virtual void io_completed(overlapped_io_context* context) override {
    if (context != nullptr && context->type == io_type::create) {
      context->is_pending = false;
      connected_flag_.store(true, std::memory_order_relaxed);
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
        PIPE_WAIT,  // 确定使用重叠 IO，这里需要阻塞模式，
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
        CALF_WIN32_CHECK(bret != FALSE, WaitNamedPipeW);
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

  pipe_message(std::uint8_t* data, std::size_t size) {
    message_.resize(size);
    memcpy(message_.data(), data, size);
  }

  pipe_message_head* head() { return reinterpret_cast<pipe_message_head*>(message_.data()); }
  std::uint8_t* data() { return message_.data() + sizeof(pipe_message_head); }
  io_buffer& buffer() { return message_; }

private:
  io_buffer message_;
};

class pipe_message_service {
public:
  using message_received_handler = 
      std::function<void(std::unique_ptr<pipe_message>&)>;

public:
  pipe_message_service(
      const std::wstring& pipe_name, 
      io_mode mode, 
      message_received_handler& handler) 
    : handler_(handler),
      io_worker_(io_service_),
      pipe_(pipe_name, mode, io_service_) {}

  void run() {
    io_worker_.dispatch(&pipe_message_service::connect, this);
    io_service_.run_loop();
  }

  void send_message(std::unique_ptr<pipe_message> message) {
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_queue_.emplace_back(std::move(message));
    lock.unlock();
    io_worker_.dispatch(&pipe_message_service::send, this);
  }

private:
  void connect() {
    pipe_.connect(std::ref(read_context_), std::bind(&pipe_message_service::connect_completed, this));
  }

  void connect_completed() {
    send();
    receive();
  }
  
  void receive() {
    if (read_context_.is_pending || !pipe_.is_connected()) {
      return;
    }

    pipe_.read(
        read_context_, 
        std::bind(&pipe_message_service::receive_completed, this));
  }

  void receive_completed() {
    if (read_context_.type != io_type::read) {
      closed();
      return;
    }

    std::unique_lock<std::mutex> lock(receive_mutex_, std::defer_lock);
    
    std::size_t offset = 0;
    std::size_t buffer_size = read_context_.buffer.size();
    while (buffer_size >= sizeof(pipe_message_head)) {
      pipe_message_head* head = 
          reinterpret_cast<pipe_message_head*>(read_context_.buffer.data() + offset);
      std::size_t message_size = sizeof(pipe_message_head) + head->size;
      if (buffer_size >= message_size) {
        // 消息已经接收完整。
        auto message = std::make_unique<pipe_message>(
            read_context_.buffer.data() + offset, 
            message_size);

        lock.lock();
        receive_queue_.emplace_back(std::move(message));
        lock.unlock();

        offset += message_size;
        buffer_size -= message_size;
      } else {
        // 消息不完整。
        break;
      }
    } 
    
    // 处理多余的数据
    if (offset != 0) {
      if (buffer_size > 0) {
        io_buffer new_buffer;
        new_buffer.resize(buffer_size);
        memcpy(new_buffer.data(), read_context_.buffer.data() + offset, buffer_size);
        read_context_.buffer = std::move(new_buffer);
        read_context_.offset = buffer_size;

        offset += buffer_size;
        buffer_size -= buffer_size;
      } else {
        // 正好完全接收
        read_context_.buffer.clear();
        read_context_.offset = 0;
      }
    }

    // 调用回调
    if (handler_) {
      lock.lock();
      while (!receive_queue_.empty()) {
        auto message = std::move(receive_queue_.front());
        receive_queue_.pop_front();
        lock.unlock();
        handler_(message);
        lock.lock();
      }
    }

    // 接收新消息
    receive();
  }
  
  void send() {
    if (write_context_.is_pending || !pipe_.is_connected()) {
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
    if (write_context_.type != io_type::write) {
      closed();
      return;
    }

    send();
  }

  void closed() {
    io_service_.quit();
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