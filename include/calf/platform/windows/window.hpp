#ifndef CALF_PLATFORM_WINDOWS_WINDOW_HPP_
#define CALF_PLATFORM_WINDOWS_WINDOW_HPP_

#include "win32.hpp"
#include "debugging.hpp"
#include "thread_message.hpp"

#include <dwmapi.h>

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <mutex>

namespace calf {
namespace platform {
namespace windows {

struct ui_point {
  ui_point() : x(0), y(0) {}
  ui_point(int x, int y) : x(x), y(y) {}

  int x;
  int y;
};

struct ui_size {
  ui_size(int w, int h) : w(w), h(h) {}

  int w;
  int h;
};

struct ui_rect : public ui_point, public ui_size {
  ui_rect(const RECT& rect) 
    : ui_point(rect.left, rect.top), 
      ui_size(rect.right - rect.left, rect.bottom - rect.top) {}
  ui_rect(int x, int y, int w, int h)
    : ui_point(x, y), ui_size(w, h) {}
  
  operator RECT() const {
    RECT trc;
    trc.left = x;
    trc.right = x + w;
    trc.top = y;
    trc.bottom = y + h;
    return trc;
  }
};

// 
template<DWORD t_style, DWORD t_ex_style>
class window_traits {
public:
  static DWORD style() { return t_style; }
  static DWORD ex_style() { return t_ex_style; }
};

// 
class window_handle {
public:
  window_handle() : hwnd_(nullptr) {}
  window_handle(HWND hwnd) : hwnd_(hwnd) {}
  window_handle(const window_handle& other) {
    hwnd_ = other.hwnd_;
  }
  window_handle(window_handle&& other) {
    hwnd_ = other.hwnd_;
    other.hwnd_ = NULL;
  }

public:
  bool valid() { 
    return ::IsWindow(hwnd_) != FALSE;
  }

  void show() {
    ::ShowWindow(hwnd_, SW_SHOW);
  }

  void move(ui_rect& rc) {
    BOOL bret = ::MoveWindow(hwnd_, rc.x, rc.y, rc.w, rc.h, TRUE);
    CALF_WIN32_API_CHECK(bret != FALSE, MoveWindow);
  }

  void move_center() {
    HMONITOR current_monitor = ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONULL);
    if (current_monitor != nullptr) {
      MONITORINFO mi;
      mi.cbSize = sizeof(MONITORINFO);
      ::GetMonitorInfoW(current_monitor, &mi);
      ui_rect monitor_rc(mi.rcWork);
      ui_rect before_rc(window_rect());
      ui_rect after_rc((monitor_rc.w - before_rc.w) / 2, (monitor_rc.h - before_rc.h) / 2, before_rc.w, before_rc.h);
      move(after_rc);
    }
  }

  void set_timer(int id, int ms) {
    ::SetTimer(hwnd_, id, ms, nullptr);
  }

  window_handle get_parent() {
    HWND result = ::GetAncestor(hwnd_, GA_PARENT); // 不使用 GetParent，这里只获取父窗口。
    CALF_WIN32_API_CHECK(result != NULL, GetAncestor);
    return window_handle(result);
  }

  bool is_visible() {
    return ::IsWindowVisible(hwnd_) != FALSE;
  }

  window_handle get_root() {
    HWND result = ::GetAncestor(hwnd_, GA_ROOT);
    CALF_WIN32_API_CHECK(result != NULL, GetAncestor);
    return window_handle(result);
  }

  window_handle get_owner() {
    HWND result = ::GetWindow(hwnd_, GW_OWNER);
    CALF_WIN32_API_CHECK(result != NULL, GetWindow);
    return window_handle(result);
  }

  void set_parent(const window_handle& handle) {
    HWND result = ::SetParent(hwnd_, handle.hwnd());
    CALF_WIN32_API_CHECK(result != NULL, SetParent);
  }

  void kill_timer(int id) {
    ::KillTimer(hwnd_, id);
  }

  void invalidate() {
    ::InvalidateRect(hwnd_, NULL, TRUE);
  }

  HDC get_dc() {
    return ::GetDC(hwnd_);
  }

  void release_dc(HDC hdc) {
    ::ReleaseDC(hwnd_, hdc);
  }

  void add_ex_style(LONG new_ex_style) {
    LONG ex_style = ::GetWindowLongW(hwnd_, GWL_EXSTYLE);
    ::SetWindowLongW(hwnd_, GWL_EXSTYLE, ex_style | new_ex_style);
  }

