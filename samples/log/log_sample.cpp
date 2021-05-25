#include <calf/platform/windows.hpp>

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
  calf::log_manager::instance()->set_default_target("stdout");
  CALF_LOG(info) << "asdf";
  CALF_LOG(error) << "efda";
  CALF_LOG(fatal) << "fdas";
  CALF_LOG_TARGET(m1, info) << "asdf";
  CALF_LOG_TARGET(m2, error) << "efda";
  CALF_LOG_TARGET(m3, fatal) << "fdas";

  return 0;
}