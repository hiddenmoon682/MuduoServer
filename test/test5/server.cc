#include "../../source/Server.hpp"

std::unordered_map<uint64_t, PtrConnection> conns;     // 连接管理
uint64_t conn_id = 1;                                  // 连接唯一标识符

void MessageCallback(PtrConnection conn, Buffer* buf)
{
    DBG_LOG("Use MessageCallback");
    std::string str = buf->ReadAsStringAndPop(buf->ReadAbleSize());
    std::cout << str << std::endl;
    if(!str.empty()) {
        conn->Send(str.c_str(), str.size());
    }
}

void ConnectedCallback(const PtrConnection& conn)
{
    DBG_LOG("Use ConnectedCallback");
}


void Accept(Socket* sock, EventLoop* loop)
{
    int newfd = accept(sock->Fd(), nullptr, nullptr);
    if(newfd < 0)
    {
        ERR_LOG("SOCKET ACCEPT FAILED!");
        return;
    }
    INF_LOG("ACCEPT NEW CONNECTION %d", newfd);

    PtrConnection new_conn = std::make_shared<Connection>(loop, conn_id, newfd);
    conns.insert(std::make_pair(conn_id++, new_conn));
    INF_LOG("GET NEW CONNECTION");

    new_conn->SetConnectedCallback(ConnectedCallback);
    new_conn->SetMessageCallback(MessageCallback);
    new_conn->Established();
}

// 测试Connection模块
int main()
{
    Socket server;
    server.CreateServer(8080, true);
    EventLoop loop;

    Channel chna(&loop, server.Fd());
    chna.EnableRead();
    chna.SetReadCallback(std::bind(&Accept, &server, &loop));

    loop.Start();

    return 0;
}