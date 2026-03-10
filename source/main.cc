#include "Server.hpp"

using namespace std;

int main()
{
    LoopThread lt;
    EventLoop* loop = lt.GetLoop();

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }

    auto conn = std::make_shared<Connection>(loop, 1, sv[0]);

    conn->SetConnectedCallback([](const PtrConnection& c){
        cout << "Connected: fd=" << c->Fd() << endl;
    });

    conn->SetMessageCallback([&](const PtrConnection& c, Buffer* buf){
        string s = buf->ReadAsStringAndPop(buf->ReadAbleSize());
        cout << "Received: " << s << endl;
        const char* reply = "ACK";
        c->Send(reply, 3);
    });

    conn->SetClosedCallback([](const PtrConnection& c){
        cout << "Closed" << endl;
    });

    conn->Established();

    const char* msg = "hello\n";
    send(sv[1], msg, strlen(msg), 0);

    char rbuf[64];
    ssize_t n = recv(sv[1], rbuf, sizeof(rbuf), 0);
    if (n > 0) {
        cout << "Peer got: " << string(rbuf, n) << endl;
    }

    conn->Shutdown();

    sleep(1);
    close(sv[1]);

    return 0;
}