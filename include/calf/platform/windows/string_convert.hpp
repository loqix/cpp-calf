#ifndef CALF_PLATFORM_WINDOWS_STRING_CONVERT_HPP_
#define CALF_PLATFORM_WINDOWS_STRING_CONVERT_HPP_

#include "win32.hpp"
#include <string>

namespace calf {
namespace platform {
namespace windows {

class convert {
  
};

struct string_code_wide {
  using Char = wchar_t;
};
struct string_code_utf8 {
  using Char = char;
};
struct string_code_ansi {
  using Char = char;
};
struct string_code_utf16 {
  using Char = wchar_t;
};

template<typename SrcCode, typename DstCode> 
class string_convert;

template<>
class string_convert<string_code_wide, string_code_utf8> {
  using SrcCode = string_code_wide;
  using DstCode = string_code_utf8;

public:
  string_convert(const SrcCode::Char* src, size_t src_len) {
    int dst_len = ::WideCharToMultiByte(CP_UTF8, 0, src, (int)src_len, 
        nullptr, 0, nullptr, nullptr);
    DstCode::Char* dst = reinterpret_cast<DstCode::Char*>(
        malloc(dst_len * sizeof(DstCode::Char)));
    if (dst != nullptr) {
      dst_len = ::WideCharToMultiByte(CP_UTF8, 0, src, (int)src_len,
          dst, dst_len, nullptr, nullptr);
      if (dst_len != 0) {
        dst_string_ = std::move(std::string(dst, dst_len));
      }
      free(dst);
      dst = nullptr;
    }
  }

  string_convert(const std::wstring& data) : string_convert(data.c_str(), data.length()) {}

  operator std::string&&() { return std::move(dst_string_); }

  std::string&& str() { return std::move(dst_string_); }

private:
  std::string dst_string_;
};

template<>
class string_convert<string_code_utf8, string_code_wide> {
  using SrcCode = string_code_utf8;
  using DstCode = string_code_wide;

public:
  string_convert(const std::string& src) : string_convert(src.c_str(), src.length()) {}

  string_convert(const SrcCode::Char* src, size_t src_len) {
    int dst_len = ::MultiByteToWideChar(CP_UTF8, 0, src, (int)src_len, 
        nullptr, 0);
    DstCode::Char* dst = reinterpret_cast<DstCode::Char*>(
        malloc(dst_len * sizeof(DstCode::Char)));
    if (dst != nullptr) {
      dst_len = ::MultiByteToWideChar(CP_UTF8, 0, src, (int)src_len,
          dst, dst_len);
      if (dst_len != 0) {
        dst_string_ = std::wstring(dst, dst_len);
      }
      free(dst);
      dst = nullptr;
    }
  }

  operator std::wstring&&() { return std::move(dst_string_); }

  std::wstring&& str() { return std::move(dst_string_); }

private:
  std::wstring dst_string_;
};

using string_convert_from_wide_to_utf8 = 
    string_convert<string_code_wide, string_code_utf8>;
using string_convert_from_utf8_to_wide = 
    string_convert<string_code_utf8, string_code_wide>;

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_STRING_CONVERT_HPP_