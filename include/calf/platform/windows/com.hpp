#ifndef CALF_PLATFORM_WINDOWS_COM_HPP_
#define CALF_PLATFORM_WINDOWS_COM_HPP_

#include <exception>
#include <string>

#include <objbase.h>

namespace calf {
namespace platform {
namespace windows {

class com_error : public std::exception {
public:
  com_error(HRESULT hr) : err_("com error: ") {
    err_.append(std::to_string(hr));
  }

  virtual const char* what() const noexcept override {
    return err_.c_str();
  }

private:
  std::string err_;
};

inline bool com_check(HRESULT hr) {
  if (FAILED(hr)) {
    throw com_error(hr);
    return false;
  }
  return true;
}

template<typename T>
class com_ptr {
public:
  com_ptr() : ptr_(nullptr) {}
  com_ptr(T* p) : ptr_(p) {}
  ~com_ptr() { 
    release();
  }

  void release() {
    if (ptr_ != nullptr) {
      ptr_->Release();
      ptr_ = nullptr;
    }
  }

  T* get() { return ptr_; }
  T** get_addr() { return &ptr_; }
  T** operator &() { return &ptr_; }
  operator T*() { return ptr_; }
  T* operator ->() { return ptr_; }

private:
  T* ptr_;
};


class com_context {
public:
  com_context() {
    HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    com_check(hr);
  }

  ~com_context() {
    ::CoUninitialize();
  }
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_COM_HPP_
