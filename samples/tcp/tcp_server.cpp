#include <calf\platform\windows\networking.hpp>

#include <iostream>
#include <thread>

class EchoServer {
public:
  EchoServer() 
    : io_worker_(io_service_),
      listen_socket_(io_service_),
      accept_socket_(io_service_),
      thread_(&calf::io_completion_service::run_loop, &io_service_) {}

  void Run() {
    listen_socket_.bind("127.0.0.1", 4900);
    listen_socket_.listen();
    recv_context_.type = calf::io_type::create;
    listen_socket_.accept(
        recv_context_, 
        accept_socket_, 
        std::bind(&EchoServer::OnAccept, this));
    thread_.join();
  }

  void OnAccept() {
    std::cout << "on accept." << std::endl;
  }

private:
  calf::io_completion_service io_service_;
  calf::io_completion_worker io_worker_;
  calf::winsock winsock_;
  calf::socket listen_socket_;
  calf::socket accept_socket_;
  calf::io_context recv_context_;
  calf::io_context send_context_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoServer server;
  server.Run();
  return 0;
}