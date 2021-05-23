#ifndef CALF_PLATFORM_WINDOWS_LOGGING_HPP_
#define CALF_PLATFORM_WINDOWS_LOGGING_HPP_

#include "win32.hpp"

#include <sstream>
#include <string>
#include <iostream>
#include <codecvt>

namespace calf {
namespace platform {
namespace windows {
namespace debugging {

enum class log_level {
  verbose,
  info,
  warn,
  error,
  fatal
};

class log_target {

};

class log {
public:
  log(log_level level, const wchar_t* file, int line) {
    stream_ << L"[" << file << L"(" << line << L") " << get_level_string(level) << L"] ";
  }

  ~log() {
    std::wcout << stream_.str() <<std::endl;
  }

  log& operator<< (const wchar_t* str) {
    stream_ << str;
    return *this;
  }

  log& operator<< (int n) {
    stream_ << n;
    return *this;
  }

  log& operator<< (const std::wstring& str) {
    stream_ << str;
    return *this;
  }

  log& operator<< (const char* str) {
    stream_ << convert_.from_bytes(str);
    return *this;
  }

  log& operator<< (const std::string& str) {
    stream_ << convert_.from_bytes(str);
    return *this;
  }

private:
  static const wchar_t* get_level_string(log_level level) {
    switch (level)
    {
    case log_level::verbose:
      return L"VERBOSE";
    case log_level::info:
      return L"INFO";
    case log_level::warn:
      return L"WARN";
    case log_level::error:
      return L"ERROR";
    case log_level::fatal:
      return L"FATAL";
    default:
      return L"UNKNOWN";
    }
  }

protected:
  std::wstringstream stream_;
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert_;
};

} // namespace debugging
} // namespace windows
} // namespace platform
} // namespace calf

#define CALF_WIN32_LOG(level) calf::platform::windows::debugging::log(calf::platform::windows::debugging::log_level::level, __FILEW__, __LINE__)

#endif // CALF_PLATFORM_WINDOWS_LOGGING_HPP_