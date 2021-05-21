#include <calf\platform\windows\networking.hpp>

#include <iostream>
#include <thread>

class EchoServer {
public:
  EchoServer() 
    : thread_(&calf::tcp_service::run, &tcp_service_) {}

  void Run() {
    auto& listen_channel = tcp_service_.create_socket(std::bind(&EchoServer::OnAccept, this, std::placeholders::_1));
    listen_channel.listen("127.0.0.1", 4900);
    auto& accept_channel = tcp_service_.create_socket(std::bind(&EchoServer::OnRecv, this, std::placeholders::_1));
    listen_channel.accept(accept_channel);

    thread_.join();
  }

  void OnAccept(calf::socket_channel& channel) {
    auto& accept_channel = tcp_service_.create_socket(std::bind(&EchoServer::OnRecv, this, std::placeholders::_1));
    channel.accept(accept_channel);
    std::cout << "on accept." << std::endl;
  }

  void OnRecv(calf::socket_channel& channel) {
    auto type = channel.get_type();
    if (type == calf::io_type::create) {
      std::cout << "remote_addr=" << channel.get_remote_addr() << " remote_port=" << channel.get_remote_port() <<std::endl;
    }
    auto buffer = channel.recv_buffer();
    if (!buffer.empty()) {
      std::cout << "on recv: " << 
          std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()) << " size=" << buffer.size() << std::endl;
      channel.send_buffer(buffer);
    }
  }

private:
  calf::tcp_service tcp_service_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoServer server;
  server.Run();
  return 0;
}