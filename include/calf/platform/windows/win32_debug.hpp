#ifndef CALF_PLATFORM_WINDOWS_DEBUG_HPP_
#define CALF_PLATFORM_WINDOWS_DEBUG_HPP_

#include "win32.hpp"
#include "win32_log.hpp"

#include "DbgHelp.h"

#include <iostream>
#include <sstream>

#pragma comment(lib, "Dbghelp.lib")

#define CALF_CHECK(result) calf::platform::windows::check(result, __FILE__, __LINE__)
#define CALF_ASSERT(result) calf::platform::windows::check(result, __FILE__, __LINE__)
#define CALF_WIN32_CHECK(result, func) calf::platform::windows::win32_check(result, #func, __FILE__, __LINE__)
#define CALF_WIN32_ASSERT(result, func) calf::platform::windows::win32_check(result, #func, __FILE__, __LINE__)
#define CALF_LOG_INFO() calf::platform::windows::log_info(__FILE__, __LINE__)

namespace calf {
namespace platform {
namespace windows {

std::string get_error_format(DWORD err) {
  std::string result;

  CHAR* err_buf = nullptr;

  DWORD ret = ::FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<CHAR*>(&err_buf),
      0, NULL);
  if (ret != 0) {
    result = std::string(err_buf);
  }
  ::LocalFree(err_buf);

  return std::move(result);
}

std::string get_trace_stack() {
	static const int MAX_STACK_FRAMES = 100;
	
	void *pStack[MAX_STACK_FRAMES];
 
	HANDLE process = ::GetCurrentProcess();
	::SymInitialize(process, NULL, TRUE);
	WORD frames = ::RtlCaptureStackBackTrace(0, MAX_STACK_FRAMES, pStack, NULL);
 
	std::ostringstream oss;
	oss << "stack traceback: " << std::endl;
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

std::ostream& log_info(const char* file, int line) {
  std::cout << "[" << file << "(" << line << ")] ";
  return std::cout;
}

win32_log& check(bool expr, const char* file, int line) {
  if (!expr) {
    // 抛出 Windows 结构化异常
    ::RaiseException(STATUS_ASSERTION_FAILURE, 0, NULL, NULL);
  }

  return global_win32_log;
}

win32_log& win32_check(bool expr, const char* func, const char* file, int line) {
  if (!expr) {
    DWORD err = ::GetLastError(); 
    std::string error_format = get_error_format(err);
    std::cerr << "[" << file << "(" << line << ")] " <<
        func << " failed with error " << err << ": " << error_format << std::endl;
    std::cerr << get_trace_stack() << std::endl;

    // 抛出 Windows 结构化异常
    ::RaiseException(STATUS_ASSERTION_FAILURE, 0, NULL, NULL);
  }

  return global_win32_log;
}

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_DEBUG_HPP_