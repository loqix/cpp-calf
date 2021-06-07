cmake_minimum_required(VERSION 3.18)

project(window_sample)

set(USE_UNICODE on cache bool)
include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")
set (WINDOW_SAMPLE_SOURCES window_sample.cpp)

# Link
add_compile_options("/source-charset:utf-8")

add_definitions(-DUNICODE -D_UNICODE)
add_executable(window_sample WIN32 ${WINDOW_SAMPLE_SOURCES})

