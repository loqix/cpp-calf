#include <calf\platform\windows\networking.hpp>

#include <iostream>
#include <thread>

class EchoClient {
public:
  EchoClient() 
    : io_worker_(io_service_),
      socket_(io_service_),
      thread_(&calf::io_completion_service::run_loop, &io_service_) {}

  void Run() {
    socket_.bind("127.0.0.1", 0);
    socket_.connect(recv_context_, std::bind(&EchoClient::OnConnect, this));
    thread_.join();
  }

  void OnConnect() {
    std::cout << "on connect." << std::endl;
  }

private:
  calf::io_completion_service io_service_;
  calf::io_completion_worker io_worker_;
  calf::winsock winsock_;
  calf::socket socket_;
  calf::io_context recv_context_;
  calf::io_context send_context_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoClient client;
  client.Run();
  return 0;
}