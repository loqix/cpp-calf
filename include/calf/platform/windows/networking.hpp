#ifndef CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
#define CALF_PLATFORM_WINDOWS_NETWORKING_HPP_

#include "win32_debug.hpp"
#include "file_io.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <mutex>
#include <list>
#include <functional>
#include <string>
#include <iostream>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define CALF_WSA_CHECK(result, func) calf::platform::windows::wsa_check(result, #func, __FILE__, __LINE__)
#define CALF_WSA_ASSERT(result, func) calf::platform::windows::wsa_check(result, #func, __FILE__, __LINE__)

namespace calf {
namespace platform {
namespace windows {

win32_log& wsa_check(bool expr, const char* func, const char* file, int line) {
  if (!expr) {
    int err = ::WSAGetLastError(); 
    std::string error_format = get_error_format(err);
    std::cerr << "[" << file << "(" << line << ")] " <<
        func << " failed with error " << err << ": " << error_format << std::endl;
    std::cerr << get_trace_stack() << std::endl;

    // 抛出 Windows 结构化异常
    ::RaiseException(STATUS_ASSERTION_FAILURE, 0, NULL, NULL);
  }

  return global_win32_log;
}

class winsock {
public:
  winsock() 
    : has_init_(false) { startup(); }
  ~winsock() { cleanup(); }

public:
  

private:
  void startup() {
    int result = ::WSAStartup(MAKEWORD(2, 2), &wsa_data_);
    CALF_CHECK(result == 0) << "WSAStartup failed: " << result;
    has_init_ = result == 0;
  }

  void cleanup() {
    if (has_init_) {
      has_init_ = false;
      ::WSACleanup();
    }
  }

private:
  WSADATA wsa_data_;
  bool has_init_;
};

struct socket_context
  : public io_context {
  socket_context() : io_context() {
    memset(&wsabuf, 0, sizeof(wsabuf));
  }

  WSABUF wsabuf;
};

class socket
  : public io_completion_handler {
public:
  static const std::size_t default_buffer_size = 4 * 1024;
  static const std::size_t max_buffer_size = 128 * 1024 * 1024;

public:
  socket(io_completion_service& io_service)
    : socket_(INVALID_SOCKET) {
    create();  
    if (is_valid()) {
      io_service.register_handle(reinterpret_cast<HANDLE>(socket_), this);
    }
  }

  void bind(const std::string& ip_addr, std::uint16_t port) {
    CALF_CHECK(is_valid());
    if (!is_valid()) {
      return;
    }

    SOCKADDR_IN local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = ::inet_addr(ip_addr.c_str());
    local_addr.sin_port = ::htons(port);

    int result = ::bind(
        socket_, 
        reinterpret_cast<SOCKADDR*>(&local_addr), 
        sizeof(local_addr));
    CALF_WSA_CHECK(result != SOCKET_ERROR, bind);
  }

  void listen() {
    CALF_CHECK(is_valid());
    if (!is_valid()) {
      return;
    }

    int result = ::listen(socket_, SOMAXCONN);
    CALF_WSA_CHECK(result != SOCKET_ERROR, listen);
  }

  void accept(socket_context& context, socket& accept_socket, const io_handler& handler) {
    context.handler = handler;
    accept(context, accept_socket);
  }
  
  void accept(socket_context& context, socket& accept_socket) {
    LPFN_ACCEPTEX accept_ex = get_accept_ex();
    if (accept_ex != nullptr) {
      context.type = io_type::create;
      context.buffer.resize(default_buffer_size);
      DWORD bytes_received = 0;
      BOOL result = accept_ex(
          socket_, 
          accept_socket.socket_,
          context.buffer.data(),
          context.buffer.size() - ((sizeof(SOCKADDR_IN) + 16) * 2),
          sizeof(SOCKADDR_IN) + 16,
          sizeof(SOCKADDR_IN) + 16,
          &bytes_received,
          &context.overlapped);
      CALF_ASSERT(result == FALSE);
      int err = ::WSAGetLastError();
      CALF_WSA_CHECK(err == WSA_IO_PENDING, AcceptEx);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      }
    }
  }

  void connect(
      const std::string& ip_addr, 
      std::uint16_t port, 
      socket_context& context, 
      const io_handler& handler) {
    context.handler = handler;
    connect(ip_addr, port, context);
  }

  void connect(
      const std::string& ip_addr, 
      std::uint16_t port, 
      socket_context& context) {
    SOCKADDR_IN remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = ::inet_addr(ip_addr.c_str());
    remote_addr.sin_port = ::htons(port);

    LPFN_CONNECTEX connect_ex = get_connect_ex();
    if (connect_ex != nullptr) {
      context.type = io_type::create;
      context.buffer.resize(default_buffer_size);
      DWORD bytes_sent = 0;
      BOOL result = connect_ex(
        socket_,
        reinterpret_cast<SOCKADDR*>(&remote_addr),
        sizeof(remote_addr),
        context.buffer.data(),
        context.buffer.size(),
        &bytes_sent,
        &context.overlapped);
      CALF_ASSERT(result == FALSE);
      int err = ::WSAGetLastError();
      CALF_WSA_CHECK(err == WSA_IO_PENDING, ConnectEx);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      }
    }
  }

