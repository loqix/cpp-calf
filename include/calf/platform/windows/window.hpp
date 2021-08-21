#pragma once

#ifndef CALF_PLATFORM_WINDOWS_WINDOW_HPP_
#define CALF_PLATFORM_WINDOWS_WINDOW_HPP_

//
// CALF TEMPLATE LIBRARY
//
// Windows 窗口和窗口资源封装
//

#include "win32.hpp"
#include "debugging.hpp"
#include "thread_message.hpp"

#include <dwmapi.h>

#include <string>
#include <map>
#include <set>
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

class window_message_handler {
public:
  virtual bool process(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lresult) = 0;
};

// Windows 经典窗口菜单
// 这里实现的是动态创建，也可以采用资源文件创建。
class menu_handle {
public:
  menu_handle()
    : hmenu_(NULL) {}

  void insert(const std::wstring& text, const menu_handle& sub_menu) {
    MENUITEMINFOW mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_DATA | MIIM_SUBMENU;
    mii.fType = MFT_STRING;
    mii.fState = MFS_ENABLED;
    mii.hSubMenu = sub_menu.hmenu(); // 关联后不需要销毁
    mii.dwTypeData = const_cast<LPWSTR>(text.c_str());
    mii.cch = static_cast<UINT>(text.length());
    ::InsertMenuItemW(hmenu_, item_count(), TRUE, &mii);
  }

  void insert(const std::wstring& text, int pos) {
    MENUITEMINFOW mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_DATA;
    mii.fType = MFT_STRING;
    mii.fState = MFS_ENABLED;
    mii.dwItemData = NULL;
    mii.dwTypeData = const_cast<LPWSTR>(text.c_str());
    mii.cch = static_cast<UINT>(text.length());
    ::InsertMenuItemW(hmenu_, pos, TRUE, &mii);
  }

  void insert(const std::wstring& text, int pos, int menu_id) {
    MENUITEMINFOW mii;
    memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_DATA | MIIM_ID;
    mii.fType = MFT_STRING;
    mii.fState = MFS_ENABLED;
    mii.dwItemData = NULL;
    mii.wID = menu_id;
    mii.dwTypeData = const_cast<LPWSTR>(text.c_str());
    mii.cch = static_cast<UINT>(text.length());
    ::InsertMenuItemW(hmenu_, pos, TRUE, &mii);
  }

  int item_count() const {
    return ::GetMenuItemCount(hmenu_);
  }

  HMENU hmenu() const { return hmenu_; }

protected:
  void destroy() {
    // 仅在没有窗口拥有该菜单时，才需要销毁
    if (hmenu_ != NULL) {
      //::DestroyMenu(hmenu_);
      hmenu_ = NULL;
    }
  }

protected:
  HMENU hmenu_; // Root menu.
};

// 子菜单
class popup_menu 
  : public menu_handle, 
    public window_message_handler {
public:
  using menu_handler = std::function<void()>;

public:
  popup_menu() {
    create();
  }

  ~popup_menu() {
    destroy();
  }

  void insert(const std::wstring& text, menu_handler&& handler) {
    int pos = __super::item_count();
    handler_map_.emplace(pos, std::move(handler));
    __super::insert(text, pos);
  }

  // Override window_message_handler methods.
  virtual bool process(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lresult) override {
    int index = LOWORD(wparam);
    auto handler_it = handler_map_.find(index);
    if (handler_it != handler_map_.end()) {
      handler_it->second();
      lresult = 0;
      return true;
    }

    return false;
  }

private:
  void create() {
    hmenu_ = ::CreatePopupMenu();

    // 绑定 handler 供窗口类回调。
    MENUINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_STYLE;
    BOOL bresult = ::GetMenuInfo(hmenu_, &mi);
    if (bresult) {
      mi.fMask = MIM_STYLE | MIM_MENUDATA;
      mi.dwStyle |= MNS_NOTIFYBYPOS;
      mi.dwMenuData = reinterpret_cast<ULONG_PTR>(static_cast<window_message_handler*>(this));
      bresult = ::SetMenuInfo(hmenu_, &mi);
    }

  }

private:
  std::map<int, menu_handler> handler_map_;
};

// 根菜单
class menu
  : public menu_handle {
public:
  menu() {
    create();
  }

  ~menu() {
    destroy();
  }

private:
  void create() {
    // 创建根菜单
    hmenu_ = ::CreateMenu();

    // 由于子菜单需要使用 handler，需要让系统处理 WM_MENUCOMMAND 消息，而不是 WM_COMMAND 消息，
    // 这样可以拿到点击的菜单句柄。
    MENUINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_STYLE;
    BOOL bresult = ::GetMenuInfo(hmenu_, &mi);
    if (bresult) {
      mi.fMask = MIM_STYLE;
      mi.dwStyle |= MNS_NOTIFYBYPOS;
      bresult = ::SetMenuInfo(hmenu_, &mi);
    }

  }
};

