#ifndef CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
#define CALF_PLATFORM_WINDOWS_NETWORKING_HPP_

#include "win32.hpp"
#include "debugging.hpp"
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

namespace calf {
namespace platform {
namespace windows {
namespace debugging {

class wsa_check : public check {
public:
  wsa_check(
      const wchar_t* expr, 
      const wchar_t* func, 
      const wchar_t* file, 
      int line)
    : check(expr, file, line) {
    stream_ << L" call " << func << L" failed with error ";
    DWORD err = ::WSAGetLastError();
    stream_ << err << L": " << get_error_format(err);
  }
};

class wsa_assert : public assert {
public:
  wsa_assert(
      const wchar_t* expr, 
      const wchar_t* func, 
      const wchar_t* file, 
      int line)
    : assert(expr, file, line) {
    stream_ << L" call " << func << L" failed with error ";
    DWORD err = ::WSAGetLastError();
    stream_ << err << L": " << get_error_format(err);
  }
};

} // namespace windows

#define CALF_WIN32_WSA_CHECK(result, func) if (!(result)) calf::platform::windows::debugging::wsa_check(L#result, L#func, __FILEW__, __LINE__)
#define CALF_WIN32_WSA_ASSERT(result, func) if (!(result)) calf::platform::windows::debugging::wsa_check(L#result, L#func, __FILEW__, __LINE__)

class winsock {
public:
  winsock() 
    : has_init_(false) { startup(); }
  ~winsock() { cleanup(); }

public:
  

private:
  void startup() {
    int result = ::WSAStartup(MAKEWORD(2, 2), &wsa_data_);
    CALF_WIN32_CHECK(result == 0) << "WSAStartup failed: " << result;
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
  SOCKADDR_IN local_addr;
  SOCKADDR_IN remote_addr;
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

  void bind(const std::wstring& addr) {
    CALF_WIN32_CHECK(is_valid());
    if (!is_valid()) {
      return;
    }

    SOCKADDR_IN local_addr;
    local_addr.sin_family = AF_INET;
    set_sockaddr_addr(local_addr, addr);

    int result = ::bind(
        socket_, 
        reinterpret_cast<SOCKADDR*>(&local_addr), 
        sizeof(local_addr));
    CALF_WIN32_WSA_CHECK(result != SOCKET_ERROR, bind);
  }

  void listen() {
    CALF_WIN32_CHECK(is_valid());
    if (!is_valid()) {
      return;
    }

    int result = ::listen(socket_, SOMAXCONN);
    CALF_WIN32_WSA_CHECK(result != SOCKET_ERROR, listen);
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

      // AcceptEx 函数的特性，在缓存区尾部会写入本地和远程 Socket 的地址信息。
      // 所以需要留出部分空间。
      BOOL result = accept_ex(
          socket_, 
          accept_socket.socket_,
          context.buffer.data(),
          context.buffer.size() - ((sizeof(SOCKADDR_IN) + 16) * 2),
          sizeof(SOCKADDR_IN) + 16,
          sizeof(SOCKADDR_IN) + 16,
          &bytes_received,
          &context.overlapped);
      CALF_WIN32_ASSERT(result == FALSE);
      int err = ::WSAGetLastError();
      CALF_WIN32_WSA_CHECK(err == WSA_IO_PENDING, AcceptEx);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      }
    }
  }

  void connect(
      const std::wstring& addr, 
      socket_context& context, 
      const io_handler& handler) {
    context.handler = handler;
    connect(addr, context);
  }

