#ifndef CALF_PLATFORM_LINUX_FILE_IO_HPP
#define CALF_PLATFORM_LINUX_FILE_IO_HPP

#include <unistd.h>
#include <sys/epoll.h>

// 文件描述符
class file_descriptor {
public:
  file_descriptor() : fd_(-1) {}
  ~file_descriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int get_fd() { return fd_; }

protected:
  int fd_;
};

class io_multiplexing_epoll 
  : public file_descriptor {
public:
  void create() {
    fd_ = ::epoll_create(1000);
  }

private:
  epoll_event epoll_event_;
};

#endif // CALF_PLATFORM_LINUX_FILE_IO_HPP
