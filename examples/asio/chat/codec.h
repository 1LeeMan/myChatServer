#ifndef MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
#define MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
#include <string>
#include <climits>
#include "muduo/base/Logging.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/Endian.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/EventLoop.h"

class LengthHeaderCodec : muduo::noncopyable
{
 public:
  typedef std::function<void (const muduo::net::TcpConnectionPtr&,
                                const muduo::string& message,
                                muduo::Timestamp)> StringMessageCallback;
  typedef std::function<void (const muduo::string& message)> MonitorCallback;

  explicit LengthHeaderCodec(const StringMessageCallback& cb)
    : messageCallback_(cb)
  {
  }

  void setMonitorCallback(MonitorCallback cb)
  {
    monitorCallback_ = cb;
  }

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp receiveTime)
  {
    while (buf->readableBytes() >= kHeaderLen) // kHeaderLen == 4
    {
      // FIXME: use Buffer::peekInt32()
      const void* data = buf->peek();
      int32_t be32 = *static_cast<const int32_t*>(data); // SIGBUS
      const int32_t len = muduo::net::sockets::networkToHost32(be32);
      if (len > 65536 || len < 0)
      {
        LOG_ERROR << "Invalid length " << len;
        conn->shutdown();  // FIXME: disable reading
        break;
      }
      else if (buf->readableBytes() >= len + kHeaderLen)
      {
        buf->retrieve(kHeaderLen);
        int threadId = conn->getLoop()->getThreadId();
        minthreadId = (threadId < minthreadId)?threadId: minthreadId;
        muduo::string message(buf->peek(), len);
        muduo::string messagewithuser = "User" + std::to_string(threadId-minthreadId) + ": "+ message + "\n";
        if(monitorCallback_) monitorCallback_(messagewithuser);
        messageCallback_(conn, message, receiveTime);
        buf->retrieve(len);
      }
      else
      {
        break;
      }
    }
  }

  // FIXME: TcpConnectionPtr
  void send(muduo::net::TcpConnection* conn,
            const muduo::StringPiece& message)
  {
    muduo::net::Buffer buf;
    buf.append(message.data(), message.size());
    int32_t len = static_cast<int32_t>(message.size());
    int32_t be32 = muduo::net::sockets::hostToNetwork32(len);
    buf.prepend(&be32, sizeof be32); // 因为buffer的readindex初始化时做好了预留，可以直接插在前面:在每条消息的头部加一个长度字段
    conn->send(&buf);
  }

 private:
  int minthreadId = INT_MAX;
  StringMessageCallback messageCallback_;
  MonitorCallback monitorCallback_;
  const static size_t kHeaderLen = sizeof(int32_t);
};

#endif  // MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
