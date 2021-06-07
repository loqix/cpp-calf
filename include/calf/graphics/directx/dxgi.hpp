#ifndef CALF_PLATFORM_WINDOWS_DXGI_HPP_
#define CALF_PLATFORM_WINDOWS_DXGI_HPP_

#include <calf/platform/windows/com.hpp>

#include <dxgi.h>

namespace calf {
namespace platform {
namespace windows {

class dxgi_factory {
public:
  dxgi_factory(IDXGIFactory* fac) : factory_(fac) {}

  bool get_adapters_desc(std::vector<DXGI_ADAPTER_DESC>& adapter_descs) {
    UINT adapter_count = 0;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC desc;

    while (factory_->EnumAdapters(adapter_count, &adapter) != DXGI_ERROR_NOT_FOUND) {
      memset(&desc, 0, sizeof(desc));
      adapter->GetDesc(&desc);
      adapter_descs.resize(adapter_count + 1);
      memcpy(&adapter_descs[adapter_count], &desc, sizeof(DXGI_ADAPTER_DESC));
      ++adapter_count;
    }

    return !adapter_descs.empty();
  }

public:
  static IDXGIFactory* create() {
    HMODULE lib = ::LoadLibraryW(L"dxgi.dll");
    if (lib == NULL) {
      return nullptr;
    }

    using CreateDXGIFactoryProc = HRESULT (WINAPI*) (REFIID riid, void **ppFactory);
    CreateDXGIFactoryProc create_dxgi_factory = 
        reinterpret_cast<CreateDXGIFactoryProc>(
            ::GetProcAddress(lib, "CreateDXGIFactory1"));
    if (create_dxgi_factory == nullptr) {
      CreateDXGIFactoryProc create_dxgi_factory = 
          reinterpret_cast<CreateDXGIFactoryProc>(
              ::GetProcAddress(lib, "CreateDXGIFactory"));
      if (create_dxgi_factory == nullptr) {
        return nullptr;
      }
    }

    IDXGIFactory* fac = nullptr;
    HRESULT hr = create_dxgi_factory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&fac));
    if (com_check(hr)) {
      return fac;
    }

    return nullptr;
  }

private:
  com_ptr<IDXGIFactory> factory_;
};

class swapchain {

};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_DXGI_HPP_
