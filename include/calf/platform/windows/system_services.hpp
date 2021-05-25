#ifndef CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_
#define CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_

#include "win32.hpp"
#include "debugging.hpp"
#include "file_io.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <queue>
#include <list>

namespace calf {
namespace platform {
namespace windows {

class waitable_object : public kernel_object {
public:
  bool wait(DWORD timeout = INFINITE) {
    DWORD result = ::WaitForSingleObject(handle_, timeout);
    CALF_WIN32_API_CHECK(result == WAIT_OBJECT_0, WaitForSingleObject);
    return result == WAIT_OBJECT_0;
  }
};

class system_event : public waitable_object {
public:
  system_event(const std::wstring& name, io_mode mode = io_mode::create, bool auto_reset = false) { 
    switch(mode) {
    case io_mode::create:
      create(name, auto_reset);
      break;
    case io_mode::open:
      open(name);
      break;
    }
  }

  void set() {
    CALF_WIN32_CHECK(is_valid());
    ::SetEvent(handle_);
  }

  void reset() {
    CALF_WIN32_CHECK(is_valid());
    ::ResetEvent(handle_);
  }

private:
  void create(const std::wstring& name, bool auto_reset = false) {
    handle_ = ::CreateEventW(NULL, auto_reset ? TRUE : FALSE, FALSE, name.c_str());
    CALF_WIN32_API_CHECK(handle_ != NULL, CreateEventW);
  }

  void open(const std::wstring& name) {
    handle_ = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    CALF_WIN32_API_CHECK(is_valid(), OpenEventW);
  }
};

class system_pipe
  : public file {

public:
  system_pipe(const std::wstring& pipe_name, io_mode mode)
    : connected_flag_(ATOMIC_VAR_INIT(false)) {
    switch (mode) {
    case io_mode::create:
      create(pipe_name);
      break;
    case io_mode::create_multiple_instance:
      create(pipe_name, false);
      break;
    case io_mode::open:
      open(pipe_name);
      break;
    }
  }
  system_pipe(
      const std::wstring& pipe_name, 
      io_mode mode, 
      io_completion_service& io_service)
    : system_pipe(pipe_name, mode) {
    io_service.register_handle(handle_, this);
  }

  void connect(overlapped_io_context& io_context, const io_handler& handler) {
    io_context.handler = handler;
    io_context.type = io_type::create;

    if (connected_flag_.load(std::memory_order_relaxed)) {
      // 连接已完成
      // 通过 CreateFile 打开的管道不能调用 ConnectNamePipe。
      io_context.handler(io_context);
    } else {
      BOOL bret = ::ConnectNamedPipe(handle_, &io_context.overlapped);
      CALF_WIN32_CHECK(bret == FALSE); // 异步总是返回 FALSE。
      if (bret == FALSE) {
        DWORD err = ::GetLastError();
        CALF_WIN32_API_CHECK(err == ERROR_IO_PENDING || err == ERROR_PIPE_CONNECTED, ConnectNamedPipe);
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
    CALF_WIN32_API_CHECK(err == ERROR_BROKEN_PIPE, GetQueuedCompletionStatus);

    file::io_broken(context, err);
  }

protected:
  void create(const std::wstring& pipe_name, bool first_instance = true) {
    handle_ = ::CreateNamedPipeW(
        pipe_name.c_str(), 
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        (first_instance ? FILE_FLAG_FIRST_PIPE_INSTANCE : NULL),
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
        PIPE_WAIT,  // 确定使用重叠 IO，这里需要阻塞模式，
        PIPE_UNLIMITED_INSTANCES, // 允许创建多重管道实例
        default_buffer_size,
        default_buffer_size,
        default_timeout,
        NULL);
    CALF_WIN32_API_CHECK(is_valid(), CreateNamedPipeW);
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
        CALF_WIN32_API_CHECK(bret != FALSE, WaitNamedPipeW);
        if (bret != FALSE) {
          open(pipe_name, false);
          return;
        }
      }
    }

    CALF_WIN32_API_CHECK(handle_ != INVALID_HANDLE_VALUE, CreateFileW);
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
  pipe_message(const std::uint8_t* mem_data, std::size_t size) {
    message_.resize(size);
    memcpy(message_.data(), mem_data, size);
  }

  pipe_message(int id, std::size_t size) {
    message_.resize(sizeof(pipe_message_head) + size);
    pipe_message_head* head = reinterpret_cast<pipe_message_head*>(message_.data());
    head->id = static_cast<std::uint32_t>(id);
    head->size = static_cast<std::uint32_t>(size);
  }

  pipe_message(int id, const std::uint8_t* mem_data, std::size_t size) 
    : pipe_message(id, size) {
    memcpy(data(), mem_data, size);
  }

  pipe_message(int id, std::string& str_data) 
    : pipe_message(
          id, 
          reinterpret_cast<const std::uint8_t*>(str_data.c_str()), 
          str_data.length()) {}

  pipe_message_head* head() { return reinterpret_cast<pipe_message_head*>(message_.data()); }
  std::uint8_t* data() { return message_.data() + sizeof(pipe_message_head); }
  io_buffer& buffer() { return message_; }
  std::string to_string() { return std::string(reinterpret_cast<char*>(data()), head()->size); }
  std::size_t size() { return head()->size; }
  int id() { return head()->id; }

private:
  io_buffer message_;
};

class pipe_message_channel {
public:
  using message_handler = 
      std::function<void(
          pipe_message_channel& channel)>;

public:
  pipe_message_channel(
      const std::wstring& pipe_name, 
      io_mode mode, 
      io_completion_service& io_service,
      io_completion_worker& io_worker,
      message_handler handler) 
    : io_worker_(io_worker),
      pipe_(pipe_name, mode, io_service),
      handler_(handler) {}
  pipe_message_channel(const pipe_message_channel&) = delete;
    
  void send_message(std::unique_ptr<pipe_message> message) {
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_queue_.emplace_back(std::move(message));
    lock.unlock();
    io_worker_.dispatch(&pipe_message_channel::send, this);
  }

  std::unique_ptr<pipe_message> receive_message() {
    std::unique_lock<std::mutex> lock(receive_mutex_);
    if (!receive_queue_.empty()) {
      auto message = std::move(receive_queue_.front());
      receive_queue_.pop_front();
      return std::move(message);
    }
    return std::unique_ptr<pipe_message>();
  }

  io_type type() const { return read_context_.type; }

  bool is_valid() const { return pipe_.is_valid(); }

private:
  void connect() {
    pipe_.connect(std::ref(read_context_), std::bind(&pipe_message_channel::connect_completed, this));
  }

  void connect_completed() {
    if (handler_) {
      handler_(*this);
    }
    send();
    receive();
  }
  
  void receive() {
    if (read_context_.is_pending || !pipe_.is_connected()) {
      return;
    }

    pipe_.read(
        read_context_, 
        std::bind(&pipe_message_channel::receive_completed, this));
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
      handler_(*this);
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
          std::bind(&pipe_message_channel::send_completed, this));
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
    if (handler_) {
      handler_(*this);
    }
  }

private:
  io_completion_worker& io_worker_;

  system_pipe pipe_;
  std::deque<std::unique_ptr<pipe_message>> send_queue_;
  std::mutex send_mutex_;
  std::deque<std::unique_ptr<pipe_message>> receive_queue_;
  std::mutex receive_mutex_;
  io_context read_context_;
  io_context write_context_;
  
  // callback handler;
  message_handler handler_;

friend class pipe_message_service;
};

class pipe_message_service {
public:
  pipe_message_service(
      const std::wstring& pipe_name, 
      io_mode mode) 
    : pipe_name_(pipe_name),
      mode_(mode),
      io_worker_(io_service_) {}
  pipe_message_service(const pipe_message_service&) = delete;