// 
template<DWORD t_style, DWORD t_ex_style>
class window_traits {
public:
  static DWORD style() { return t_style; }
  static DWORD ex_style() { return t_ex_style; }
};

// Handle 
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

  void set_hmenu(const menu_handle& menu) {
    BOOL bresult = ::SetMenu(hwnd_, menu.hmenu());
    CALF_WIN32_API_CHECK(bresult != FALSE, SetMenu);
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
  void create_window(
      const ui_rect& rc, 
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

  void destroy_window() {
    if (valid()) {
      ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
  }

protected:
  HWND hwnd_;
};

// 窗口类，相当于窗口模板，便于创建大量同类型窗口。
// 负责处理窗口过程，进行窗口事件分发。
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
    } else if (msg == WM_MENUCOMMAND) {
      // 处理窗口菜单事件交给菜单对象
      HMENU hmenu = reinterpret_cast<HMENU>(lparam);
      if (hmenu != NULL) {
        MENUINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_MENUDATA;
        BOOL bresult = ::GetMenuInfo(hmenu, &mi);
        handler = reinterpret_cast<window_message_handler*>(mi.dwMenuData);
      }
    } else {
      // 其他窗口事件交给窗口
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
    create(rc, parent, title, wc, handler);
  }

  void create(
      const ui_rect& rc, 
      const window_handle& parent, 
      const WCHAR* title, 
      const window_class& wc,
      const menu_handle& root_menu,
      window_message_handler* handler) {
    create_window(
        rc, 
        parent.hwnd(), 
        wc.class_name(), 
        L"", 
        WindowTraits::style(), 
        WindowTraits::ex_style(),
        root_menu.hmenu(), 
        reinterpret_cast<void*>(handler));
  }

  void attach(HWND hwnd) {
    destroy_window();
    hwnd_ = hwnd;
  }

  HWND detach() {
    HWND hwnd = hwnd_;
    hwnd_ = NULL;
    return hwnd;
  }

  ~basic_window() {
    destroy_window();
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

struct mouse_state_t {
};

struct keyboard_state_t {
};

struct screen_state_t {
};

struct message_state_t {
  message_state_t()
    : result(false) {}

  bool result;
};

struct window_state_t {
  mouse_state_t mouse_state;
  keyboard_state_t keyboard_state;
  screen_state_t screen_state;
  message_state_t message_state;
};

class window
  : public overlapped_window,
    public window_message_handler {
public:
  using window_handler = std::function<void(window_state_t&)>;
  using window_handler_list = std::list<window_handler>;
  using window_handler_map = std::map<std::string, window_handler_list>;

public:
  window(
      const ui_rect& rc = default_rect, 
      const window_handle& parent = desktop_window_handle,
      const menu_handle& root_menu = menu_handle()) {
    static application_window_class window_class_;
    static std::once_flag class_init_flag_;

    std::call_once(class_init_flag_, [this]() {
      window_class_.register_class(L"CalfWindowClass");
    });
    overlapped_window::create(rc, parent, L"", window_class_, root_menu, this);
  }

  void listen(const std::string& name, const window_handler& handler) {
    auto map_it = window_handler_map_.find(name);
    if (map_it == window_handler_map_.end()) {
      auto res = window_handler_map_.emplace(name, window_handler_list());
      if (res.second) {
        map_it = res.first;
      }
    }

    if (map_it != window_handler_map_.end()) {
      map_it->second.emplace_back(handler);
    }
  }

  // Override class window_message_handler.
  bool process(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& lresult) override {
    const char* name = nullptr;

    switch(msg) {
    case WM_CREATE:
      name = "create";
      break;
    case WM_DESTROY:
      name = "destroy";
      break;
    case WM_COMMAND:
      name = "command";
      break;
    default:
      name = "unknown";
      break;
    }

    auto map_it = window_handler_map_.find(name);
    if (map_it != window_handler_map_.end()) {
      for (auto& handler_it : map_it->second) {
        handler_it(window_state_);
        if (window_state_.message_state.result) {
          break;
        }
      }
    }

    return false;
  }

private:
  window_state_t window_state_;

  window_handler_map window_handler_map_;
};

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_WINDOW_HPP_
