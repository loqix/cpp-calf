#ifndef CALF_PLATFORM_WINDOWS_GID_HPP_
#define CALF_PLATFORM_WINDOWS_GID_HPP_

#include <calf/platform/windows/win32.hpp>
#include <calf/platform/windows/window.hpp>

#include <wingdi.h>

namespace calf {
namespace platform {
namespace windows {
namespace gdi {

template<typename Handle>
class object {
public:
  object() : handle_(nullptr) {}

  void destroy() {
    if (handle_ != nullptr) {
      ::DeleteObject(handle_);
      handle_ = nullptr;
    }
  }

  void detach() {
    handle_ = nullptr;
  }

  HGDIOBJ gdiobj() { return handle_; }
  Handle handle() { return handle_; }

protected:
  Handle handle_;
};


class pen : public object<HPEN> {
public:
  pen() {
    // ref: https://docs.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createpen
    handle_ = ::CreatePen(PS_SOLID, 5, RGB(0, 0, 0)); // 绘制实线
  }

  ~pen() {
    destroy();
  }
};

class brush : public object<HBRUSH> {
public:
  brush(BYTE r, BYTE g, BYTE b) {
    handle_ = ::CreateSolidBrush(RGB(r, g, b));
  }

  ~brush() {
    destroy();
  }
};

// 非兼容 Bitmap，允许内存读写，无法硬件加速。
class bitmap : public object<HBITMAP> {
public:
  bitmap(windows::dc_handle& dc, int width, int height)
    : buffer_(nullptr) {
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    handle_ = ::CreateDIBSection(dc.hdc(), &bmi, DIB_PAL_COLORS, 
        &buffer_, NULL, 0);
  }

  ~bitmap() {
    destroy();
    buffer_ = nullptr;
  }

  void* const buffer() { return buffer_; }

private:
  void* buffer_;
};

class rgn : public object<HRGN> {
public:
  ~rgn() {
    destroy();
  }

  void combine_or(rgn& other) {
    combine(other, RGN_OR);
  }

  void combine_diff(rgn& other) {
    combine(other, RGN_DIFF);
  }

private:
  void combine(rgn& other, int mode) {
    ::CombineRgn(handle_, handle_, other.handle(), mode);
  }
};

class rect_rgn : public rgn {
public:
  rect_rgn(int x1, int y1, int x2, int y2) {
    handle_ = ::CreateRectRgn(x1, y1, x2, y2);
  }
};

// 兼容 Bitmap，允许硬件加速。
class compatible_bitmap : public object<HBITMAP> {
public:
  compatible_bitmap(windows::dc_handle& dc, int width, int height) {
    handle_ = ::CreateCompatibleBitmap(dc.hdc(), width, height);
  }
  ~compatible_bitmap() {
    destroy();
  }
};

class dc{
public:
  dc() : hdc_(nullptr) {}
  dc(HDC hdc) : hdc_(hdc) {}

  void draw(rect rc, const WCHAR* text, int text_len) {
    RECT target_rc = rc;
    ::DrawTextW(hdc_, text, text_len, &target_rc, 0);
  }

  void destroy() {
    if (hdc_ != nullptr) {
      ::DeleteDC(hdc_);
      hdc_ = nullptr;
    }
  }

  HDC hdc() const { return hdc_; }

protected:
  HDC hdc_;
};


class dc_handle : public dc {
public:
  dc_handle() = default;
  dc_handle(dc& dc) : dc(dc) {}

  template<typename GdiObject>
  void select(GdiObject& obj) {
    ::SelectObject(hdc_, obj.gdiobj());
  }

  void move(int x, int y) {
    ::MoveToEx(hdc_, x, y, nullptr);
  }

  void draw_line(int x, int y) {
    ::LineTo(hdc_, x, y);
  }

  void draw_rect(const win32_rect& rc, HBRUSH hbr) {
    RECT rect = rc;
    ::FillRect(hdc_, &rect, hbr);
  }

  void bit_blt(windows::win32_rect rc, windows::dc_handle& dc) {
    win32_check(::BitBlt(hdc_, rc.x, rc.y, rc.w, rc.h, dc.hdc(), 0, 0, SRCCOPY)
        != FALSE);
  }

  void stretch_blt(windows::win32_rect dst_rc, windows::dc_handle& dc, windows::win32_rect src_rc) {
    win32_check(::StretchBlt(hdc_, dst_rc.x, dst_rc.y, dst_rc.w, dst_rc.h, 
        dc.hdc(), src_rc.x, src_rc.y, src_rc.w, src_rc.h, SRCCOPY));
  }

  int width() {
    return ::GetDeviceCaps(hdc_, HORZRES);
  }

  int height() {
    return ::GetDeviceCaps(hdc_, VERTRES);
  }
};

class dc : public dc_handle {
public:
  dc() = default;
  dc(HDC hdc) : dc_handle(hdc) {}
  dc(LPCWSTR driver, LPCWSTR device, LPCWSTR port, const DEVMODEW* pdm) {
    hdc_ = ::CreateDCW(driver, device, port, pdm);
  }
  ~dc() {
    destroy();
  }

  static HDC create_desktop_dc() {
    return ::CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
  }
};

class scoped_dc : public dc_handle {
public:
  scoped_dc(window_handle& window) : window_(window) {
    hdc_ = window.get_dc();
  }
  
  ~scoped_dc() {
    window_.release_dc(hdc_);
    hdc_ = nullptr;
  }

private:
  window_handle& window_;
};

class compatible_dc : public dc_handle {
public:
  compatible_dc(windows::dc_handle& dc) {
    hdc_ = ::CreateCompatibleDC(dc.hdc());
  }

  ~compatible_dc() {
    destroy();
  }
};

} // namespace gdi
} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_GID_HPP_
