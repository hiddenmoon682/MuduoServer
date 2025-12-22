#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>

#define INF 0
#define DBG 1
#define ERR 2
#define LOG_LEVEL DBG

#define LOG(level, format, ...) do{\
        if (level < LOG_LEVEL) break;\
        time_t t = time(NULL);\
        struct tm *ltm = localtime(&t);\
        char tmp[32] = {0};\
        strftime(tmp, 31, "%H:%M:%S", ltm);\
        fprintf(stdout, "[%p %s %s:%d] " format "\n", (void*)pthread_self(), tmp, __FILE__, __LINE__, ##__VA_ARGS__);\
    }while(0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)


#define BUFFER_DEFAULT_SIZE 1024

class Buffer
{
private:
    std::vector<char> _buffer;  // 使用vector进行内存空间管理
    uint64_t _reader_idx;       // 读偏移
    uint64_t _writer_idx;       // 写偏移

private:
    // 获取buffer空间起始地址
    char* Begin() { return &(*_buffer.begin()); }

    // 获取当前写入位置起始地址
    char* WritePosition() { return Begin() + _writer_idx; }

    // 获取当前读位置起始地址
    char* ReadPosition() { return Begin() + _reader_idx; }

    // 获取缓冲区末尾空闲空间大小--也就是写偏移到末尾的大小
    uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }

    // 获取缓冲区起始空闲大小--也就是读偏移到起始位置的大小
    uint64_t HeadIdleSize() { return _reader_idx; }

    // 获取可读数据大小 = 写偏移 - 读偏移
    uint64_t ReadAbleSize() { return _writer_idx - _reader_idx; }

    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if(len == 0) return;
        // 向后移动的大小，必须小于可读数据大小
        assert(len <= ReadAbleSize());
        _reader_idx += len;
    }

    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {   
        if(len == 0) return;
        // 向后移动的大小必须小于末尾空间大小
        assert(len <= TailIdleSize());
        _writer_idx += len;
    }

    // 确保可写空间足够（整体空间够了就移动数据，不够就扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        // 如果末尾空间足够，就之间返回
        if(len <= TailIdleSize()) return;
        // 末尾空间不够，判断加上起始部分空闲空间是否足够，足够就移动位置
        if(len <= TailIdleSize() + HeadIdleSize())
        {
            // 将数据移动到起始位置
            uint64_t rsz = ReadAbleSize();  // 先把当前数据大小存起来
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin());   //把数据全都先前挪
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            // 总体空间不够，需要扩容, 就扩大到足够添加数据的长度
            _buffer.resize(_writer_idx + len);
        }
    }

    // 写入未知类型数据
    void Write(const void* data, uint64_t len)
    {
        // 1. 保证有足够空间
        if(len == 0) return;
        EnsureWriteSpace(len);
        // 2. 拷贝数据
        const char* d = (const char*)data;
        std::copy(d, d + len, WritePosition());
    }

    // 写入字符串数据
    void WriteString(const std::string& data)
    {
        Write(data.c_str(), data.size());
    }

    // 写入/拷贝其他的Buffer缓冲区
    void WriteBuffer(Buffer& data)
    {
        Write(data.ReadPosition(), data.ReadAbleSize());
    }

    // 查找换行符
    char* FindCRLF()
    {
        char* res = (char*)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

public:
    Buffer()
        :_reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE)
    {}
    // 写入数据并更新偏移
    void WriteAndPush(const void* data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }

    // 写入字符串并更新偏移
    void WriteStringAndPush(const std::string& data)
    {
        WriteString(data);
        MoveWriteOffset(data.size());
    }

    // 写入字符串并更新偏移
    void WriteBufferAndPush(Buffer& data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadAbleSize());
    }

    // 读取数据
    void Read(void* buf, uint64_t len)
    {
        // 要求获取的数据大小小于可读数据大小
        assert(len <= ReadAbleSize());
        // 拷贝数据
        std::copy(ReadPosition(), ReadPosition() + len, (char*)buf);
    }

    // 读取数据并弹出
    void ReadAndPop(void* buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    // 以字符串形式读取
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);
        Read(&str[0], len);
        return str;
    }

    // 以字符串形式读取并弹出
    std::string ReadAsStringAndPop(uint64_t len)
    {
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }

    // 以字符串形式，获取一行数据
    std::string Getline()
    {
        // 找到换行符就停下作为一行
        char* pos = FindCRLF();
        if(pos == nullptr) return "";
        // +1 是为了把换行符也给取出来
        return ReadAsString(pos - ReadPosition() + 1);
    }

    std::string GetlineAndPop()
    {
        std::string str = Getline();
        MoveReadOffset(str.size());
        return str;
    }

    // 清空缓冲区
    void Clear()
    {
        // 只需要将偏移量归0即可
        _reader_idx = 0;
        _writer_idx = 0;
    }
};



