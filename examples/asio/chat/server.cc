#include "examples/asio/chat/server.h"
#include "muduo/net/http/HttpServer.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 2)
  {
    EventLoop loop;
    
    uint16_t chatPort = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress chatServerAddr(chatPort);
    

    uint16_t httpPort = static_cast<uint16_t>(atoi(argv[2]));
    InetAddress httpServerAddr(httpPort);
    // HttpServer httpServer(&loop, httpServerAddr, "httpserver");
    // httpServer.start();
    ChatServer chatServer(&loop, chatServerAddr, httpServerAddr);
    chatServer.start();

    loop.loop();
  }
  else
  {
    printf("Usage: %s port\n", argv[0]);
  }
}