  void send(socket_context& context, const io_handler& handler) {
    context.handler = handler;
    send(context);
  }

  void send(socket_context& context) {
    context.type = io_type::write;
    context.wsabuf.buf = reinterpret_cast<CHAR*>(context.buffer.data());
    context.wsabuf.len = context.buffer.size();

    DWORD bytes_sent = 0;
    int result = ::WSASend(
        socket_,
        &context.wsabuf,
        1,
        &bytes_sent,
        0,
        &context.overlapped,
        NULL);
    if (result == FALSE) {
      int err = ::WSAGetLastError();
      //CALF_WSA_CHECK(err == WSA_IO_PENDING || err == 0, WSASend);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      }
    } else {
      context.type = io_type::broken;
      if (context.handler) {
        context.handler(context);
      }
    }
  }

  void recv(socket_context& context, const io_handler& handler) {
    context.handler = handler;
    recv(context);
  }

  void recv(socket_context& context) {
    context.type = io_type::read;
    context.buffer.resize(default_buffer_size);
    context.wsabuf.buf = reinterpret_cast<CHAR*>(context.buffer.data());
    context.wsabuf.len = context.buffer.size();

    DWORD bytes_received = 0;
    DWORD flags = 0;
    int result = ::WSARecv(
        socket_,
        &context.wsabuf,
        1,
        &bytes_received,
        &flags,
        &context.overlapped,
        NULL);
    if (result == FALSE) {
      int err = ::WSAGetLastError();
      //CALF_WSA_CHECK(err == WSA_IO_PENDING || err == 0, WSARecv);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      }
    } else {
      context.type = io_type::broken;
      if (context.handler) {
        context.handler(context);
      }
    }
  }

  bool is_valid() { return socket_ != INVALID_SOCKET; }

private:
  // Override class calf::io_completion_handler method.
  virtual void io_completed(overlapped_io_context* context) override {
    auto sc = reinterpret_cast<socket_context*>(context);
    if (sc != nullptr) {
      switch(sc->type) {
      case io_type::create:
        sc->is_pending = false;
        break;
      case io_type::write:
        sc->is_pending = false;
        sc->buffer.resize(sc->bytes_transferred);
      case io_type::read:
        sc->is_pending = false;
        sc->buffer.resize(sc->bytes_transferred);
      }

      if (sc->handler) {
        sc->handler(*sc);
      }
    }
  }

  void io_broken(overlapped_io_context* context, DWORD err) override {
    auto sc = reinterpret_cast<socket_context*>(context);
    if (sc != nullptr) {
      sc->type = io_type::broken;
      sc->is_pending = false;
      if (sc->handler) {
        sc->handler(*sc);
      }
    }
  }

  void create() {
    socket_ = ::WSASocketW(
        AF_INET,      // IPv4
        SOCK_STREAM,  // TCP
        0,
        NULL,
        0,
        WSA_FLAG_OVERLAPPED);
    CALF_WSA_CHECK(is_valid(), WSASocketW);
  }

  void close() {
    if (is_valid()) {
      int result = ::closesocket(socket_);
      CALF_WSA_CHECK(result, closesocket);
      socket_ = INVALID_SOCKET;
    }
  }

  LPFN_ACCEPTEX get_accept_ex() {
    static LPFN_ACCEPTEX accept_ex = nullptr;
    if (accept_ex == nullptr) {
      GUID accept_ex_guid = WSAID_ACCEPTEX;
      DWORD bytes = 0;
      int result = ::WSAIoctl(
          socket_, 
          SIO_GET_EXTENSION_FUNCTION_POINTER,
          &accept_ex_guid, 
          sizeof(accept_ex_guid),
          &accept_ex,
          sizeof(accept_ex),
          &bytes,
          NULL,
          NULL);
      CALF_WSA_CHECK(result != SOCKET_ERROR, WSAIoctl);
    }
    return accept_ex;
  }

