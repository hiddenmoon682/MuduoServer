#include "../../source/Server.hpp"

std::unordered_map<uint64_t, PtrConnection> conns;     // 连接管理
uint64_t conn_id = 1;                                  // 连接唯一标识符

void MessageCallback(Connection* conn)
{
    DBG_LOG("Use MessageCallback");
    std::string str;
}

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

void Acceptor(const PtrConnection& conn, Buffer* buf)
{
    int newfd = accept(conn->Fd(), nullptr, nullptr);
    if(newfd < 0)
    {
        ERR_LOG("SOCKET ACCEPT FAILED!");
        return;
    }

    PtrConnection new_conn = std::make_shared<Connection>(conn->GetLoop(), conn_id, newfd);
    conns.insert(std::make_pair(conn_id++, new_conn));
    // TODO
    // 设置新连接的回调函数
}

// 测试Connection模块
int main()
{
    Socket server;
    server.CreateServer(8080, true);
    EventLoop loop;

    PtrConnection server_conn = std::make_shared<Connection>(&loop, conn_id++, server.Fd());

    Buffer tmp_buf;
    server_conn->SetMessageCallback(Acceptor);

    loop.Start();

    return 0;
}