#include <calf/platform/windows/file_io.hpp>

int main(int argc, char* argv[]) {
  calf::file_io_service service;
  std::thread thread([&service]() {
    service.run();
  });
  auto& file = service.create_file(L"D:\\Test.txt");
  file.write("asdf");
  thread.join();

  return 0;
}