#include <calf\platform\windows\networking.hpp>

#include <iostream>
#include <thread>

class EchoClient {
public:
  EchoClient() 
    : thread_(&calf::tcp_service::run, &tcp_service_) {}

  void Run() {
    auto& channel = tcp_service_.create_socket(std::bind(&EchoClient::OnRecv, this, std::placeholders::_1));
    channel.connect("127.0.0.1", 4900);
    calf::socket_channel::socket_buffer buffer(10, 'n');
    channel.send_buffer(buffer);

    thread_.join();
  }

  void OnRecv(calf::socket_channel& channel) {
    auto buffer = channel.recv_buffer();
    std::cout << "on recv: " << 
        std::string(reinterpret_cast<char*>(buffer.data()), buffer.size()) << std::endl;
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