  // 仅 Windows 7 以上版本支持。 
  bool enable_touch() {
    return !!::RegisterTouchWindow(hwnd_, 0); // 禁用防误触。
  }

  // 仅 Windows 7 以上版本支持
  bool enable_blur_behind() {
    DWM_BLURBEHIND bb = { 0 };
    HRGN hRgn = CreateRectRgn(0, 0, -1, -1);
    bb.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.hRgnBlur = hRgn;
    bb.fEnable = TRUE;

    using DwmEnableBlurBehindWindowProc = HRESULT (WINAPI*) (
        HWND hWnd, const DWM_BLURBEHIND* pBlurBehind);
    HMODULE lib = ::LoadLibraryW(L"dwmapi.dll");
    if (lib) {
      DwmEnableBlurBehindWindowProc dwm_enable_blur_behind_window = 
          reinterpret_cast<DwmEnableBlurBehindWindowProc>(
              ::GetProcAddress(lib, "DwmEnableBlurBehindWindow"));
      if (dwm_enable_blur_behind_window) {
        return SUCCEEDED(dwm_enable_blur_behind_window(hwnd_, &bb));
      }
    }

    return false;
  }

  ui_rect window_rect() const {
    RECT rc;
    ::GetWindowRect(hwnd_, &rc);
    return ui_rect(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top);
  }

  ui_rect client_rect() const {
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    return ui_rect(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top);
  }

  void set_capture() {
    ::SetCapture(hwnd_);
  }

  void release_capture() {
    ::ReleaseCapture();
  }

  HWND hwnd() const { return hwnd_; }

protected:
  void create(
      ui_rect& rc, 
      HWND parent_wnd, 
      const WCHAR* class_name, 
      const WCHAR* title, 
      DWORD style, 
      DWORD ex_style, 
      HMENU id, 
      void* handler) {  // 如果是子窗，允许传入一个 ID。
    hwnd_ = ::CreateWindowExW(ex_style, class_name, title, 
        style, rc.x, rc.y, rc.w, rc.h, 
        parent_wnd, id, ::GetModuleHandleW(nullptr), 
        handler);
    CALF_WIN32_API_CHECK(hwnd_ != NULL, CreateWindowExW);
  }

  void destroy() {
    if (valid()) {
      ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
  }

protected:
  HWND hwnd_;
};

class window_message_handler {
public:
  virtual ~window_message_handler() {}

  virtual bool process(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lresult) = 0;
};

// 窗口类
// ref: https://docs.microsoft.com/en-us/windows/win32/winmsg/about-window-classes
class window_class {
public:
  window_class()  {}
  window_class(const std::wstring& class_name) : class_name_(class_name) {}
  virtual ~window_class() {}

  const wchar_t* class_name() const { return class_name_.c_str(); }

protected:
  std::wstring class_name_;
};

class system_window_class : public window_class {
public:
  system_window_class(const std::wstring& class_name) : window_class(class_name) {}
};

class application_window_class : public window_class {
public: 
  application_window_class() {}

  application_window_class(const std::wstring& class_name) {
    register_class(class_name);
  }

  ~application_window_class() {
    unregister_class();
  }

  bool register_class(const std::wstring& class_name) {
    if (class_name.empty()) {
      return false;
    }

    class_name_ = class_name;

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;   // 大小改变时通知重绘。
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = ::GetModuleHandleW(nullptr);
    wcex.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wcex.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  // 默认背景色
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = class_name_.c_str();
    wcex.hIconSm = nullptr;
    
    ATOM atom = ::RegisterClassExW(&wcex);
    CALF_WIN32_API_CHECK(atom != 0, RegisterClassExW);
    return atom != 0;
  }

  void unregister_class() {
    if (class_name_.empty()) {
      return;
    }

    BOOL result = ::UnregisterClassW(class_name_.c_str(), ::GetModuleHandleW(nullptr));
    CALF_WIN32_API_CHECK(result != FALSE, UnregisterClassW);
  }

private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    window_message_handler* handler = nullptr;
    if (msg == WM_NCCREATE) { // 这里的 NC 原来是 No-Client 的意思 (=.=!)。
      CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      if (cs != nullptr) {
        handler = reinterpret_cast<window_message_handler*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(handler)); // 关联消息处理函数。
      }
    } else {
      handler = reinterpret_cast<window_message_handler*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (handler != nullptr) {
      LRESULT result = 0;
      bool has_result = handler->process(msg, wparam, lparam, result);
      if (has_result) {
        return result;
      }
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
  }
};

// === 系统窗口类 ===
static const system_window_class button_class(WC_BUTTONW);
static const system_window_class edit_class(WC_EDITW);

static const ui_rect default_rect(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT);
static const ui_rect null_rect(0, 0, 0, 0);

// 窗口基础类，分离了窗口属性特性萃取，窗口类和消息处理。
template<typename WindowTraits>
class basic_window : public window_handle {
public:
  basic_window() {}

