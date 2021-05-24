#ifndef CALF_PLATFORM_WINDOWS_WINDOW_HPP_
#define CALF_PLATFORM_WINDOWS_WINDOW_HPP_

#include "win32.hpp"
#include "debugging.hpp"
#include "thread_message_queue.hpp"

#include <dwmapi.h>

#include <typeinfo>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <vector>

namespace calf {
namespace platform {
namespace windows {

struct win32_point {
  win32_point() : x(0), y(0) {}
  win32_point(int x, int y) : x(x), y(y) {}

  int x;
  int y;
};

struct win32_size {
  win32_size(int w, int h) : w(w), h(h) {}

  int w;
  int h;
};

struct win32_rect : public win32_point, public win32_size {
  win32_rect(const RECT& rect) 
    : win32_point(rect.left, rect.top), 
      win32_size(rect.right - rect.left, rect.bottom - rect.top) {}
  win32_rect(int w, int h) : win32_point(0, 0), win32_size(w, h) {}
  win32_rect(int x, int y, int w, int h)
    : win32_point(x, y), win32_size(w, h) {}
  
  operator RECT() const {
    RECT trc;
    trc.left = x;
    trc.right = x + w;
    trc.top = y;
    trc.bottom = y + h;
    return trc;
  }
};

template<DWORD t_style, DWORD t_ex_style>
class window_traits {
public:
  static DWORD style() { return t_style; }
  static DWORD ex_style() { return t_ex_style; }
};

// 显示设备上下文
class dc_handle {
public:
  dc_handle() : hdc_(nullptr) {}
  dc_handle(HDC hdc) : hdc_(hdc) {}

