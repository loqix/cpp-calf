#ifndef CALF_PLATFORM_WINDOWS_LOGGING_HPP_
#define CALF_PLATFORM_WINDOWS_LOGGING_HPP_

#include "../../logging.hpp"

namespace calf {
namespace platform {
namespace windows {
namespace logging {

using namespace calf::logging;

class win32_logger : public logger {
public:
  win32_logger(log_level level, const wchar_t* file, int line)
    : logger(nullptr, level, file, line) {
  }
};

#define CALF_WIN32_LOG(level) calf::platform::windows::logging::logger(calf::platform::windows::logging::log_level::level, __FILEW__, __LINE__)

} // namespace logging
} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_LOGGING_HPP_
