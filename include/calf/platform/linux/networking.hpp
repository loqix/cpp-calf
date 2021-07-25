#ifndef CALF_PLATFORM_LINUX_NETWORKING_HPP_
#define CALF_PLATFORM_LINUX_NETWORKING_HPP_

#include "posix.hpp"
#include "file_io.hpp"
#include "debugging.hpp"

#include <cstdint>
#include <cstring>
#include <string>

#include <sys/socket.h> // ::socket()
#include <netinet/in.h> // struct sockaddr_id
#include <arpa/inet.h> // ::inet_pton()

namespace calf {
namespace platform {
namespace linux {

class socket
  : public file_descriptor,
    public io_event_handler {
public:
  socket() {
    create();
  }

  bool bind(std::string ip, uint16_t port) {
    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    ::inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr);
    local_addr.sin_port = ::htons(3000);

    int ret = ::bind(fd_, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
    return ret != -1;
  }

  void listen() {
    ::listen(fd_, 5);
  }

  // Override class io_event_handler method.
  virtual void io_event_arrived(io_event_context* context) override {

  }
  
protected:
  bool create() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  }
};

} // namespace linux 
} // namepsace platform
} // namespace calf

#endif // CALF_PLATFORM_LINUX_NETWORKING_HPP_