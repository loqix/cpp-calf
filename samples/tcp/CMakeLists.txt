cmake_minimum_required(VERSION 3.18)

project(tcp_sample)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

set (TCP_SERVER_SOURCES tcp_server.cpp)
set (TCP_CLIENT_SOURCES tcp_client.cpp)

# Link
add_compile_options("/source-charset:utf-8")

add_executable(tcp_server ${TCP_SERVER_SOURCES})
add_executable(tcp_client ${TCP_CLIENT_SOURCES})
