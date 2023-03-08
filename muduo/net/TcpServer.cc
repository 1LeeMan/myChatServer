// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/TcpServer.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Acceptor.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/EventLoopThreadPool.h"
#include "muduo/net/SocketsOps.h"

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
    threadPool_(new EventLoopThreadPool(loop, name_)),
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
    nextConnId_(1)
{
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    item.second.reset();
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listening());
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  EventLoop* ioLoop = threadPool_->getNextLoop(); //从线程池中获取一个EventLoop（因为EventLoop对应一个线程），这里相当于获取了一个线程
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));



  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary
  // 为当前接收到的连接请求创建一个TcpConnection对象，将TcpConnection对象与分配的（ioLoop）线程绑定
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  now = Timestamp::now();
  double delay = 20.0;
  loop_->runAfter(
        delay,
        std::bind(&TcpConnection::handleCloseforserver, conn));
  conn->setAdjustTimerCallBack(
    std::bind(&TcpServer::adjustTimerCallBack, this, _1)); 

  connections_[connName] = conn;
  // 将（用户定义的）连接建立回调函数、消息接收回调函数注册到TcpConnection对象中
  // 由此可知TcpConnection才是muduo库对与网络连接的核心处理
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_); 
  
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe   

  // 对于新建连接的socket描述符，还需要设置期望监控的事件（POLLIN | POLLPRI），
  // 并且将此socket描述符放入poll函数的监控描述符集合中，用于等待接收客户端从此连接上发送来的消息
  // 这些工作，都是由TcpConnection::connectEstablished函数完成。
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn)); // shared_ptr:2
} //shared_ptr:1

void TcpServer::adjustTimerCallBack(Timestamp IO_now)
{
  double delta = timeDifference(IO_now, now);
  now = IO_now;
  loop_->adjust(delta);
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) //shared_ptr:3
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn)); //放入IO线程里执行去了，保证connections_的erase与create在同一线程
}//shared_ptr:2

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn) //shared_ptr:4
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  // LOG_INFO << "conn count:"<<conn.use_count();
  // printf("conn count:%ld\n",conn.use_count());
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn)); //直接放入其他线程中执行，这一步不能在handleEvent内执行，因为channel的remove不能发生在调用channel的函数里
} //shared_ptr:3

