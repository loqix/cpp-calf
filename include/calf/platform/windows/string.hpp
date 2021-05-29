// һ����װ�� Strsafe.h �ڵ��ַ�����������
// �������Դ���󲿷� c ���п���ַ���������
// 
// windows ʵ�������ຯ�����������ֱ���� Cch �� Cb �ֶΣ�
// �ֱ��ʾ���ַ������ֽ�������ʾ���ȡ�
// https://docs.microsoft.com/zh-cn/windows/desktop/api/strsafe/index
//
// Windows �ں�ģʽҲ�����Ƶĺ����� NtStrsaft.h��
// �ֱ���� Rtl �� RtlUnicode ǰ׺����ʾʹ�� C ����ַ����� UNICODE_STRING ���Ͳ�����
// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntstrsafe/index
//
// ������װ�� stringapiset.h ͷ�ļ��е��ַ������ʻ�������
// ���ڱ���ת����
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
