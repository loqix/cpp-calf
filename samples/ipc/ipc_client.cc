#include <calf/platform/windows/win32.hpp>
#include <calf/platform/windows/system_services.hpp>

int main(int argc, char* argv[]) {
  calf::io_completion_service io_service;
  calf::named_pipe pipe(L"\\\\.\\pipe\\ipc_sample", io_service);

  std::thread io_thread(std::bind(&calf::io_completion_service::run_loop, &io_service));
  
  io_thread.join();
  
  return 0;    
}