## Calf C++ Template Library

calf 是一个简单的现代 C++ 模板库，提供一些标准库没有的比较便利的功能封装。

## Usage

采用 Head-only 方式，仅包含头文件即可使用。

## Feature

### 通用

- **calf/singleton.hpp** 单例模式类模板
  - **class SingletonThreadSafe** 线程安全的单例实现

### Windows Win32 API 封装

- **calf/platform/windows/kernel_object.hpp** 内核对象封装
  - **class kernel_object** 内核对象

- **calf/platform/windows/file_io.hpp** 同步、异步文件 IO
  - **class io_completion_service** IO 完成端口封装
  - **class io_completion_worker** 基于 IO 完成端口的任务队列
  - **class file** 文件对象
  - **class file_channel** 文件读写通道
  - **class file_io_service** 基于完成端口的文件异步 IO 服务

- **calf/platform/windows/system_services.hpp** 系统服务相关
  - **class system_pipe** 匿名、命名管道
  - **class pipe_message_channel** 管道消息通道
  - **class pipe_message_service** 基于完成端口的管道消息服务

- **calf/platform/windows/networking.hpp** 网络接口
  - **class winsock** Winsock 上下文封装
  - **class socket** Socket 封装
  - **class socket_channel** Socket 通信通道。
  - **class tcp_service** 基于完成端口的 TCP 服务封装。


