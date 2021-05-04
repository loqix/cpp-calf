#ifndef CALF_PLATFORM_WINDOWS_DEBUG_HPP_
#define CALF_PLATFORM_WINDOWS_DEBUG_HPP_

#include "win32.hpp"
#include "win32_log.hpp"

namespace calf {
namespace platform {
namespace windows {

win32_log& win32_assert(bool expr) {
  if (!expr) {
    // 抛出 Windows 结构化异常
    ::RaiseException(STATUS_ASSERTION_FAILURE, 0, NULL, NULL);
  }

  return global_win32_log;
}

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_DEBUG_HPP_