cmake_minimum_required(VERSION 3.18)

project(ipc_sample)

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../include")

set (IPC_SERVER_SOURCES ipc_server.cc)
set (IPC_CLIENT_SOURCES ipc_client.cc)

# Link
add_compile_options("/source-charset:utf-8")

add_executable(ipc_server ${IPC_SERVER_SOURCES})
add_executable(ipc_client ${IPC_CLIENT_SOURCES})
target_link_libraries(ipc_server Dbghelp.lib)
target_link_libraries(ipc_client Dbghelp.lib)
