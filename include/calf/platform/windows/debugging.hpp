#ifndef CALF_PLATFORM_WINDOWS_DEBUGGING_HPP_
#define CALF_PLATFORM_WINDOWS_DEBUGGING_HPP_

#include "win32.hpp"
#include "logging.hpp"

#include "DbgHelp.h"

#include <iostream>
#include <sstream>
#include <codecvt>

#pragma comment(lib, "Dbghelp.lib")

namespace calf {
namespace platform {
namespace windows {
namespace debugging {

using calf::platform::windows::logging::win32_log;
using calf::logging::log_level;

class debug {
public:
  static std::wstring get_error_format(DWORD err) {
    std::wstring result;
    WCHAR* err_buf = nullptr;
    DWORD ret = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<WCHAR*>(&err_buf),
        0, NULL);
    if (ret != 0) {
      result = err_buf;
    }
    ::LocalFree(err_buf);
    return std::move(result);
  }

  static std::string get_trace_stack() {
    static const int MAX_STACK_FRAMES = 100;
    
    void *pStack[MAX_STACK_FRAMES];

    HANDLE process = ::GetCurrentProcess();
    ::SymInitialize(process, NULL, TRUE);
    WORD frames = ::RtlCaptureStackBackTrace(0, MAX_STACK_FRAMES, pStack, NULL);

    std::ostringstream oss;
    oss << "\nstack traceback: " << std::endl;
    for (WORD i = 0; i < frames; ++i) {
      DWORD64 address = (DWORD64)(pStack[i]);

      DWORD64 displacementSym = 0;
      char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
      PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
      pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      pSymbol->MaxNameLen = MAX_SYM_NAME;

      DWORD displacementLine = 0;
      IMAGEHLP_LINE64 line;
      //SymSetOptions(SYMOPT_LOAD_LINES);
      line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

      if (::SymFromAddr(process, address, &displacementSym, pSymbol)
      && ::SymGetLineFromAddr64(process, address, &displacementLine, &line)) {
        oss << "\t" << pSymbol->Name << " at " << line.FileName << ":" << line.LineNumber << "(0x" << std::hex << pSymbol->Address << std::dec << ")" << std::endl;
      }
      else {
        //oss << "\terror: " << GetLastError() << std::endl;
        break;
      }
    }
    return oss.str();
  }

  static void throw_exception() {
    // 抛出 Windows 结构化异常
    ::RaiseException(STATUS_ASSERTION_FAILURE, 0, NULL, NULL);
  }
};

class check : public win32_log {
public:
  check(const wchar_t* expr, const wchar_t* file, int line)
    : win32_log(log_level::error, file, line) {
    stream_ << L"check \"" << expr << L"\" failed. ";
  }

  ~check() {
  }
};

class assert : public win32_log {
public:
  assert(const wchar_t* expr, const wchar_t* file, int line)
    : win32_log(log_level::error, file, line) {
    stream_ << L"assert \"" << expr << L"\" failed. ";
  }

  ~assert() {
    *this << debug::get_trace_stack();
  }
};

class api_check : public check {
public:
  api_check(
      const wchar_t* expr, 
      const wchar_t* func, 
      const wchar_t* file, 
      int line)
    : check(expr, file, line) {
    stream_ << L" call " << func << L" failed with error ";
    DWORD err = ::GetLastError();
    stream_ << err << L": " << debug::get_error_format(err);
  }
};

class api_assert : public assert {
public:
  api_assert(
      const wchar_t* expr, 
      const wchar_t* func, 
      const wchar_t* file, 
      int line)
    : assert(expr, file, line) {
    stream_ << L" call " << func << L" failed with error ";
    DWORD err = ::GetLastError();
    stream_ << err << L": " << debug::get_error_format(err);
  }
};

} // namespace debugging

namespace logging {

using calf::logging::log_target;
  
class log_debugger_target
  : public log_target {
public:
  void output(const std::wstring& data) override {
    ::OutputDebugStringW(data.c_str());
  }
};

} // namespace logging

} // namespace windows
} // namespace platform
} // namespace calf

#define CALF_WIN32_CHECK(result) if (!(result)) calf::platform::windows::debugging::check(L#result, __FILEW__, __LINE__)
#define CALF_WIN32_ASSERT(result) if (!(result)) calf::platform::windows::debugging::assert(L#result, __FILEW__, __LINE__)
#define CALF_WIN32_API_CHECK(result, func) if (!(result)) calf::platform::windows::debugging::api_check(L#result, L#func, __FILEW__, __LINE__)
#define CALF_WIN32_API_ASSERT(result, func) if (!(result)) calf::platform::windows::debugging::api_assert(L#result, L#func, __FILEW__, __LINE__)

#endif // CALF_PLATFORM_WINDOWS_DEBUG_HPP_