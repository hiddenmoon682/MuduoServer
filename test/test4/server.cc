#include "../../source/Server.hpp"

void ReadCallback(Socket* sock, Channel* chna)
{
    DBG_LOG("Use ReadCallback");
    std::string str;
    char buf[1024] = {0};
    ssize_t len = sock->Recv(buf, sizeof(buf), MSG_DONTWAIT);
    if(len > 0)
    {
        std::string str(buf);
        std::cout << str << std::endl;
        chna->EnableWrite();
    }
    else if(len <= 0)
    {
        std::cout << "缓冲区数据读取完毕或者对端连接断开" << std::endl;
        chna->Remove();
    }
    else
    {
        std::cout << "数据读取出现问题" << std::endl;
        chna->Remove();
    } 
}

void WriteCallback(Socket* sock, Channel* chna)
{
    DBG_LOG("Use WriteCallback");
    std::string str = "hello client\n";
    ssize_t ret = sock->Send(str.c_str(), str.size());
    if(ret < 0) return;
    chna->DisableWrite();
}

void CloseCallback(Channel* chna)
{
    DBG_LOG("对端连接关闭，取消监控");
    chna->Remove();
    sleep(10);
}

void Accept(Socket* sock, EventLoop* loop)
{
    int newfd = sock->Accept();
    DBG_LOG("Get a new socket");
    Socket* newsock = new Socket(newfd);
    Channel* newchna = new Channel(loop, newfd);
    newchna->EnableRead();

    newchna->SetCloseCallback(std::bind(&CloseCallback, newchna));
    newchna->SetReadCallback(std::bind(&ReadCallback, newsock, newchna));
    newchna->SetWriteCallback(std::bind(&WriteCallback, newsock, newchna));

    loop->TimerAdd(1000, 10, std::bind(&CloseCallback, newchna));
}

// 测试定时任务模块
int main()
{
    Socket server;
    server.CreateServer(8080, true);
    EventLoop loop;

    Channel chna(&loop, server.Fd());
    chna.EnableRead();
    chna.SetReadCallback(std::bind(&Accept, &server, &loop));
    chna.SetCloseCallback(std::bind(&CloseCallback, &chna));


    loop.Start();

    return 0;
}