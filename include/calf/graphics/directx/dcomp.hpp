#ifndef CALF_PLATFORM_WINDOWS_DCOMP_HPP_
#define CALF_PLATFORM_WINDOWS_DCOMP_HPP_

#include <calf/platform/windows/com.hpp>

#include <dcomp.h>

namespace calf {
namespace platform {
namespace windows {

class dcomp_device {
public: 
  dcomp_device(IDCompositionDevice* dev) : dev_(dev) {}

  void set_target(HWND hwnd, IDXGISwapChain1* swapchain) {
    HRESULT hr = dev_->CreateTargetForHwnd(hwnd, TRUE, &target_);
    if (com_check(hr)) {
      hr = dev_->CreateVisual(&visual_);
      if (com_check(hr)) {
        com_check(visual_->SetContent(swapchain));
        com_check(target_->SetRoot(visual_));
      }
    }
  }

  void commit() {
    com_check(dev_->Commit());
  }

public:
  static IDCompositionDevice* create_from_dxgi(IDXGIDevice *dxgi_device) {
    HMODULE dcomp_lib = ::LoadLibraryW(L"dcomp.dll");
    if (!dcomp_lib) {
      return nullptr;
    }

    using DCompositionCreateDeviceProc = HRESULT(WINAPI*)(
        IDXGIDevice *dxgi_device, const IID& iid, void **dcomposition_device);
    DCompositionCreateDeviceProc dcomposition_create_device = 
        reinterpret_cast<DCompositionCreateDeviceProc>(
            GetProcAddress(dcomp_lib, "DCompositionCreateDevice"));
    if (!dcomposition_create_device) {
      return nullptr;
    }

    IDCompositionDevice* dev;
    HRESULT hr = dcomposition_create_device(dxgi_device, 
        __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&dev));
    if (com_check(hr)) {
      return dev;
    }

    return nullptr;
  }


private:
  com_ptr<IDCompositionDevice> dev_;
  com_ptr<IDCompositionTarget> target_;
  com_ptr<IDCompositionVisual> visual_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_DCOMP_HPP_