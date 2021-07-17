cmake_minimum_required(VERSION 3.13)

project(linux_sample)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")
set (LINUX_SAMPLE_SOURCES linux_sample.cpp)

# Link
add_executable(linux_sample ${LINUX_SAMPLE_SOURCES})

