#ifndef CALF_PLATFORM_WINDOWS_INPUT_HPP_
#define CALF_PLATFORM_WINDOWS_INPUT_HPP_

// ref:
// https://docs.microsoft.com/en-us/previous-versions/windows/desktop/directmanipulation/direct-manipulation-portal

#include <calf/platform/windows/win32.hpp>

namespace calf {
namespace platform {
namespace windows {

enum struct mouse_info_type {
  null,
  lb_down,
  lb_up,
  rb_down,
  rb_up,
  mb_down,
  mb_up,
  move
};

struct mouse_info {
  mouse_info()
    : type(mouse_info_type::null),
      pos(0, 0),
      lb(false),
      rb(false),
      mb(false) {}

  mouse_info_type type;
  win32_point pos;
  bool lb;
  bool rb;
  bool mb;
};

struct keyboard_info {};

enum struct pointer_info_type { null, down, up, update };

struct pointer_info {
  pointer_info()
    : type(pointer_info_type::null), pos(0, 0), pb(false), sb(false) {}

  pointer_info_type type;
  win32_point pos;
  bool pb;
  bool sb;
};

enum struct touch_info_type {

};
struct touch_info {
  touch_info_type type;
  win32_point pos;
  int id;
};

template <typename Base, bool (Base::*t_handler)(const mouse_info& info)>
class mouse_message_handler {
 public:
  static bool handle(Base* base,
                     HWND hwnd,
                     UINT message,
                     WPARAM wparam,
                     LPARAM lparam,
                     LRESULT& lresult) {
    mouse_info info;
    info.pos.x = static_cast<short>(LOWORD(lparam));
    info.pos.y = static_cast<short>(HIWORD(lparam));

    if (message == WM_LBUTTONDOWN)
      info.type = mouse_info_type::lb_down;
    else if (message == WM_LBUTTONUP)
      info.type = mouse_info_type::lb_up;
    else if (message == WM_RBUTTONDOWN)
      info.type = mouse_info_type::rb_down;
    else if (message == WM_RBUTTONUP)
      info.type = mouse_info_type::rb_up;
    else if (message == WM_MBUTTONDOWN)
      info.type = mouse_info_type::mb_down;
    else if (message == WM_MBUTTONUP)
      info.type = mouse_info_type::mb_up;
    else if (message == WM_MOUSEMOVE) {
      info.type = mouse_info_type::move;
      if (wparam & MK_LBUTTON)
        info.lb = true;
      if (wparam & MK_RBUTTON)
        info.rb = true;
      if (wparam & MK_MBUTTON)
        info.mb = true;
    }

    return (base->*t_handler)(info);
  }
  static bool is_handle(UINT message) {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
           message == WM_MOUSEMOVE;
  }
};

template <typename Base, bool (Base::*t_handler)(const pointer_info& info)>
class pointer_message_handler {
 public:
  static bool handle(Base* base,
                     HWND hwnd,
                     UINT message,
                     WPARAM wparam,
                     LPARAM lparam,
                     LRESULT& lresult) {
    pointer_info info;
    info.pos.x = LOWORD(lparam);
    info.pos.y = HIWORD(lparam);

    if (message == WM_POINTERDOWN)
      info.type = pointer_info_type::down;
    else if (message == WM_POINTERUP)
      info.type = pointer_info_type::up;
    else if (message == WM_POINTERUPDATE)
      info.type = pointer_info_type::update;

    UINT pointer_id = GET_POINTERID_WPARAM(wparam);
    POINTER_INFO pi;
    BOOL bret = ::GetPointerInfo(pointer_id, &pi);
    win32_check(bret);

    return (base->*t_handler)(info);
  }
  static bool is_handle(UINT message) {
    return message == WM_POINTERDOWN || message == WM_POINTERUP ||
           message == WM_POINTERUPDATE;
  }
};

}  // namespace windows
}  // namespace platform
}  // namespace calf

#endif  // CALF_PLATFORM_WINDOWS_INPUT_HPP_