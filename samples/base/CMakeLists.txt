cmake_minimum_required(VERSION 3.18)

project(base)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

set (BASE_SOURCES base.cc)

# Link
add_compile_options("/source-charset:utf-8")

add_executable(base ${BASE_SOURCES})