  LPFN_CONNECTEX get_connect_ex() {
    static LPFN_CONNECTEX connect_ex = nullptr;
    if (connect_ex == nullptr) {
      GUID connect_ex_guid = WSAID_CONNECTEX;
      DWORD bytes = 0;
      int result = ::WSAIoctl(
          socket_, 
          SIO_GET_EXTENSION_FUNCTION_POINTER,
          &connect_ex_guid, 
          sizeof(connect_ex_guid),
          &connect_ex,
          sizeof(connect_ex),
          &bytes,
          NULL,
          NULL);
      CALF_WSA_CHECK(result != SOCKET_ERROR, WSAIoctl);
    }
    return connect_ex;
  }

private:
  SOCKET socket_;
};

class socket_channel {
public:
  using socket_handler = std::function<void(socket_channel&)>;
  using socket_buffer = std::vector<std::uint8_t>;

public:
  socket_channel(io_completion_service& io_service, const socket_handler& handler)
    : socket_(io_service),
      handler_(handler),
      connected_flag_(false) {}

  void connect(const std::string& ip_addr, std::uint16_t port) {
    socket_.bind("0.0.0.0", 0);
    socket_.connect(ip_addr, port, recv_context_, std::bind(&socket_channel::connect_completed, this));
  }

  void listen(const std::string& ip_addr, std::uint16_t port) {
    socket_.bind(ip_addr, port);
    socket_.listen();
  }

  void accept(socket_channel& channel) {
    socket_.accept(recv_context_, channel.socket_, std::bind(&socket_channel::accept_completed, this, std::ref(channel)));
  }

  void send_buffer(std::uint8_t* data, std::size_t size) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    std::size_t offset = send_buffer_.size();
    CALF_CHECK(offset + size < socket::max_buffer_size);

    send_buffer_.resize(offset + size);
    memcpy(send_buffer_.data() + offset, data, size);
    lock.unlock();

    send();
  }

  void send_buffer(socket_buffer& buffer) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    std::size_t offset = send_buffer_.size();
    if (offset == 0) {
      send_buffer_.swap(buffer);
    } else {
      CALF_CHECK(offset + buffer.size() < socket::max_buffer_size);

      send_buffer_.resize(offset + buffer.size());
      memcpy(send_buffer_.data() + offset, buffer.data(), buffer.size());
    }
    lock.unlock();

    send();
  }

  socket_buffer recv_buffer() {
    std::unique_lock<std::mutex> lock(recv_mutex_);
    socket_buffer buffer;
    buffer.swap(recv_buffer_);
    return std::move(buffer);
  }

  void recv_buffer(socket_buffer& buffer) {
    std::unique_lock<std::mutex> lock(recv_mutex_);
    buffer.swap(recv_buffer_);
  }

private:
  void accept_completed(socket_channel& channel) {
    if (handler_) {
      handler_(*this);
    }

    channel.connect_completed();
  }

  void connect_completed() {
    connected_flag_.store(true, std::memory_order_relaxed);

    if (handler_) {
      handler_(*this);
    }

    send();
    recv();
  }

  void send() {
    if (send_context_.is_pending || !connected_flag_.load(std::memory_order_relaxed)) {
      return;
    }

    // Swap send buffer.
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_context_.buffer.swap(send_buffer_);
    send_buffer_.clear();
    lock.unlock();

    socket_.send(send_context_, std::bind(&socket_channel::send_completed, this));
  }

  void send_completed() {
    if (send_context_.type != io_type::write) {
      closed();
      return;
    }
    send();
  }

  void recv() {
    if (recv_context_.is_pending || !connected_flag_.load(std::memory_order_relaxed)) {
      return;
    }
    
    socket_.recv(recv_context_, std::bind(&socket_channel::recv_completed, this));
  }

  void recv_completed() {
    if (recv_context_.type != io_type::read) {
      closed();
      return;
    }

    std::unique_lock<std::mutex> lock(recv_mutex_);
    std::size_t offset = recv_buffer_.size();
    if (offset == 0) {
      recv_buffer_.swap(recv_context_.buffer);
    } else {
      CALF_CHECK(offset + recv_context_.buffer.size() < socket::max_buffer_size);
      recv_buffer_.resize(offset + recv_context_.buffer.size());
      memcpy(
          recv_buffer_.data() + offset, 
          recv_context_.buffer.data(), 
          recv_context_.buffer.size());
    }
    lock.unlock();

    if (handler_) {
      handler_(*this);
    }

    recv();
  }

  void closed() {
    if (handler_) {
      handler_(*this);
    }
  }

private:
  socket socket_;
  std::atomic_bool connected_flag_;
  socket_buffer send_buffer_;
  socket_buffer recv_buffer_;
  std::mutex send_mutex_;
  std::mutex recv_mutex_;
  socket_context send_context_;
  socket_context recv_context_;
  socket_handler handler_;
};

class tcp_service {
public:
  tcp_service()
    : io_worker_(io_service_) {}

  void run() {
    io_service_.run_loop();
  }

  socket_channel& create_socket(const socket_channel::socket_handler& handler) {
    channels_.emplace_back(io_service_, handler);
    auto& channel = channels_.back();
    return channel;
  }

private:
  io_completion_service io_service_;
  io_completion_worker io_worker_;
  winsock winsock_;
  std::list<socket_channel> channels_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
