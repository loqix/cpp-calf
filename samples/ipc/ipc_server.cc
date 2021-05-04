#include <calf/platform/windows/win32.hpp>
#include <calf/platform/windows/file_io.hpp>
#include <calf/platform/windows/system_services.hpp>
#include <calf/worker_service.hpp>

#include <functional>
#include <sstream>
#include <iostream>

class IpcService {
public:
  IpcService()
    : io_worker_(io_service_),
      pipe_(L"\\\\.\\pipe\\ipc_sample", io_service_) {}

  void Run() {
    std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
        "ipc service: start." << std::endl;

    std::thread io_thread(
        std::bind(&calf::io_completion_service::run_loop, &io_service_));
    
    calf::overlapped_io_context io_context;
    io_worker_.dispatch(&calf::named_pipe::connect, &pipe_, io_context, []() {
      std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
          "ipc service: pipe connected" << std::endl;
    });

    io_thread.join();
  }

private:
  calf::io_completion_service io_service_;
  calf::io_completion_worker io_worker_;
  calf::named_pipe pipe_;
};

int main(int argc, char* argv[]) {
  return 0;    
}