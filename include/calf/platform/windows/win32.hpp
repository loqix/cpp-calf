#ifndef CALF_PLATFORM_WINDOWS_WIN32_HPP_
#define CALF_PLATFORM_WINDOWS_WIN32_HPP_

// 去除 Windows.h 中不常用的定义
#define WIN32_LEAN_AND_MEAN

// 禁用 max min 宏
#define NOMINMAX

// 使用 UNICODE 字符集
#define UNICODE
#define _UNICODE
#include <Windows.h>

namespace calf {
namespace platform {
namespace windows {



} // namespace windows
} // namespace platform

using namespace platform::windows;

} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_WIN32_HPP_