  void draw(win32_rect rc, const WCHAR* text, int text_len) {
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

class window_handle {
public:
  window_handle() : hwnd_(nullptr) {}
  window_handle(HWND hwnd) : hwnd_(hwnd) {}

public:
  bool exist() { 
    return ::IsWindow(hwnd_) != FALSE;
  }

  void show() {
    ::ShowWindow(hwnd_, SW_SHOW);
  }

  void move(win32_rect& rc) {
    BOOL bret = ::MoveWindow(hwnd_, rc.x, rc.y, rc.w, rc.h, TRUE);
    CALF_WIN32_API_CHECK(bret != FALSE, MoveWindow);
  }

  void move_center() {
    HMONITOR current_monitor = ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONULL);
    if (current_monitor != nullptr) {
      MONITORINFO mi;
      mi.cbSize = sizeof(MONITORINFO);
      ::GetMonitorInfoW(current_monitor, &mi);
      win32_rect monitor_rc(mi.rcWork);
      win32_rect before_rc(window_rect());
      win32_rect after_rc((monitor_rc.w - before_rc.w) / 2, (monitor_rc.h - before_rc.h) / 2, before_rc.w, before_rc.h);
      move(after_rc);
    }
  }

  void set_timer(int id, int ms) {
    ::SetTimer(hwnd_, id, ms, nullptr);
  }

  window_handle parent() {
    return window_handle(::GetParent(hwnd_));
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

  template<typename ...InputSources>
  bool enable_raw_input(InputSources ...sources) {
    std::vector<RAWINPUTDEVICE> rids;
    rids.reserve(sizeof...(sources));  // 预分配内存
    return enable_raw_input(rids, sources...);
  }

  template<>
  bool enable_raw_input(std::vector<RAWINPUTDEVICE>& rids) {
    return ::RegisterRawInputDevices(rids.data(), rids.size(), sizeof(RAWINPUTDEVICE));
  }

  template<typename InputSource, typename ...InputSources>
  bool enable_raw_input(std::vector<RAWINPUTDEVICE>& rids, InputSource source, InputSources... sources) {
    rids.emplace_back();
    const auto &rid = rids.back();
    rid.usUsagePage = source.raw_input_usage_page();
    rid.usUsage = source.raw_input_usage();
    rid.dwFlags = source.raw_input_flags();
    rid.hwndTarget = hwnd_;
    return enable_raw_input(rids, sources...);
  }

  //bool enable_raw_input() {
  //  RAWINPUTDEVICE rid;
  //  rid.usUsagePage = 0x0D;
  //  rid.usUsage = 0x00;     // 鼠标事件
  //  rid.dwFlags = RIDEV_INPUTSINK | RIDEV_PAGEONLY; // 忽略普通鼠标事件
  //  rid.hwndTarget = hwnd_;

  //  return ::RegisterRawInputDevices(&rid, 1, sizeof(rid));
  //}

  win32_rect window_rect() const {
    RECT rc;
    ::GetWindowRect(hwnd_, &rc);
    return win32_rect(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top);
  }

  win32_rect client_rect() const {
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    return win32_rect(rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top);
  }

  void set_capture() {
    ::SetCapture(hwnd_);
  }

  void release_capture() {
    ::ReleaseCapture();
  }

  HWND hwnd() const { return hwnd_; }

protected:
  void create(win32_rect& rc, HWND parent_wnd, 
      const WCHAR* class_name, const WCHAR* title, DWORD style, DWORD ex_style, 
      HMENU id, void* handler) {  // 如果是子窗，允许传入一个 ID。
    hwnd_ = ::CreateWindowExW(ex_style, class_name, title, 
        style, rc.x, rc.y, rc.w, rc.h, 
        parent_wnd, id, ::GetModuleHandleW(nullptr), 
        handler);
    CALF_WIN32_API_CHECK(hwnd_ != NULL, CreateWindowExW);
  }

  void destroy() {
    if (exist()) {
      ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
  }

protected:
  HWND hwnd_;
};

// 窗口类
// ref: https://docs.microsoft.com/en-us/windows/win32/winmsg/about-window-classes
class window_class {
public:
  using message_handler_type = void;

public:
  window_class()  {}
  window_class(const WCHAR* class_name) : class_name_(class_name) {}

  const wchar_t* class_name() const { return class_name_.c_str(); }

protected:
  std::wstring class_name_;
};

class system_window_class : public window_class {
public:
  system_window_class(const WCHAR* class_name) : window_class(class_name) {}
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

// 应用全局窗口类
template<typename MessageHandler>
class application_window_class : public window_class {
public:
  using message_handler_type = MessageHandler;

public: 
  application_window_class() {
    class_name_ = L"CalfWindowClass_";
    class_name_.append(std::to_wstring(typeid(MessageHandler).hash_code()));
  }

  application_window_class(const WCHAR* class_name) : window_class(class_name) {}

  bool register_custom_class() {
    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(wcex);
    wcex.style = CS_HREDRAW | CS_VREDRAW;   // 大小改变时通知重绘。
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = ::GetModuleHandleW(nullptr);
    wcex.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wcex.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);  // 默认背景色
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = class_name_.c_str();
    wcex.hIconSm = nullptr;
    
    ATOM atom = ::RegisterClassExW(&wcex);
    return atom != 0;
  }

private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    MessageHandler* handler = nullptr;
    if (msg == WM_NCCREATE) { // 这里的 NC 原来是 No-Client 的意思 (=.=!)。
      CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      if (cs != nullptr) {
        handler = reinterpret_cast<MessageHandler*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(handler)); // 关联消息处理函数。
      }
    } else {
      handler = reinterpret_cast<MessageHandler*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (handler != nullptr) {
      LRESULT result = 0;
      return handler->process(hwnd, msg, wparam, lparam);
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
  }
};

class scoped_window : public window_handle {
public: 
  scoped_window() {}
  scoped_window(HWND hwnd) : window_handle(hwnd) {}
  ~scoped_window() {
    destroy();
  }

  void attach(HWND hwnd) {
    hwnd_ = hwnd;
  }

  void attach(window_handle& window) {
    hwnd_ = window.hwnd();
  }
};

// 窗口基础类，分离了窗口属性特性萃取，窗口类和消息处理。
template<typename WindowTraits, typename WindowClass>
class basic_window : public window_handle {
public:
  using window_class = WindowClass;

public:
  basic_window() = default;

  basic_window(win32_rect rc, const window_handle& parent, 
      const WCHAR* title, typename WindowClass::message_handler_type* handler) {
    WindowClass wc;
    wc.register_custom_class();
    window_handle::create(rc, parent.hwnd(), 
        wc.class_name(), title, WindowTraits::style(), WindowTraits::ex_style(),
        nullptr, reinterpret_cast<void*>(handler));
  }

  basic_window(win32_rect rc, const window_handle& parent, const WCHAR* title, 
      const WindowClass& wc, 
      int id, typename WindowClass::message_handler_type* handler) {
    window_handle::create(rc, parent.hwnd(), 
        wc.class_name(), title, WindowTraits::style(), WindowTraits::ex_style(),
        reinterpret_cast<HMENU>(id), reinterpret_cast<void*>(handler));
  }

  ~basic_window() {
    destroy();
  }
  
  // 注意：如 attach 前已经持有窗口，该窗口将被销毁。
  void attach(HWND hwnd) { 
    destroy();
    hwnd_ = hwnd; 
  }

  HWND detach() { 
    HWND hwnd = hwnd_;
    hwnd_ = nullptr;
    return hwnd;
  }

  void create(win32_rect rc, const window_handle& parent, 
      const WCHAR* title, typename WindowClass::message_handler_type* handler) {
    WindowClass wc;
    wc.register_custom_class();
    window_handle::create(rc, parent.hwnd(), 
        wc.class_name(), title, WindowTraits::style(), WindowTraits::ex_style(),
        nullptr, reinterpret_cast<void*>(handler));
  }

  void create(win32_rect rc, const window_handle& parent, const WCHAR* title, 
      const WindowClass& wc, UINT id, typename WindowClass::message_handler_type* handler) {
    window_handle::create(rc, parent.hwnd(), 
        wc.class_name(), title, WindowTraits::style(), WindowTraits::ex_style(),
        static_cast<HMENU>(id), reinterpret_cast<void*>(handler));
  }
};

template<typename WindowTraits, typename MessageHandle>
using window = basic_window<WindowTraits,
    application_window_class<MessageHandle>>;

using popup_window_traits = window_traits<WS_POPUP, 0>;
using popupwindow_window_traits = window_traits<WS_POPUPWINDOW | WS_CAPTION, 0>;
using overlappedwindow_window_traits = window_traits<
    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0>;
using child_window_traits = window_traits<
    WS_CHILDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0>;
using layered_window_traits = window_traits<WS_POPUP, WS_EX_LAYERED>;
using no_redirection_bitmap_window_traits = window_traits<WS_POPUP, WS_EX_NOREDIRECTIONBITMAP>;
using message_window_traits = window_traits<WS_POPUP | WS_VISIBLE, 0>;

template<typename MessageHandle>
using popup_window = basic_window< popup_window_traits, 
    application_window_class<MessageHandle>>;

template<typename MessageHandle>
using popupwindow_window = basic_window< popupwindow_window_traits, 
    application_window_class<MessageHandle>>;

template<typename MessageHandle>
using overlapped_window = basic_window< overlappedwindow_window_traits, 
    application_window_class<MessageHandle> >;

template<typename MessageHandle>
using child_window = basic_window< child_window_traits,
    application_window_class<MessageHandle> >;

template<typename MessageHandle>
using layered_window = basic_window< layered_window_traits, 
    application_window_class<MessageHandle> >;

template<typename MessageHandle>
using no_redirection_bitmap_window = basic_window< no_redirection_bitmap_window_traits, 
    application_window_class<MessageHandle> >;

template<typename MessageHandle>
using message_window = basic_window< message_window_traits, 
    application_window_class<MessageHandle> >;

// === 系统窗口 ===
// 消息窗口，以它为父窗口不会被显示。
static const window_handle message_window_handle(HWND_MESSAGE);
// 系统桌面窗口。
static const window_handle desktop_window_handle(HWND_DESKTOP);
class desktop_window : public window_handle {
public:
  desktop_window() : window_handle(::GetDesktopWindow()) {}
};

static const win32_rect default_rect(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT);
static const win32_rect null_rect(0, 0, 0, 0);

// === 系统窗口类 ===
// 按钮
using button_window_traits = window_traits<
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0>;
static const system_window_class button_class(WC_BUTTONW);
using button_window = basic_window< button_window_traits, 
    system_window_class >;

// 编辑框
using edit_window_traits = window_traits<
    WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0>;
static const system_window_class edit_class(WC_EDITW);
using edit_window = basic_window< edit_window_traits, 
    system_window_class >;

/*
template<USHORT t_usage_page, USHORT t_usage, DWORD t_flags>
class raw_input_traits {
public:
  static USHORT raw_input_usage_page() { return t_usage_page; }
  static USHORT raw_input_usage() { return t_usage; }
  static DWORD raw_input_flags() { return raw_input_flags; }
};

using raw_input_touch_traits = raw_input_traits<0x0D, 0x00, RIDEV_INPUTSINK | RIDEV_PAGEONLY>;
const static raw_input_touch_traits raw_input_touch;

class mouse_input {

};

class touch_input {

};

class raw_input {
public:
  static bool get_touch_state(std::vector<touch_state>& states) {
    
  }

};

using on_mouse_proc = void(window_handle&, mouse_state&);
using on_keyboard_proc = void(window_handle&, keyboard_state&);
using on_touch_proc = void(window_handle&, std::vector<touch_state>&);

class input_handler {
  using Self = input_handler;

public:
  Self& register_mouse(std::function<on_mouse_proc> func) {
    on_mouse_func_ = func;
    return *this;
  }

  Self& register_keyboard(std::function<on_keyboard_proc> func) {
    on_keyboard_func_ = func;
    return *this;
  }

  Self& register_touch(std::function<on_touch_proc> func) {
    on_touch_func_ = func;
    return *this;
  }

  template<typename MessageHandler>
  void register_self(MessageHandler& handler) {
    std::function<on_mouse_proc> on_mouse_func = on_mouse_func_;
    std::function<on_touch_proc> on_touch_func = on_touch_func_;
    std::function<on_keyboard_proc> on_keyboard_func = on_keyboard_func_;

    auto mouse_handler = [on_mouse_func](window_handle& window, WPARAM wparam, 
        LPARAM lparam, LRESULT& lresult) -> bool {
      mouse_state ms;
      ms.x = LOWORD(lparam);
      ms.y = HIWORD(lparam);
      on_mouse_func(window, ms);
      return false;
    };
    handler.register_handler(WM_MOUSEMOVE, mouse_handler);

    handler.register_handler(WM_TOUCH, [on_touch_func](window_handle& window, WPARAM wparam, 
        LPARAM lparam, LRESULT& lresult) -> bool {
      UINT ti_num = wparam;    // 触点数
      std::vector<TOUCHINPUT> ti_vec;
      ti_vec.reserve(ti_num);
      ti_vec.resize(ti_num);

      std::vector<touch_state> ts_vec;
      ti_vec.reserve(ti_num);

      if (::GetTouchInputInfo((HTOUCHINPUT)lparam, ti_num, &ti_vec[0],
          sizeof(TOUCHINPUT)) != FALSE) {
        for (auto ti : ti_vec) {
          touch_state ts;
          if (ti.dwFlags & TOUCHEVENTF_PALM) {
            if (ti.dwFlags & TOUCHEVENTF_DOWN) {
              ts.state = touch_state::kTouchStatePalmDown;
            } else if (ti.dwFlags & TOUCHEVENTF_MOVE) {
              ts.state = touch_state::kTouchStatePalmMove;
            } else if (ti.dwFlags & TOUCHEVENTF_UP) {
              ts.state = touch_state::kTouchStatePalmUp;
            }
          } else {
            if (ti.dwFlags & TOUCHEVENTF_DOWN) {
              ts.state = touch_state::kTouchStateDown;
            } else if (ti.dwFlags & TOUCHEVENTF_MOVE) {
              ts.state = touch_state::kTouchStateMove;
            } else if (ti.dwFlags & TOUCHEVENTF_UP) {
              ts.state = touch_state::kTouchStateUp;
            }
          }

          POINT pt;
          pt.x = ti.x / 100;    // 换算屏幕坐标
          pt.y = ti.y / 100;
          ::ScreenToClient(window.hwnd(), &pt);
          ts.x = pt.x;
          ts.y = pt.y;

          ts.id = ti.dwID;
          ts_vec.push_back(std::move(ts));
        }
      }

      on_touch_func(window, ts_vec);
      return true;
    });
  }

protected:
  std::function<on_mouse_proc> on_mouse_func_;
  std::function<on_keyboard_proc> on_keyboard_func_;
  std::function<on_touch_proc> on_touch_func_;
};

class raw_input_handler : input_handler {
  using Self = raw_input_handler;
  using Parent = input_handler;
public:
  Self& register_mouse(std::function<on_mouse_proc> func) {
    Parent::register_mouse(func);
    return *this;
  }

  template<typename MessageHandler>
  void register_self(MessageHandler& handler) {
    std::function<on_mouse_proc> on_mouse_func = on_mouse_func_;
    std::function<on_keyboard_proc> on_keyboard_func = on_keyboard_func_;
    handler.register_handler(WM_INPUT, [on_mouse_func,    // TODO: 这里多了一层 function 闭包的拷贝
       on_keyboard_func](window_handle& window, WPARAM,
           LPARAM lparam, LRESULT&) -> bool {
      UINT size;
      ::GetRawInputData((HRAWINPUT)lparam, RID_INPUT, nullptr, 
          &size, sizeof(RAWINPUTHEADER));
      std::unique_ptr<BYTE> data_up(new BYTE[size]);
      if (::GetRawInputData((HRAWINPUT)lparam, RID_INPUT, data_up.get(), 
          &size, sizeof(RAWINPUTHEADER)) == size) {
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(data_up.get());
        if (raw->header.dwType == RIM_TYPEMOUSE) {
          mouse_state ms;
          ms.x = raw->data.mouse.lLastX;
          ms.y = raw->data.mouse.lLastY;
          on_mouse_func(window, ms);
        }
      }
      return false;
   });
  }
};
*/

template<typename MessageRoute>
class window_message_route {
};
} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_WINDOW_HPP_
