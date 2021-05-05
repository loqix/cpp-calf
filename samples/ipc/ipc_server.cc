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
      pipe_(L"\\\\.\\pipe\\ipc_sample", calf::io_mode::create, io_service_) {}

  void Run() {
    std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
        "ipc service: start." << std::endl;

    std::thread io_thread(&calf::io_completion_service::run_loop, &io_service_);
    
    io_worker_.dispatch(&calf::named_pipe::connect, &pipe_, std::ref(read_context_), [this](calf::overlapped_io_context& context) {
      std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
          "ipc service: pipe connected" << std::endl;
      
      pipe_.read(read_context_, [this](calf::overlapped_io_context& context) {
        if (context.type == calf::io_type::broken) {
          std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
              "ipc service: pipe close" << std::endl;
          io_service_.quit();
          context.handler = nullptr;
          return;
        }
        std::string data(reinterpret_cast<char*>(read_context_.buffer.data()), read_context_.buffer.size());
        std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
            "ipc service: pipe read \"" << data << "\" length=" << read_context_.bytes_transferred << std::endl;
        pipe_.write(write_context_, reinterpret_cast<uint8_t*>("fdas"), 4);
        pipe_.read(read_context_);
      });
    });

    io_thread.join();
    std::cout << "thread(" << ::GetCurrentThreadId() << ") " <<
        "ipc service: end." << std::endl;
  }

private:
  calf::io_completion_service io_service_;
  calf::io_completion_worker io_worker_;
  calf::named_pipe pipe_;
  calf::io_context read_context_;
  calf::io_context write_context_;
};

int main(int argc, char* argv[]) {
  IpcService service;
  service.Run();
  return 0;    
}