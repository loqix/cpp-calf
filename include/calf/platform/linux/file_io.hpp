#ifndef CALF_PLATFORM_LINUX_FILE_IO_HPP
#define CALF_PLATFORM_LINUX_FILE_IO_HPP

#include "posix.hpp"

#include <vector>
#include <atomic>
#include <map>
#include <functional>

#include <unistd.h>
#include <sys/epoll.h>

namespace calf {
namespace platform {
namespace linux {

enum struct io_type {
  unknown,
  read, 
  write
};

using io_buffer = std::vector<uint8_t>;

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
  bool is_invalid() { return fd_ < 0; }
  bool is_valid() { return !is_invalid(); }

protected:
  int fd_;
};

struct io_event_context;
struct io_context;

using io_handler = std::function<void(io_context& context)>;

class io_event_handler {
public:
  virtual void io_event_arrived(io_event_context* context) = 0;
  virtual void io_broken() {}
};

struct io_event_context {
  io_event_context() 
    : event_handler(nullptr), 
      type(io_type::unknown) {}

  io_event_handler* event_handler;
  io_type type;
};

struct io_context 
  : public io_event_context {
  io_buffer buf;
  io_handler handler;
};

class io_multiplexing_epoll 
  : public file_descriptor {
public:
  io_multiplexing_epoll() {
    create();
  }

  void create() {
    fd_ = ::epoll_create(1000); // 最大轮询 fd 数量
  }

  void associate(int fd, io_event_context* context) {
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = context;
    ::epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &ev);
  }

  int wait(epoll_event* events, int events_count) {
    int ret = ::epoll_wait(fd_, events, events_count, -1);
    return ret;
  }
};

class io_multiplexing_service {
public:
  io_multiplexing_service()
    : quit_flag_(ATOMIC_VAR_INIT(false)) {
    events_.resize(100); // 最大返回事件数
  }

  void run_loop() {
    while(!quit_flag_.load(std::memory_order_relaxed)) {
      int ret = epoll_.wait(events_.data(), events_.size());
      for (auto& ev : events_) {
        io_event_context* context = reinterpret_cast<io_event_context*>(ev.data.ptr);
        if (context != nullptr) {
          if (ev.events & EPOLLIN) {
            context->type = io_type::read;
          } else if (ev.events & EPOLLOUT) {
            context->type = io_type::write;
          }

          io_event_handler* handler = context->event_handler;
          if (handler != nullptr) {
            handler->io_event_arrived(context);
          }
        }
      }
    }
  }

  void quit() {
    quit_flag_.store(true, std::memory_order_relaxed);
  }

  void register_fd(file_descriptor& fd, io_event_context* context) {
    epoll_.associate(fd.get_fd(), context);
  }

private:
  io_multiplexing_epoll epoll_;
  std::atomic_bool quit_flag_;
  std::vector<epoll_event> events_;
};

} // namespace linux 
} // namespace platform 
} // namespace calf

#endif // CALF_PLATFORM_LINUX_FILE_IO_HPP
