#ifndef CALF_PLATFORM_WINDOWS_HPP_
#define CALF_PLATFORM_WINDOWS_HPP_

#include "windows/win32.hpp"
#include "windows/debugging.hpp"
#include "windows/file_io.hpp"
#include "windows/system_services.hpp"
#include "windows/process.hpp"

namespace calf {
namespace logging {
} // namespace logging
namespace platform {
namespace windows {
namespace logging {
} // namespace logging
namespace debugging {
} // namespace debugging
} // namespace windows
} // namespace platform 

using namespace calf::logging;
using namespace calf::platform::windows;
using namespace calf::platform::windows::debugging;
using namespace calf::platform::windows::logging;

} // namespace calf

#define CALF_CHECK(result) CALF_WIN32_CHECK(result)
#define CALF_ASSERT(result) CALF_WIN32_ASSERT(result)

#endif // CALF_PLATFORM_WINDOWS_HPP_