  void run() {
    io_service_.run_loop();
    CALF_LOG(info) << "Pipe message service quit.";
  }

  pipe_message_channel& create_channel(
      pipe_message_channel::message_handler handler, bool first_instance = true) {
    std::unique_lock<std::mutex> lock(channels_mutex_);
    channels_.emplace_back(
        pipe_name_, 
        first_instance ? mode_ : io_mode::create_multiple_instance, 
        io_service_, 
        io_worker_, 
        std::bind(
            &pipe_message_service::message_handler, 
            this, 
            handler, 
            std::placeholders::_1));
    auto& channel = channels_.back();
    io_worker_.dispatch(&pipe_message_channel::connect, &channel);
    return channel;
  }

  void close_channel(pipe_message_channel& channel) {
    std::unique_lock<std::mutex> lock(channels_mutex_);
    channels_.remove_if([&channel](const pipe_message_channel& object) {
      return &object == &channel;
    });
  }

  void quit() {
    io_service_.quit();
  }

private:
  void message_handler(
      pipe_message_channel::message_handler handler,
      pipe_message_channel& channel) {
    if (handler) {
      handler(channel);
    }

    if (channel.type() == io_type::broken ||
        channel.type() == io_type::close) {
      // 管道关闭了。
      io_worker_.dispatch(&pipe_message_service::close_channel, this, std::ref(channel));
    } else if (channel.type() == io_type::create && mode_ == io_mode::create) {
      // 服务端建立连接后，准备一个新的连接。
      create_channel(handler, false);
    }
  }

private:
  std::wstring pipe_name_;
  io_mode mode_;
  io_completion_service io_service_;
  io_completion_worker io_worker_;
  std::list<pipe_message_channel> channels_;
  std::mutex channels_mutex_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_SYSTEM_SERVICES_HPP_