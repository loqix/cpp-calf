#ifndef CALF_PLATFORM_WINDOWS_KERNEL_OBJECT_HPP_
#define CALF_PLATFORM_WINDOWS_KERNEL_OBJECT_HPP_

#include "win32.hpp"

// refs: https://docs.microsoft.com/en-us/windows/win32/sysinfo/kernel-objects

namespace calf {
namespace platform {
namespace windows {

class kernel_object {
public:
  kernel_object() : handle_(NULL) {}
  kernel_object(HANDLE handle) : handle_(handle) {}
  ~kernel_object() { close(); }

  HANDLE handle() const  { return handle_; }
  bool is_null() const { return handle_ == NULL; }
  bool is_invalid() const { return handle_ == INVALID_HANDLE_VALUE; }
  bool is_valid() const { return !is_null() && !is_invalid(); }

  void reset(HANDLE handle) {
    close();
    handle_ = handle;
  }

  void close() {
    if (is_valid()) {
      ::CloseHandle(handle_);
      handle_ = NULL;
    }
  }

protected:
  HANDLE handle_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_KERNEL_OBJECT_HPP_
