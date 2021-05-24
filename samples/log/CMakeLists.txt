cmake_minimum_required(VERSION 3.18)

project(log_sample)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

set (LOG_SOURCES log_sample.cpp)

# Link
add_compile_options("/source-charset:utf-8")

add_executable(log_sample ${LOG_SOURCES})
target_link_libraries(log_sample Dbghelp.lib)
