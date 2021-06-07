#ifndef CALF_PLATFORM_WINDOWS_DWRITE_HPP_
#define CALF_PLATFORM_WINDOWS_DWRITE_HPP_

#include <dwrite.h>
#include <wrl.h>

#include <memory>

namespace calf {
namespace platform {
namespace windows {
namespace dwrite {

using Microsoft::WRL::ComPtr;

class text_format {
public:
  text_format(IDWriteTextFormat* format) : format_(format) {}

  IDWriteTextFormat* get() { return format_.Get(); }

private:
  ComPtr<IDWriteTextFormat> format_;
};

class factory {
public:
  factory(IDWriteFactory *fct) : fct_(fct) {}

  std::shared_ptr<text_format> make_text_format(float size) {
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = fct_->CreateTextFormat(L"Consolas", nullptr, 
        DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"en-us", format.GetAddressOf());
    if (SUCCEEDED(hr)) {
      return std::make_shared<text_format>(format.Detach());
    }
    return nullptr;
  }

public:
  static std::shared_ptr<factory> make() {
    using DWriteCreateFactoryProc = HRESULT (WINAPI*)(DWRITE_FACTORY_TYPE factoryType,
      const IID &iid, IUnknown **factory);
    
    HMODULE lib_dwrite = LoadLibraryW(L"dwrite.dll");
    if (lib_dwrite == nullptr) {
      return nullptr;
    }

    DWriteCreateFactoryProc dwrite_create_factory = 
      reinterpret_cast<DWriteCreateFactoryProc>(
          GetProcAddress(lib_dwrite, "DWriteCreateFactory"));

    if (dwrite_create_factory == nullptr) {
      return nullptr;
    }

    ComPtr<IDWriteFactory> fct;
    HRESULT hr = dwrite_create_factory(DWRITE_FACTORY_TYPE_SHARED, 
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(fct.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      return std::make_shared<factory>(fct.Detach());
    }

    return nullptr;
  }

  IDWriteFactory* operator->() {
    return fct_.Get();
  }

private:
  ComPtr<IDWriteFactory> fct_;
};

} // namespace dwrite
} // namespace windows
} // namespace platform
} // namespace calf 

#endif // CALF_PLATFORM_WINDOWS_DWRITE_HPP_