#include <calf/platform/windows.hpp>
#include <calf/platform/windows/system_services.hpp>

#include <functional>
#include <sstream>
#include <iostream>

class EchoClient {
public:
  EchoClient()
    : pipe_service_(
          L"\\\\.\\pipe\\nd101ppt", 
          calf::io_mode::open),
      thread_(&calf::pipe_message_service::run, &pipe_service_) {}

  void Run() {
    auto& channel = pipe_service_.create_channel(
      std::bind(&EchoClient::MessageHandler, this, std::placeholders::_1));
    auto message = std::make_unique<calf::pipe_message>(1, std::string("{\"methodId\": 1, \"methodName\": \"getUserInfo\"}"));
    channel.send_message(std::move(message));
    thread_.join();
  }

  void MessageHandler(calf::pipe_message_channel& channel) {
    auto message = channel.receive_message();
    while(message) {
      std::cerr << "id=" << message->head()->id << " size=" << message->head()->size << 
          " data=" << std::string(reinterpret_cast<char*>(message->data()), message->head()->size) << std::endl;
      message = channel.receive_message();
    }
  }

private:  
  calf::pipe_message_service pipe_service_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoClient client;
  client.Run();
  return 0;    
}