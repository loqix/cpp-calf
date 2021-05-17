#ifndef CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
#define CALF_PLATFORM_WINDOWS_NETWORKING_HPP_

#include "win32_debug.hpp"
#include "file_io.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

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
  winsock(io_completion_service& iocp) 
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

class socket {
public:
  socket(io_completion_service& io_service)
    : socket_(INVALID_SOCKET) {
    create();  
    if (is_valid()) {
      io_service.register_handle(reinterpret_cast<HANDLE>(socket_), ;
    }
  }

  void bind() {
    CALF_CHECK(is_valid());
    if (!is_valid()) {
      return;
    }

    SOCKADDR_IN local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    local_addr.sin_port = ::htons(4900);

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
  
  void accept() {

  }

  bool is_valid() { return socket_ != INVALID_SOCKET; }

private:
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

private:
  SOCKET socket_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
