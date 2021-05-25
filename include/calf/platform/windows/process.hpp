#ifndef CALF_PLATFORM_WINDOWS_PROCESS_HPP_
#define CALF_PLATFORM_WINDOWS_PROCESS_HPP_


#include "win32.hpp"
#include "debugging.hpp"
#include "kernel_object.hpp"

#include <string>
#include <algorithm>

namespace calf {
namespace platform {
namespace windows {

class process : public kernel_object {
public:
  process(const std::wstring& file_path, const std::wstring& command_line = std::wstring()) {
    create(file_path, command_line);
  }

  process(HANDLE handle) : kernel_object(handle) {}

  void create(const std::wstring& file_path, const std::wstring& command_line = std::wstring()) {
    STARTUPINFOW startup_info;
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info;
    memset(&process_info, 0, sizeof(process_info));

    std::wstring str_cl;
    str_cl.append(L"\"")
        .append(file_path)
        .append(L"\"");
    if (!command_line.empty()) {
    str_cl.append(L" ")
        .append(command_line);
    }
    WCHAR cl[MAX_PATH];
    std::size_t cl_size = std::min(str_cl.length(), static_cast<std::size_t>(MAX_PATH));
    memcpy(cl, str_cl.c_str(), cl_size * sizeof(WCHAR));
    cl[cl_size] = L'\0';
    BOOL result = ::CreateProcessW(
        NULL,
        cl,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startup_info,
        &process_info);
    CALF_WIN32_API_CHECK(result != FALSE, CreateProcessW);
    if (result != FALSE) {
      handle_ = process_info.hProcess;
      ::CloseHandle(process_info.hThread);
    }
  }

  void terminate() {
    ::TerminateProcess(handle_, 0);
  }

public:
  static std::wstring get_current_module_directory() {
    WCHAR buffer[MAX_PATH] = { 0 };
    DWORD size = ::GetModuleFileNameW(::GetModuleHandleW(NULL), buffer, MAX_PATH);
    std::wstring file_path(buffer, size);
    std::wstring path = file_path.substr(0, file_path.find_last_of(L'\\'));
    return std::move(path);
  }

  static std::wstring get_current_work_directory() {
    WCHAR buffer[MAX_PATH] = { 0 };
    DWORD size = ::GetCurrentDirectoryW(MAX_PATH, buffer);
    std::wstring path(buffer, size);
    return std::move(path);
  }

  static process get_current_process() {
    HANDLE handle = ::GetCurrentProcess();
    return process(handle);
  }

};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_PROCESS_HPP_