#define MAX_LISTEN 1024
class Socket
{
private:
    int _sockfd;    // 套接字文件描述符

private:
    // 创建套接字
    bool Create()
    {
        // int socket(int domain, int type, int protocol)
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(_sockfd < 0)
        {
            ERR_LOG("CREATE SOCKET FAILED!");
            return false;
        }
        return true;
    }

    // 绑定地址信息
    bool Bind(const std::string& ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &(addr.sin_addr));
        socklen_t len = sizeof(struct sockaddr_in);
        
        int ret = bind(_sockfd, (struct sockaddr*)&addr, len);
        if(ret < 0)
        {
            ERR_LOG("BIND ADDRESS FAILED!");
            return false;
        }
        return true;
    }

    // 开始监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        // int listen(int blacklog)
        int ret = listen(_sockfd, backlog);
        if(ret < 0)
        {
            ERR_LOG("SOCKET LISTEN FAILED!");
            return false;
        }
        return true;
    }

    // 向服务器发起连接
    bool Connect(const std::string& ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &(addr.sin_addr));
        socklen_t len = sizeof(struct sockaddr_in);
        
        int ret = connect(_sockfd, (struct sockaddr*)&addr, len);
        if(ret < 0)
        {
            ERR_LOG("CONNECT ADDRESS FAILED!");
            return false;
        }
        return true;
    }

    // 设置套接字属性 为非阻塞
    bool NonBlock()
    {
        int flags = fcntl(_sockfd, F_GETFL, 0); // 先获取当前标志
        flags |= O_NONBLOCK;                    // 添加非阻塞标志
        if (fcntl(_sockfd, F_SETFL, flags) == -1) 
        {
            // 错误处理
            ERR_LOG("SET NONBLOCK FAILED!");
            return false;
        }
        // 现在 sockfd 就是非阻塞的了
        return true;
    }

    // 启用地址和端口重用
    void ReuseAddress()
    {
        // int setsockopt(int fd, int leve, int optname, void *val, int vallen)
        // 允许地址复用，方便快速重启
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&val, sizeof(int));
        // 允许端口复用，多个套接字可以绑定同一个端口，用于负载均衡
        val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, (void*)&val, sizeof(int));
    }

public:
    Socket(): _sockfd(-1) {}
    Socket(int fd): _sockfd(fd) {}
    ~Socket() { Close(); }

    // 获取新连接
    int Accept()
    {
        int newfd = accept(_sockfd, nullptr, nullptr);
        if(newfd < 0)
        {
            ERR_LOG("SOCKET ACCEPT FAILED!");
            return false;
        }
        return newfd;
    }

    // 阻塞等待接受数据
    // ssize_t 是有符号整型
    ssize_t Recv(void* buf, size_t len, int flag = 0)
    {
        if(len == 0) return 0;
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if(ret <= 0)
        {
            if(errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("SOCKET RECV FAILED");
            return -1;
        }
        return ret;
    }

    // 非阻塞接受数据
    ssize_t NonBlockRecv(void* buf, size_t len)
    {
        // MSG_DONTWAIT 表示当前接受为非阻塞
        return Recv(buf, len, MSG_DONTWAIT);
    }

    // 发送数据
    ssize_t Send(const void* buf, size_t len, int flag = 0)
    {
        if(len == 0) return 0;
        ssize_t ret = send(_sockfd, buf, len, flag);
        if(ret < 0)
        {
            if(errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            ERR_LOG("SOCKET SEND FAILED!");
            return -1;
        }
        return ret;
    }

    // 非阻塞发送数据
    ssize_t NonBlockSend(void* buf, size_t len)
    {
        return Send(buf, len, MSG_DONTWAIT);
    }

    // 关闭套接字
    void Close()
    {
        if(_sockfd != -1) 
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }

    // 创建一个服务器端
    bool CreateServer(uint16_t port, bool block_flag = false, const std::string& ip = "0.0.0.0")
    {
        // 1. 创建套接字 2. 绑定地址 3. 开始监听 4. 设置非阻塞 5. 启动地址重用
        if(Create() == false) return false;
        if(block_flag && NonBlock() == false) return false;;
        ReuseAddress();
        if(Bind(ip, port) == false) return false;
        if(Listen() == false) return false;
        return true;
    }

    // 创建一个客户端
    bool CreateClient(const std::string& ip, uint16_t port)
    {
        // 1. 创建套接字 2. 指向连接服务器
        if(Create() == false) return false;
        if(Connect(ip, port) == false) return false;
        return true;
    }
};