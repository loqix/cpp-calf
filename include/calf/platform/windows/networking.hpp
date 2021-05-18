#ifndef CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
#define CALF_PLATFORM_WINDOWS_NETWORKING_HPP_

#include "win32_debug.hpp"
#include "file_io.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <mutex>

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

  void connect(socket_context& context, const io_handler& handler) {
    context.handler = handler;
    connect(context);
  }

  void connect(socket_context& context) {
    SOCKADDR_IN remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    remote_addr.sin_port = ::htons(4900);

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
    int result = ::WSASend(
        socket_,
        &context.wsabuf,
        1,
        NULL,
        NULL,
        &context.overlapped,
        NULL);
    CALF_ASSERT(result == FALSE);
    int err = ::WSAGetLastError();
    CALF_WSA_CHECK(err == WSA_IO_PENDING, WSASend);
    if (err == WSA_IO_PENDING) {
      context.is_pending = true;
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
    int result = ::WSARecv(
        socket_,
        &context.wsabuf,
        1,
        NULL,
        NULL,
        &context.overlapped,
        NULL);
    CALF_ASSERT(result == FALSE);
    int err = ::WSAGetLastError();
    CALF_WSA_CHECK(err == WSA_IO_PENDING, WSARecv);
    if (err == WSA_IO_PENDING) {
      context.is_pending = true;
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
  socket_channel(io_completion_service& io_service)
    : socket_(io_service) {}

private:
  socket socket_;
  socket_context send_context_;
  socket_context recv_context_;
};

class tcp_service {
public:
  tcp_service(const std::string& ip_addr, std::uint16_t port, io_mode mode)
    : io_worker_(io_service_),
      ip_addr_(ip_addr),
      port_(port),
      mode_(mode) {}

  void run() {
    io_service_.run_loop();
  }

  socket_channel& create_socket()

private:
  std::string ip_addr_;
  std::uint16_t port_;
  io_mode mode_;
  io_completion_service io_service_;
  io_completion_worker io_worker_;
  std::list<socket_channel> channels_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
