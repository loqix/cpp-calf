#include <calf\platform\windows\networking.hpp>

#include <iostream>
#include <thread>

class EchoClient {
public:
  EchoClient() 
    : thread_(&calf::tcp_service::run, &tcp_service_) {}

  void Run() {
    auto& channel = tcp_service_.create_socket(std::bind(&EchoClient::OnRecv, this, std::placeholders::_1));
    channel.send_buffer("hello");
    channel.send_buffer("world");
    channel.connect("127.0.0.1", 4900);
    channel.send_buffer("nnnnnnnnnn");

    thread_.join();
  }

  void OnRecv(calf::socket_channel& channel) {
    auto buffer = channel.recv_buffer();
    if (!buffer.empty()) {
      std::cout << "on recv: " <<
          std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()) << " size=" << buffer.size() << std::endl;
    }
  }
private:
  calf::tcp_service tcp_service_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoClient client;
  client.Run();
  return 0;
}