  void connect(
      const std::wstring& addr, 
      socket_context& context) {
    context.remote_addr.sin_family = AF_INET;
    set_sockaddr_addr(context.remote_addr, addr);

    LPFN_CONNECTEX connect_ex = get_connect_ex();
    if (connect_ex != nullptr) {
      context.type = io_type::open;
      DWORD bytes_sent = 0;
      BOOL result = connect_ex(
        socket_,
        reinterpret_cast<SOCKADDR*>(&context.remote_addr),
        sizeof(context.remote_addr),
        context.buffer.empty() ? NULL : context.buffer.data(),
        context.buffer.size(),
        &bytes_sent,
        &context.overlapped);
      CALF_WIN32_ASSERT(result == FALSE);
      int err = ::WSAGetLastError();
      CALF_WIN32_WSA_CHECK(err == WSA_IO_PENDING, ConnectEx);
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
    } else {
      int err = ::WSAGetLastError();
      //CALF_WIN32_WSA_CHECK(err == WSA_IO_PENDING || err == 0, WSASend);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      } else {
        context.type = io_type::broken;
        if (context.handler) {
          context.handler(context);
        }
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
    if (result == 0) {
    } else {
      int err = ::WSAGetLastError();
      CALF_WIN32_WSA_CHECK(err == WSA_IO_PENDING || err == 0, WSARecv);
      if (err == WSA_IO_PENDING) {
        context.is_pending = true;
      } else {
        context.type = io_type::broken;
        if (context.handler) {
          context.handler(context);
        }
      }
    }
  }

  bool is_valid() { return socket_ != INVALID_SOCKET; }

  std::wstring get_sockaddr_addr(SOCKADDR_IN& sockaddr) {
    WCHAR address[32] = { 0 };
    DWORD address_len = 32;
    int result = ::WSAAddressToStringW(
        reinterpret_cast<SOCKADDR*>(&sockaddr),
        sizeof(sockaddr),
        NULL,
        address,
        &address_len);
    CALF_WIN32_WSA_CHECK(result == 0, WSAAddressToStringW);

    return std::wstring(address);
  }

  std::uint16_t get_sockaddr_port(SOCKADDR_IN& sockaddr) {
    std::uint16_t port = 0;
    int result = ::WSANtohs(socket_, sockaddr.sin_port, &port);
    CALF_WIN32_WSA_CHECK(result == 0, WSANtohs);

    return port;
  }

private:
  // Override class calf::io_completion_handler method.
  virtual void io_completed(overlapped_io_context* context) override {
    auto sc = reinterpret_cast<socket_context*>(context);
    if (sc != nullptr) {
      switch(sc->type) {
      case io_type::create: {
        sc->is_pending = false;
        LPFN_GETACCEPTEXSOCKADDRS get_accept_ex_sockaddrs = get_get_connect_ex_sockaddrs();
        if (get_accept_ex_sockaddrs != nullptr) {
          SOCKADDR* local_addr = nullptr;
          SOCKADDR* remote_addr = nullptr;
          INT local_addr_size = 0;
          INT remote_addr_size = 0;
          get_accept_ex_sockaddrs(
              sc->buffer.data(), 
              sc->buffer.size() - ((sizeof(SOCKADDR_IN) + 16) * 2),
              sizeof(SOCKADDR_IN) + 16,
              sizeof(SOCKADDR_IN) + 16,
              &local_addr, 
              &local_addr_size,
              &remote_addr, 
              &remote_addr_size);
          if (local_addr != nullptr) {
            memcpy(&sc->local_addr, local_addr, 
                std::min(sizeof(sc->local_addr), static_cast<std::size_t>(local_addr_size)));
          }
          if (remote_addr != nullptr) {
            memcpy(&sc->remote_addr, remote_addr, 
                std::min(sizeof(sc->remote_addr), static_cast<std::size_t>(remote_addr_size)));
          }
          CALF_WIN32_ASSERT(remote_addr != nullptr);
        }
        sc->buffer.resize(sc->bytes_transferred);
        break;
      }
      case io_type::open:
        sc->is_pending = false;
        break;
      case io_type::write:
        sc->is_pending = false;
        sc->buffer.resize(sc->bytes_transferred);
        break;
      case io_type::read:
        sc->is_pending = false;
        sc->buffer.resize(sc->bytes_transferred);
        break;
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
    CALF_WIN32_WSA_CHECK(is_valid(), WSASocketW);
  }

  void close() {
    if (is_valid()) {
      int result = ::closesocket(socket_);
      CALF_WIN32_WSA_CHECK(result, closesocket);
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
      CALF_WIN32_WSA_CHECK(result != SOCKET_ERROR, WSAIoctl);
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
      CALF_WIN32_WSA_CHECK(result != SOCKET_ERROR, WSAIoctl);
    }
    return connect_ex;
  }

  LPFN_GETACCEPTEXSOCKADDRS get_get_connect_ex_sockaddrs() {
    static LPFN_GETACCEPTEXSOCKADDRS get_connect_ex_sockaddrs = nullptr;
    if (get_connect_ex_sockaddrs == nullptr) {
      GUID get_connect_ex_sockaddrs_guid = WSAID_GETACCEPTEXSOCKADDRS;
      DWORD bytes = 0;
      int result = ::WSAIoctl(
        socket_,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &get_connect_ex_sockaddrs_guid,
        sizeof(get_connect_ex_sockaddrs_guid),
        &get_connect_ex_sockaddrs,
        sizeof(get_connect_ex_sockaddrs),
        &bytes,
        NULL,
        NULL);
      CALF_WIN32_WSA_CHECK(result != SOCKET_ERROR, WSAIoctl);
    }
    return get_connect_ex_sockaddrs;
  }

  void set_sockaddr_addr(SOCKADDR_IN& sockaddr, const std::wstring& ip_addr) {
    INT address_len = sizeof(SOCKADDR_IN);
    WCHAR address[32] = { 0 };
    memcpy(&address, ip_addr.c_str(), std::min(static_cast<std::size_t>(32), ip_addr.length()) * sizeof(WCHAR));

    // 这个函数和 inet_ntos 有所区别，端口也在该函数中设置。
    int result = ::WSAStringToAddressW(
        address, 
        sockaddr.sin_family, 
        NULL, 
        reinterpret_cast<SOCKADDR*>(&sockaddr), 
        &address_len);
    CALF_WIN32_WSA_CHECK(result == 0, WSAStringToAddressW);
  }

  void set_sockaddr_port(SOCKADDR_IN& sockaddr, std::uint16_t port) {
    int result = ::WSAHtons(socket_, port, &sockaddr.sin_port);
    CALF_WIN32_WSA_CHECK(result == 0, WSAHtons);
  }

private:
  SOCKET socket_;
};

class socket_channel {
public:
  using socket_handler = std::function<void(socket_channel&)>;

public:
  socket_channel(io_completion_service& io_service, const socket_handler& handler)
    : socket_(io_service),
      handler_(handler),
      connected_flag_(false) {}

  void connect(const std::wstring& addr) {
    socket_.bind(L"0.0.0.0");

    // 初始连接就可以发送数据了。 
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_context_.buffer.swap(send_buffer_);
    send_buffer_.clear();
    lock.unlock();

    socket_.connect(addr, send_context_, std::bind(&socket_channel::connect_completed, this));
  }

  void listen(const std::wstring& ip_addr) {
    socket_.bind(ip_addr);
    socket_.listen();
  }

  void accept(socket_channel& channel) {
    socket_.accept(channel.recv_context_, channel.socket_, std::bind(&socket_channel::accept_completed, this, std::ref(channel)));
  }

  void send_buffer(const std::uint8_t* data, std::size_t size) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    std::size_t offset = send_buffer_.size();
    CALF_WIN32_CHECK(offset + size < socket::max_buffer_size);

    send_buffer_.resize(offset + size);
    memcpy(send_buffer_.data() + offset, data, size);
    lock.unlock();

    send();
  }

  void send_buffer(const std::string& data) {
    send_buffer(reinterpret_cast<const std::uint8_t*>(data.c_str()), data.size());
  }

  // 直接使用 io_buffer 可以利用移动语义交换缓存区，性能较好。
  // 推荐使用。
  void send_buffer(io_buffer& buffer) {
    std::unique_lock<std::mutex> lock(send_mutex_);

    std::size_t offset = send_buffer_.size();
    if (offset == 0) {
      send_buffer_.swap(buffer);
    } else {
      CALF_WIN32_CHECK(offset + buffer.size() < socket::max_buffer_size);

      send_buffer_.resize(offset + buffer.size());
      memcpy(send_buffer_.data() + offset, buffer.data(), buffer.size());
    }
    lock.unlock();

    send();
  }

  io_buffer recv_buffer() {
    std::unique_lock<std::mutex> lock(recv_mutex_);
    io_buffer buffer;
    buffer.swap(recv_buffer_);
    return std::move(buffer);
  }

  void recv_buffer(io_buffer& buffer) {
    std::unique_lock<std::mutex> lock(recv_mutex_);
    buffer.swap(recv_buffer_);
  }

  io_type get_type() {
    return recv_context_.type;
  }

  std::wstring get_remote_addr() {
    return socket_.get_sockaddr_addr(recv_context_.remote_addr);
  }

  std::uint16_t get_remote_port() {
    return socket_.get_sockaddr_port(recv_context_.remote_addr);
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

    // 初次连接就可能接收到数据。
    std::unique_lock<std::mutex> lock(recv_mutex_);
    recv_buffer_.swap(recv_context_.buffer);
    lock.unlock(); // 后面调用 handler 时可能重复加锁。

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

    // 发送缓存区中的数据，直接交换 buffer 即可。
    std::unique_lock<std::mutex> lock(send_mutex_);

    if (send_buffer_.empty()) {
      return;
    }

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
      CALF_WIN32_CHECK(offset + recv_context_.buffer.size() < socket::max_buffer_size);
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
  io_buffer send_buffer_;
  io_buffer recv_buffer_;
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
    std::unique_lock<std::mutex> lock(channels_mutex_);
    channels_.emplace_back(io_service_, handler);
    auto& channel = channels_.back();
    return channel;
  }

private:
  io_completion_service io_service_;
  io_completion_worker io_worker_;
  winsock winsock_;
  std::list<socket_channel> channels_;
  std::mutex channels_mutex_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
