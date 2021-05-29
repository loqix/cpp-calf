// 一、封装了 Strsafe.h 内的字符串处理函数，
// 这个库可以代替大部分 c 运行库的字符串函数。
// 
// windows 实现了两类函数，函数名分别带有 Cch 和 Cb 字段，
// 分别表示以字符数、字节数来表示长度。
// https://docs.microsoft.com/zh-cn/windows/desktop/api/strsafe/index
//
// Windows 内核模式也有类似的函数在 NtStrsaft.h，
// 分别带有 Rtl 和 RtlUnicode 前缀，表示使用 C 风格字符串和 UNICODE_STRING 类型参数。
// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntstrsafe/index
//
// 二、封装了 stringapiset.h 头文件中的字符串国际化函数。
// 用于编码转换。
//
#ifndef CALF_PLATFORM_WINDOWS_STRING_HPP_
#define CALF_PLATFORM_WINDOWS_STRING_HPP_

#include "win32.hpp"
#include "debugging.hpp"
#include <string>
#include <vector>

namespace calf {
namespace platform {
namespace windows {

class string {
public:
	string(const std::wstring& str) : str_(str) {}
	string(std::wstring&& str) : str_(str) {}

	void from_wstring(const std::wstring& str) {
		str_ = str;
	}
	void from_string(const std::string& str) {
		std::vector<wchar_t> buf;
		buf.resize(str.length());
	  int str_bytes = ::MultiByteToWideChar(CP_UTF8, NULL, str.c_str(), str.size(),
				buf.data(), buf.size());
		CALF_WIN32_API_CHECK(str_bytes != 0, MultiByteToWideChar);
		buf.resize(str_bytes);
		str_ = std::wstring(buf.data(), buf.size());
	}
	std::wstring to_wstring() { return std::wstring(str_); }
	std::string to_string() {
		std::vector<char> buf;
		buf.resize(str_.length() * (sizeof(wchar_t) / sizeof(char)));
		int str_bytes = ::WideCharToMultiByte(CP_UTF8, NULL, str_.c_str(), str_.length(),
					buf.data(), buf.size(), NULL, NULL);
	  CALF_WIN32_API_CHECK(str_bytes != 0, WideCharToMultiByte);
		buf.resize(str_bytes);
	  return std::string(buf.data(), buf.size());
	}

	operator std::wstring&() () {
		return str_;
	}

	operator std::wstring&&() () {
		return std::move(str_);
	}

private:
	std::wstring str_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_STRING_HPP_
