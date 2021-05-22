cmake_minimum_required(VERSION 3.18)

project(ipc_sample)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

set (FILE_SOURCES file.cpp)

# Link
add_compile_options("/source-charset:utf-8")

add_executable(file ${FILE_SOURCES})
