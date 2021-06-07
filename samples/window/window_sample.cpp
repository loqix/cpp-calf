#include <calf/platform/windows.hpp>
#include <string>

class Main {
public:
  int Run() {
    calf::log_manager::instance()->add_target("debugger", std::make_unique<calf::log_debugger_target>());
    calf::log_manager::instance()->set_default_target("debugger");

    calf::application_window_class wc(L"CalfWindowClass");
    calf::window_message_router router;

    calf::overlapped_window wnd(calf::default_rect, calf::desktop_window_handle, L"", wc, &router);
    wnd.show();
    wnd.move_center();

    CALF_LOG(info) << "parent=" << std::hex << (uint64_t)(wnd.get_parent().hwnd()) <<
        "owner=" << (uint64_t)(wnd.get_owner().hwnd());
    CALF_LOG(info) << "parent_vistable=" << wnd.is_visible();

    wnd.set_parent(calf::message_window_handle);

    CALF_LOG(info) << "parent=" << std::hex << (uint64_t)(wnd.get_parent().hwnd()) <<
        "owner=" << (uint64_t)(wnd.get_owner().hwnd());
    CALF_LOG(info) << "parent_vistable=" << wnd.is_visible();

    calf::thread_message_service service;
    service.run();

    return 0;
  }
};

CALF_WIN32_MAIN_ENTRY(Main)
