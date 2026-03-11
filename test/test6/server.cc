#include "../../source/Server.hpp"

std::unordered_map<uint64_t, PtrConnection> conns;     // 连接管理
uint64_t conn_id = 1;                                  // 连接唯一标识符

EventLoop loop;

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

void AcceptCallback(int newfd)
{
    PtrConnection new_conn = std::make_shared<Connection>(&loop, conn_id, newfd);
    conns.insert(std::make_pair(conn_id++, new_conn));
    INF_LOG("GET NEW CONNECTION");

    new_conn->SetConnectedCallback(ConnectedCallback);
    new_conn->SetMessageCallback(MessageCallback);
    new_conn->Established();
}

// 测试Connection模块
int main()
{
    Acceptor acceptor(&loop, 8080);
    acceptor.SetAcceptCallback(AcceptCallback); 
    acceptor.Listen();

    loop.Start();

    return 0;
}