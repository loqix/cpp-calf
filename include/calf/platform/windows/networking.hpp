#ifndef CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
#define CALF_PLATFORM_WINDOWS_NETWORKING_HPP_

#include "win32_debug.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

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
  socket()
    : socket_(INVALID_SOCKET) {}

private:
  void create_tcp() {
    ADDRINFOW info_hints;
    ADDRINFOW* info_result = nullptr;

    memset(&info_hints, 0, sizeof(info_hints));
    info_hints.ai_family = AF_INET;
    info_hints.ai_socktype = SOCK_STREAM;
    info_hints.ai_protocol = IPPROTO_TCP;
    info_hints.ai_flags = AI_PASSIVE;

    int result = ::GetAddrInfoW(NULL, L"4900", &info_hints, &info_result);
    CALF_WSA_CHECK(result == 0, GetAddrInfoW);

    if (result == 0) {
      socket_ = ::socket(
          info_result->ai_family,
          info_result->ai_socktype,
          info_result->ai_protocol);
      CALF_WSA_CHECK(socket_ != INVALID_SOCKET, socket);
    }
  }

private:
  SOCKET socket_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_NETWORKING_HPP_
