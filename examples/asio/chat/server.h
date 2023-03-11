
#include <set>
#include <stdio.h>
#include <unistd.h>

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"
#include "examples/asio/chat/codec.h"
#include "muduo/net/http/HttpServer.h"
#include "muduo/net/http/HttpContext.h"
#include "muduo/net/http/HttpRequest.h"
#include "muduo/net/http/HttpResponse.h"

using namespace muduo;
using namespace muduo::net;

class ChatServer : noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& chatlistenAddr,
             const InetAddress& httplistenAddr)
  : server_(loop, chatlistenAddr, "ChatServer"),
    httpServer(loop, httplistenAddr, "httpserver"),
    codec_(std::bind(&ChatServer::onStringMessage, this, _1, _2, _3))
  {
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
    codec_.setMonitorCallback(
        std::bind(&HttpServer::monitorCallback, &httpServer, _1));
  }

  void start()
  {
    server_.setThreadNum(10);
    httpServer.setThreadNum(10);
    server_.start();
    httpServer.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected())
    {
      connections_.insert(conn);
    }
    else
    {
      connections_.erase(conn);
    }
  }


  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
    for (ConnectionList::iterator it = connections_.begin();
        it != connections_.end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
  }
  TcpServer server_;
  HttpServer httpServer;
  LengthHeaderCodec codec_;
  typedef std::set<TcpConnectionPtr> ConnectionList;
  ConnectionList connections_;
};