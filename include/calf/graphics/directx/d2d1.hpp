#ifndef CALF_PLATFORM_WINDOWS_D2D1_HPP_
#define CALF_PLATFORM_WINDOWS_D2D1_HPP_

#include <d2d1_1.h>
#include <wrl.h>

#include <memory>

namespace calf {
namespace platform {
namespace windows {
namespace d2d1 {
  
using Microsoft::WRL::ComPtr;

class surface {
public:
  surface(ID2D1RenderTarget* rt) : rt_(rt) {}

  void begin_draw() {
    rt_->BeginDraw();
  }

  void end_draw() {
    rt_->EndDraw();
  }

  void clear() {
    rt_->Clear();
  }

  void fill_rect(const D2D1_RECT_F &rect, const D2D1_COLOR_F &color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt_->FillRectangle(rect, brush.Get());
  }

  void draw_rect(const D2D1_RECT_F &rect, const D2D1_COLOR_F &color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    rt_->DrawRectangle(rect, brush.Get(), 5.0f);
  }

  void draw_text(const WCHAR* text, UINT32 len, IDWriteTextFormat* format, 
      const D2D1_RECT_F &rect, const D2D1_COLOR_F &color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr = rt_->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (SUCCEEDED(hr)) {
      rt_->DrawText(text, len, format, rect, brush.Get());
    }
  }

  ID2D1RenderTarget* operator->() {
    return rt_.Get();
  }

private:
  ComPtr<ID2D1RenderTarget> rt_;
};

class scoped_draw {
public:
  scoped_draw(const std::shared_ptr<surface>& target) : target_(target) {
    if (target_) {
      target_->begin_draw();
    }
  }
  ~scoped_draw() {
    if (target_) {
      target_->end_draw();
    }
  }

private:
  const std::shared_ptr<surface> target_;
};

class factory {
public:
  factory(ID2D1Factory* factory) 
      : factory_(factory) {}
  ~factory() {}

  std::shared_ptr<surface> make_surface(IDXGISurface* dxgi_surface) {
    ComPtr<ID2D1RenderTarget> rt;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, 
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),   // alpha 预乘
        96, 96);

    // D2D 目前仅在 Win7 PU 以上版本可以和 DX11 交互。
    // Win7 SP1 上仅允许和 DX10.1 交互。
    HRESULT hr = factory_->CreateDxgiSurfaceRenderTarget(
        dxgi_surface, &props, rt.GetAddressOf());
    if (SUCCEEDED(hr)) {
      return std::make_shared<surface>(rt.Detach());
    }

    return nullptr;
  }

public:
  static std::shared_ptr<factory> make() {
    using D2D1CreateFactoryProc = HRESULT (WINAPI*) (
        D2D1_FACTORY_TYPE factoryType,
        const IID & riid,
        const D2D1_FACTORY_OPTIONS *pFactoryOptions,
        void **ppIFactory);
    HMODULE lib_d2d1 = LoadLibraryW(L"d2d1.dll");
    if (lib_d2d1 == nullptr) {
      return nullptr;
    }

    D2D1CreateFactoryProc d2d1_create_factory = reinterpret_cast<D2D1CreateFactoryProc>(
        GetProcAddress(lib_d2d1, "D2D1CreateFactory"));
    if (d2d1_create_factory == nullptr) {
      return nullptr;
    }

    HRESULT hr = E_FAIL;

    ComPtr<ID2D1Factory> fct;
    hr = d2d1_create_factory(D2D1_FACTORY_TYPE_SINGLE_THREADED, 
        __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(fct.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      return std::make_shared<factory>(fct.Detach());
    }

    return nullptr;
  }

private:
  ComPtr<ID2D1Factory> factory_;
};

}
} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_DIRECT_D2D1_HPP_
