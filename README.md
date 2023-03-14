# mycharserver

[![Build Status](https://travis-ci.org/linyacool/WebServer.svg?branch=master)](https://travis-ci.org/linyacool/WebServer) [![license](https://img.shields.io/github/license/mashape/apistatus.svg)](https://opensource.org/licenses/MIT)

## Introduction

ChatServer聊天服务器项目：基于muduo网络库实现服务器，一方面接收client的聊天信息并分发给建立连接的所有client，另一方面可通过web访问进行实时所有聊天内容监控。server接收到单client的消息，会广播给所有的client，各client收到消息的顺序是随机的，但消息的顺序一定是固定不变的。因为聊天client有限，使用poll可能比epoll_wait更合适。

## Environment

- OS: Ubuntu 20.04
- Complier: g++ 9.3.0
- Tools: CMake/VScode

## Technical Points
* muduo是静态链接的C++网络库。通过threadpool为客户端与各服务端连接完成资源分配，基于non-blocking IO＋IO multiplexing的Reactor模式下实现one loop per thread的软件架构，以事件驱动（event-driven）和事件回调的方式实现业务逻辑。

* one loop per thread。通过threadpool为通信完成资源分配，每个线程只能有一个EventLoop对象，内部循环执行已注册事件。实现方式：为避免一个loop()被多个线程共享，每次调用loop()通过assertInLoopThread会检查线程号。同时为避免一个线程创建多个loop，首次创建时便会将this对象赋值给判断变量t_loopInThisThread，之后创建的会报错。
EventLoop拥有与线程相同的生命期。

* non-blocking IO + IO multiplexing的reactor。

* 基于std::bind+std::function的基于事件驱动的事件回调来实现业务逻辑。把原来主动调用API实现接收数据完成连接或读取或写出的思路，改为client code注册一个功能回调函数，server库发现注册函数后运行函数并将最终结果反馈给client code。

* channel与loop的observer模式及其针对race condition的解决策略。在muduo中观察者observeable subject对应loop，被观察者observer对应channel。channel负责一个文件描述符的分发，由IO线程的eventLoop控制Channel::handleEvent，根据IO multiplexing响应的事件类型调用不同的回调函数。在多线程环境下，IO线程中loop和channel在同一线程时线程安全；而loop和channel分属不同线程时，需要解决race condition问题，tcpconnecion 的释放导致内部channel释放，若此时正在执行handleEvent，则引发race condition。
对于存放普通对象指针的observeable数据结构，即使其他线程导致该对象的释放(空悬指针)并=null(野指针)，但由于被析构的对象仍然是可访问的，observeable的对象指针仍存在只是指向的内存被释放，此时访问必然core dump（如recipes/thread/Observer_safe_2.cc）。对象的析构并不安全，没有保证不受其他线程的访问。
一个成功策略是使用智能指针代替原始指针：share_ptr是能控制对象的生命周期的强引用指针，所有指针共享引用次数可判断是否立即自动释放。接上面说的，使用shared_ptr代替普通指针，在其他线程企图访问已被析构的shared_ptr对象时被禁止访问，防止race condition中析构带来的问题。muduo对于IO线程中注册的回调handleEvent，本身是线程安全的，正常执行即可；对于其他线程中注册的事件回调可能存在race condition，先用weak_ptr传递tcpconnection，判断tcpconnection是否在生命期内，再用shared_ptr接受传递的tcpconnection对象，防止tcpconnection析构带来IO线程的core dump。

* runInLoop完成function同线程的同步执行/线程间异步执行。一种function在线程间异步执行的方案：临界区function传递（线程1上锁+vector push，线程2上锁+vector swap）+异步调用。不仅上锁的临界区长度被大量缩小，将上锁内容限定在swap上，保证资源的均衡分配；而且大大降低上锁带来死锁的风险。

* 当所有client长时间建立连接却没有IO对话，为避免资源浪费。自各client通话后开始启动定时器计时，根据当前时间和延迟时间建立定时器timer并存入set数据结构，每发生通话都会在set内顺时延长定时器定时时间。当定时时间到达会在所有client所在线程中关闭连接。

* 性能测评:建立连接后server将接收到client的message回写给client，client再次回写给server，直到超时后，client所有连接断开连接为止。记录时间内的吞吐量情况。
|     threadNum     |    sessionCount    |      throughput(MiB/s)    |
----------------------------------------------------------------------
|         1         |         100        |        2137.1571875       |
|         2         |         100        |        3255.46796875      |
|         3         |         100        |        3513.65546875      |
|         4         |         100        |         3304.92875        |
|         1         |         1000       |        1370.72609375      |
|         2         |         1000       |        1622.77046875      |
|         3         |         1000       |        1685.14015625      |
|         4         |         1000       |        1677.07796875      |

* 支持解析HTTP报文的GET请求，并响应聊天信息。
