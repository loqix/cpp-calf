#ifndef CALF_PLATFORM_WINDOWS_THREAD_MESSAGE_QUEUE_HPP_
#define CALF_PLATFORM_WINDOWS_THREAD_MESSAGE_QUEUE_HPP_

#include "win32.hpp"

namespace calf {
namespace platform {
namespace windows {

template<typename Base>
class basic_message_router {
  using HandlerProc = bool (Base::*)(HWND, UINT, WPARAM, LPARAM, LRESULT&);

public:
  template<UINT t_message_begin, UINT t_message_end, HandlerProc t_handler>
  class range_message_handler {
  public:
    static bool handle(Base* base, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& lresult) { 
      return (base->*t_handler)(hwnd, message, wparam, lparam, lresult); 
    }
    static bool is_handle(UINT message) {
      return message >= t_message_begin && message < t_message_end;
    }
  };

  template<UINT t_message, HandlerProc t_handler>
  class message_handler {
  public:
    static bool handle(Base* base, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& lresult) { 
      return (base->*t_handler)(hwnd, message, wparam, lparam, lresult); 
    }
    static bool is_handle(UINT message) {
      return message == t_message;
    }
  };

  template<typename ...MessageHandlerList>
  class message_dispatcher;

  template<typename MessageHandler, typename ...MessageHandlerList>
  class message_dispatcher<MessageHandler, MessageHandlerList...> {
  public:
    static bool dispatch(Base* base, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& lresult) {
      if (MessageHandler::is_handle(message)) {
        return MessageHandler::handle(base, hwnd, message, wparam, lparam, lresult);
      } else {
        return message_dispatcher<MessageHandlerList...>::dispatch(base, hwnd, message, wparam, lparam, lresult);
      }
    }
  };

  template<typename MessageHandler>
  class message_dispatcher<MessageHandler> {
  public:
    static bool dispatch(Base* base, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, LRESULT& lresult) {
      if (MessageHandler::is_handle(message)) {
        return MessageHandler::handle(base, hwnd, message, wparam, lparam, lresult);
      } else {
        return false;
      }
    }
  };

public:
  LRESULT process(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    LRESULT lresult = 0;
    if (static_cast<Base*>(this)->dispatch(      // ����ݹ�ģ������
        hwnd, message, wparam, lparam, lresult)) {
      return lresult;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }
};

template<typename Base>
using message_router = basic_message_router<Base>;

struct loop_mode_block {};
struct loop_mode_non_block {};

template<typename LoopMode>
class basic_message_loop;

template<>
class basic_message_loop<loop_mode_block> {
public:
  int polling(HWND hwnd = nullptr) {
    MSG msg = { 0 };
    BOOL result = FALSE;
    while(result = ::GetMessageW(&msg, hwnd, 0, 0), result != 0) {
      if (msg.message == WM_QUIT) {
        break;
      }

      // ���յ� WM_QUIT ʱ result Ϊ 0������ʱ��Ϊ���㡣
      // ��� hwnd �����򷵻� -1��������ٶ� hwnd û�����⡣
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }

    return 0;
  }
};

using message_loop = basic_message_loop<loop_mode_block>;

} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_THREAD_MESSAGE_QUEUE_HPP_
