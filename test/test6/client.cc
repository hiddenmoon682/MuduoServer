#include "../../source/Server.hpp"

int main()
{
    Socket client;
    bool ret = client.CreateClient("127.0.0.1", 8080);
    if( !ret )
    {
        ERR_LOG("CREATE CLIENT FAILED!");
        return -1;
    }

    std::string str = "hello world!";
    int index = 0;
    while(1)
    {
        str += std::to_string(index);
        index++;
        ssize_t ret = client.Send(str.c_str(), str.size());
        if(ret < 0)
        {
            ERR_LOG("SEND %s FAILED!", str.c_str());
            continue;
        }
        DBG_LOG("Send %s success", str.c_str());
        char buf[1024];
        client.Recv(buf, sizeof(buf)-1);
        std::string str(buf);
        std::cout << str << std::endl;
        sleep(1);
    }

    return 0;
}
