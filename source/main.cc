#include "Buffer.hpp"

using namespace std;

int main()
{
    Buffer buff;
    string str = "hello!";
    buff.WriteStringAndPush(str);

    string output;
    output = buff.ReadAsStringAndPop(str.size());
    cout << output << endl;

    return 0;
}