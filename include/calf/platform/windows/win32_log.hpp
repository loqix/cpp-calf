#ifndef CALF_PLATFORM_WINDOWS_LOG_HPP_
#define CALF_PLATFORM_WINDOWS_LOG_HPP_

namespace calf {
namespace platform {
namespace windows {

class win32_log {
public:
};

win32_log& operator << (win32_log& log, const char* str) {
  return log;
}

static win32_log global_win32_log;

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_LOG_HPP_