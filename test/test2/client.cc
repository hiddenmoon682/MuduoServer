#include "../../source/Server.hpp"

int main()
{
    Socket client;
    client.CreateClient("127.0.0.1", 8080);

    std::string str = "hello world!";
    int index = 0;
    while(1)
    {
        str += std::to_string(index);
        index++;
        client.Send(str.c_str(), str.size());
        DBG_LOG("Send %s success", str.c_str());
        char buf[1024];
        client.Recv(buf, sizeof(buf)-1);
        std::string str(buf);
        std::cout << str << std::endl;
        sleep(1);
    }

    return 0;
}
