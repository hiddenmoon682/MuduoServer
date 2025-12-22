#include "../source/Server.hpp"

using namespace std;

int main()
{
    Socket server;
    server.CreateClient("127.0.0.1", 8080);
    
    std::string str = "hello world!";
    int len = server.Send((const void*)str.c_str(), str.size());
    cout << len << endl;

    char buffer[1024];
    len = server.Recv((void*)buffer, sizeof(buffer) - 1);
    cout << len << endl;
    buffer[len] = '\0';

    std::string rsp;
    rsp = buffer;
    cout << rsp << endl;

    server.Close();

    return 0;
}