#ifndef CALF_PLATFORM_WINDOWS_HPP_
#define CALF_PLATFORM_WINDOWS_HPP_

#include "windows/win32.hpp"
#include "windows/logging.hpp"
#include "windows/debugging.hpp"

namespace calf {
namespace platform {
namespace windows {
} // namespace platform
} // namespace windows

using namespace calf::platform::windows;

} // namespace calf

#define CALF_CHECK(result) CALF_WIN32_CHECK(result)
#define CALF_ASSERT(result) CALF_WIN32_ASSERT(result)
#define CALF_LOG(level) CALF_WIN32_LOG(level)

#endif // CALF_PLATFORM_WINDOWS_HPP_