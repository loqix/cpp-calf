#include <calf/platform/windows.hpp>

#include <iostream>
#include <sstream>
#include <strstream>
#include <fstream>

int main(int argc, char* argv[]) {
  CALF_LOG(info) << L"asdf";
  CALF_ASSERT(false) << L"fdsa";

  return 0;
}