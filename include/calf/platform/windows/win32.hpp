#ifndef CALF_PLATFORM_WINDOWS_WIN32_HPP_
#define CALF_PLATFORM_WINDOWS_WIN32_HPP_

// 去除 Windows.h 中不常用的定义
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// 禁用 max min 宏
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 使用 UNICODE 字符集
#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif 

#include <Windows.h>

#ifdef assert
#undef assert
#endif 

#ifdef GetObject 
#undef GetObject 
#endif

#ifdef GetString
#undef GetString
#endif

#ifdef min
#undef min
#endif 

#ifdef max
#undef max
#endif

#define CALF_WIN32_MAIN_ENTRY(main_class) \
int wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {\
  main_class app;\
  return app.Run();\
}

#endif // CALF_PLATFORM_WINDOWS_WIN32_HPP_
