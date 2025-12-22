#include "../source/Server.hpp"

using namespace std;

int main()
{
    Socket server;
    server.CreateServer(8080);
    
    int newfd = server.Accept();
    Socket newcll(newfd);

    char buffer[1024];
    std::string str;
    int len = newcll.Recv((void*)buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';

    str = buffer;
    cout << str << endl;

    len = newcll.Send((const void*)str.c_str(), str.size());
    cout << len << endl;
    server.Close();

    return 0;
}