#include <calf/platform/windows.hpp>

#include <iostream>
#include <sstream>
#include <strstream>
#include <fstream>
#include <memory>

int main(int argc, char* argv[]) {
  auto manager = calf::logging::log_manager::instance();
  manager->add_target("file", std::make_unique<calf::log_file_target>(L"default.log"));
  manager->set_default_target("file");

  CALF_LOG(info) << L"asdf";
  CALF_ASSERT(false) << L"fdsa";

  return 0;
}