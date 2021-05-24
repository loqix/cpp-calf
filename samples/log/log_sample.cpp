#include <calf/worker_service.hpp>
#include <iostream>
#include <thread>

class Test {
public:
  bool test() {
    std::cout << "test" << std::endl;
    return false;
  }
};

int main(int argc, char* argv[]) {
  calf::worker_service worker;
  Test test;
  auto future = worker.packaged_dispatch(&Test::test, &test);
  std::thread thread(&calf::worker_service::run_loop, &worker);
  thread.join();

  return 0;
}