  basic_window(
      ui_rect rc, 
      const window_handle& parent, 
      const WCHAR* title, 
      const window_class& wc,
      window_message_handler* handler) {
    create(
        rc, 
        parent.hwnd(), 
        wc.class_name(), 
        title, 
        WindowTraits::style(), 
        WindowTraits::ex_style(),
        NULL, 
        reinterpret_cast<void*>(handler));
  }

  void create(
      const ui_rect& rc, 
      const window_handle& parent, 
      const window_class& wc,
      window_message_handler* handler) {
    create(
        rc, 
        parent.hwnd(), 
        wc.class_name(), 
        L"", 
        WindowTraits::style(), 
        WindowTraits::ex_style(),
        NULL, 
        reinterpret_cast<void*>(handler));
  }

  ~basic_window() {
    destroy();
  }
};

using popup_window_traits = window_traits<WS_POPUP, 0>;
using popupwindow_window_traits = window_traits<WS_POPUPWINDOW | WS_CAPTION, 0>;
using overlappedwindow_window_traits = window_traits<
    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0>;
using child_window_traits = window_traits<
    WS_CHILDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0>;
using layered_window_traits = window_traits<WS_POPUP, WS_EX_LAYERED>;
using no_redirection_bitmap_window_traits = window_traits<WS_POPUP, WS_EX_NOREDIRECTIONBITMAP>;
using message_window_traits = window_traits<WS_POPUP | WS_VISIBLE, 0>;

using popup_window = basic_window<popup_window_traits>;
using popupwindow_window = basic_window<popupwindow_window_traits>;
using overlapped_window = basic_window<overlappedwindow_window_traits>;
using child_window = basic_window<child_window_traits>;
using layered_window = basic_window<layered_window_traits>;
using no_redirection_bitmap_window = basic_window<no_redirection_bitmap_window_traits>;
using message_window = basic_window<message_window_traits>;

// === 系统窗口 ===
// 消息窗口，以它为父窗口不会被显示。
static const window_handle message_window_handle(HWND_MESSAGE);
// 系统桌面窗口。
static const window_handle desktop_window_handle(HWND_DESKTOP);
class desktop_window : public window_handle {
public:
  desktop_window() : window_handle(::GetDesktopWindow()) {}
};

using button_window_traits = window_traits<
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0>;
using button_window = basic_window<button_window_traits>;

using edit_window_traits = window_traits<
    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0>;
using edit_window = basic_window<edit_window_traits>;

class window_message_router
  : public window_message_handler {
public:
  using handler = std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)>;
  using condition = std::function<bool()>;

public:
  void route(UINT msg, const handler& hd) {
    handlers_.emplace(msg, hd);
  }

  void route(const condition& cdt, const handler& dh) {
    condition_handlers_.push_back(std::make_pair(cdt, dh));
  }

  // Override class window_message_handler methods.
  virtual bool process(
      UINT msg, 
      WPARAM wparam, 
      LPARAM lparam, 
      LRESULT& lresult) override {
    bool result = false;
    auto it = handlers_.find(msg);
    if (it != handlers_.end()) {
      result = it->second(msg, wparam, lparam, lresult);
    }

    if (!result) {
      for (auto& pair : condition_handlers_) {
        if (pair.first()) {
          result = pair.second(msg, wparam, lparam, lresult);
        }
      }
    }

    return result;
  }

private:
  std::map<UINT, handler> handlers_;
  std::vector<std::pair<condition, handler>> condition_handlers_;
};

class window
  : public window_message_handler {
public:
  using message_handler = std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)>;
  using message_handler_map = std::map<UINT, message_handler>;

public:
  window(const ui_rect& rc = default_rect, const window_handle& parent = desktop_window_handle) {
    std::call_once(class_init_flag_, [this]() {
      window_class_.register_class(L"CalfWindowClass");
    });
    window_.create(rc, parent, window_class_, this);
  }

  // Override class window_message_handler.
  bool process(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lresult) override {
    
  }

private:
  overlapped_window window_;
  message_handler_map handler_map_;

private:
  static application_window_class window_class_;
  static std::once_flag class_init_flag_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_WINDOW_HPP_
