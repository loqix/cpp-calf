#include <calf/platform/windows.hpp>

#include <functional>
#include <sstream>
#include <iostream>

class EchoServer {
public: 
  EchoServer()
    : pipe_service_(
          L"\\\\.\\pipe\\echo_server", 
          calf::io_mode::create),
      thread_(&calf::pipe_message_service::run, &pipe_service_) {}

  void Run() {
    pipe_service_.create_channel(
      std::bind(&EchoServer::MessageHandler, this, std::placeholders::_1));
    thread_.join();
  }

  void MessageHandler(calf::pipe_message_channel& channel) {
    auto message = channel.receive_message();
    while(message) {
      std::cerr << "id=" << message->head()->id << " size=" << message->head()->size << 
          " data=" << std::string(reinterpret_cast<char*>(message->data()), message->head()->size) << std::endl;
      channel.send_message(std::move(message));
      message = channel.receive_message();
    }
  }
  
private:
  calf::pipe_message_service pipe_service_;
  std::thread thread_;
};

int main(int argc, char* argv[]) {
  EchoServer server;
  server.Run();
  return 0;    
}