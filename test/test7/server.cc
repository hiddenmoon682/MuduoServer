#include "../../source/Server.hpp"

// 连接回调函数
void ConnectedCallback(const PtrConnection& conn)
{
    INF_LOG("New connection established: %d", conn->Id());
}

// 消息回调函数
void MessageCallback(const PtrConnection& conn, Buffer* buf)
{
    std::string str = buf->ReadAsStringAndPop(buf->ReadAbleSize());
    // INF_LOG("Received message: %s", str.c_str());
    
    // 回显消息
    conn->Send(str.c_str(), str.size());
    // INF_LOG("Echo message %s sent", str.c_str());
}

// 关闭回调函数
void ClosedCallback(const PtrConnection& conn)
{
    INF_LOG("Connection closed: %d", conn->Id());
}

// 任意事件回调函数
void AnyEventCallback(const PtrConnection& conn)
{
    DBG_LOG("Any event callback triggered for connection: %d", conn->Id());
}

int main()
{
    INF_LOG("Starting TcpServer test...");
    
    // 创建TcpServer实例，监听8080端口
    TcpServer server(8080);
    
    // 设置线程池大小为4
    server.SetThreadCount(4);
    INF_LOG("Thread pool size set to 4");
    
    // 设置回调函数
    server.SetConnectedCallback(ConnectedCallback);
    server.SetMessageCallback(MessageCallback);
    server.SetClosedCallback(ClosedCallback);
    // server.SetAnyEventCallback(AnyEventCallback);
    
    // 启用非活跃连接销毁，超时时间为60秒
    server.EnableInactiveRelease(60);
    INF_LOG("Inactive connection release enabled with 60s timeout");
    
    // 添加一个定时任务，30秒后执行
    server.RunAfter([]() {
        INF_LOG("Timer task executed: 30 seconds have passed");
    }, 30);
    
    INF_LOG("TcpServer started on port 8080");
    INF_LOG("Testing LoopThreadPool with 4 threads");
    INF_LOG("Use 'telnet localhost 8080' to test the server");
    
    // 启动服务器
    server.Start();
    
    return 0;
}