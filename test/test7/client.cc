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

    int index = 0;
    while(1)
    {
        // std::string str = std::to_string(index);
        // index++;
        // ssize_t ret = client.Send(str.c_str(), str.size());
        // if(ret < 0)
        // {
        //     ERR_LOG("SEND %s FAILED!", str.c_str());
        //     continue;
        // }
        // DBG_LOG("Send %s success", str.c_str());
        // char buf[1024] = {0};
        // client.Recv(buf, sizeof(buf)-1);
        // std::string tstr = buf;
        // DBG_LOG("Recv %s success!!!", tstr.c_str());
        sleep(1);
    }

    return 0;
}
