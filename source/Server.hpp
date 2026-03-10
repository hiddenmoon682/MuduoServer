#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <unistd.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
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

public:
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
    int Fd() { return _sockfd; }
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
            // 没有读取到数据
            if(errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            // ret==0
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
        ReuseAddress(); // 在bind之前调用，否则无效
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


class Poller;
class EventLoop;
// 事件和套接字管理
class Channel
{
private:
    int _fd;                                        // 套接字描述符
    EventLoop* _loop;
    uint32_t _events;                               // 当前需要监控的事件
    uint32_t _revents;                              // 当前连接触发的事件
    using EventCallback = std::function<void()>;    // 回调函数类型
    EventCallback _read_callback;       // 可读事件被触发的回调函数
    EventCallback _write_callback;      // 可写事件被触发的回调函数
    EventCallback _error_callback;      // 错误事件被触发的回调函数
    EventCallback _close_callback;      // 连接断开事件被触发的回调函数
    EventCallback _event_callback;      // 任意事件被触发的回调函数
public:
    Channel(EventLoop* loop, int fd)
        :_fd(fd), _events(0), _revents(0), _loop(loop)
    {}

    int Fd() { return _fd; }                                // 获取套接字描述符
    uint32_t Events() { return _events; }                   // 获取想要监控的事件
    void SetRevents(uint32_t events) { _revents = events; } // 设置实际就绪的事件
    
    // 设置对应的回调函数
    void SetReadCallback(const EventCallback& cb)  { _read_callback = cb; }
    void SetWriteCallback(const EventCallback& cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback& cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback& cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback& cb) { _event_callback = cb; }

    // 移除事件监控
    void Remove();
    // 更新事件监控
    void Update();

    // 当前是否监控了可读
    bool ReadAble()    { return (_events & EPOLLIN); }
    // 当前是否监控了可写
    bool WriteAble()   { return (_events & EPOLLOUT); }

    // 启动读事件监控
    void EnableRead()  { _events |= EPOLLIN; Update(); }
    // 启动写事件监控
    void EnableWrite() { _events |= EPOLLOUT; Update(); }

    // 关闭读事件监控
    void DisableRead() { _events &= ~EPOLLIN; Update(); }
    // 关闭写事件监控
    void DisableWrite(){ _events &= ~EPOLLOUT; Update(); }
    // 关闭所有事件监控
    void DisableAll()  { _events = 0; Update(); }

    // 事件处理，一旦连接触发了事件，就调用这个函数，自己触发了什么事件如何处理自己决定
    void HandleEvent()
    {
        if((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            // 不管任何事件，都调用的回调函数
            if(_read_callback) _read_callback();
        }
        // 有可能会是否的连接的操作，一次只处理一个
        if(_revents & EPOLLOUT) 
        {
            if(_write_callback) _write_callback();
        }
        else if(_revents & EPOLLERR)
        {
            if(_error_callback) _error_callback();
        }
        else if(_revents & EPOLLHUP)
        {
            if(_close_callback) _close_callback();
        }
        if(_event_callback) _event_callback();
    }
};


#define MAX_EPOLLEVENTS 1024
class Poller
{
private:
    int _epfd;                                      // epoll 文件描述符
    struct epoll_event _evs[MAX_EPOLLEVENTS];       // epoll_event数组
    std::unordered_map<int, Channel*> _channels;    // 套接字描述符-->监控事件管理 的映射
private:
    // 对 epoll 的直接操作
    void Update(Channel* channel, int op)
    {
        // int epoll_ctl(int epfd, int op, int fd, struct epoll_event* evs)
        // 要管理的套接字描述符fd
        int fd = channel->Fd();
        // 设置要监控的事件
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        // op 是控制增加/删除...操作
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
            ERR_LOG("EPOLLCTL FAILED!");
        
        return;
    }

    // 判断一个Channel是否已经添加了事件监控
    bool HasChannel(Channel* channel)
    {
        auto it = _channels.find(channel->Fd());
        if(it == _channels.end())
            return false;
        
        return true;
    }
public:
    Poller()
    {
        // 创建epoll实例，返回对应描述符
        _epfd = epoll_create1(0);
        if(_epfd < 0)
        {
            ERR_LOG("EPOLL CREATE FAILED!");
            abort();    // 创建失败则退出程序
        }
    }

    // 添加或修改监控事件
    void UpdateEvent(Channel* channel)
    {
        // 判断是否存在channel事件管理
        bool ret = HasChannel(channel);
        if(ret == false)
        {
            // 不存在则创建
            _channels.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        // 修改事件管理
        return Update(channel, EPOLL_CTL_MOD);
    }

    // 移植监控
    void RemoveEvent(Channel* channel)
    {
        auto it = _channels.find(channel->Fd());
        if(it != _channels.end()) {
            _channels.erase(it);
        }
        // 删除事件管理
        Update(channel, EPOLL_CTL_DEL);
    }

    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel*>* active)
    {
        // int epoll_wait(int epfd, struct epoll_event* evs, int maxevents, int timeout)
        // -1 表示阻塞等待
        // _evs表示的是有事件响应的套接字数组
        // nfds是有事件就绪的文件描述符的数量
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if(nfds < 0) {
            if(errno == EINTR) {
                return;
            }
            ERR_LOG("EPOLL WAIT ERROR:%s\n", strerror(errno));
            abort();// 退出程序
        }
        for(int i = 0; i < nfds; i++)
        {
            // _evs[i].data.fd 是 有响应事件的套接字文件描述符
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            // 设置实际就绪的事件
            it->second->SetRevents(_evs[i].events);
            active->push_back(it->second);
        }
        return;
    }
};


using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask
{
private:
    uint64_t _id;           // 定时任务ID
    uint32_t _timeout;      // 定时任务的超时时间
    bool _canceled;         // false表示任务没有被取消，true表示取消
    TaskFunc _task_cb;      // 定时任务对象要执行的任务
    ReleaseFunc _release;   // 用于删除TimerWheel中保存的定时器对象信息
public:
    TimerTask(uint64_t id, uint32_t delay, const TaskFunc& task)
        :_id(id), _timeout(delay), _task_cb(task), _canceled(false)
    {}

    // 时间轮实现的重要组成
    // 1. 在析构函数时调用要执行的任务
    ~TimerTask()
    {
        if(_canceled == false) _task_cb();
        _release();
    }

    // 取消任务的执行
    void Cancel() { _canceled = true; }
    // 设置释放函数
    void SetRelease(const ReleaseFunc& cb) { _release = cb; }
    
    uint32_t DelayTime() { return _timeout; }
};

class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int _tick;              // 当前的秒针，走到哪里就释放到哪里；释放哪里，就相当于执行哪里的任务
    int _capacity;          // 表盘的最大数量，其实就是最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;       // 任务时间轮
    std::unordered_map<uint64_t, WeakTask> _timers; // 任务ID到任务的映射

    EventLoop* _loop;       // 事件管理
    int _timerfd;           // 定时器描述符--可读事件回调就是读取计数器，执行定时任务
    std::unique_ptr<Channel> _timer_channel;    // 对于定时器描述符的事件管理
private:
    // 从timer中删除，因为析构之后无法获取对应对象，所以释放函数也设置为回调函数放在析构函数中
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if(it != _timers.end())
        {
            _timers.erase(it);
        }
    }

    static int CreateTimerFd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if(timerfd < 0)
        {
            ERR_LOG("TIMERFD CREATE FAILED!");
            abort();
        }
        struct itimerspec itime;
        itime.it_value.tv_sec = 1;
        itime.it_value.tv_nsec = 0;
        itime.it_interval.tv_sec = 1;
        itime.it_interval.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, nullptr);
        return timerfd;
    }

    int ReadTimefd()
    {
        uint64_t times;
        // 有可能因为其他描述符的事件处理花费事件比较长，然后在处理定时器描述符事件的事件，有可能就已经超时了很多次
        // read读取到的数据times就是从上一次read之后的超时的次数
        int ret = read(_timerfd, &times, 8);
        if(ret < 0)
        {
            ERR_LOG("READ TIMEFD FAILED!");
            abort();
        }
        return times;
    }

    // 这个函数应该每秒钟被执行一次，相当于秒钟向后走一步
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();  // 清空对应位置数组，就会把数组中管理的所有的shared_ptr释放掉
    }

    void OnTime()
    {
        // 根据实际超时次数，执行对应的超时任务
        int times = ReadTimefd();
        for(int i = 0; i < times; i++)
        {
            RunTimerTask();
        }
    }

    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc& cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }

    void TimerRefreshInLoop(uint64_t id)
    {
        // 通过保存的定时器对象的weak_ptr构造一个shared_ptr出来，添加到轮子中
        auto it = _timers.find(id);
        if(it == _timers.end()) { return; }

        PtrTask pt = it->second.lock(); // lock获取weak_ptr管理的对象对应的shared_ptr
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end()) { return; } //没找着定时任务，没法刷新，没法延迟
        PtrTask pt = it->second.lock();
        if(pt) pt->Cancel();
    }
public:
    TimerWheel(EventLoop* loop)
        :_capacity(60), _tick(0), _wheel(_capacity), _loop(loop),
        _timerfd(CreateTimerFd()), _timer_channel(new Channel(_loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead();   // 启动读事件监控
    }

    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& cb);

    // 刷新/延迟任务
    void TimerRefresh(uint64_t id);

    void TimerCancel(uint64_t id);

    // 这个接口存在线程安全问题--这个接口实际不能被外界使用者调用，只能在模块内，在对应的EventLoop线程内执行
    bool HasTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end())
            return false;
        
        return true;
    }
};


class EventLoop
{
private:
    using Functor = std::function<void()>;
    std::thread::id _thread_id;         // 线程ID
    int _event_fd;                      // eventfd唤醒IO事件监控有可能导致的阻塞
    std::unique_ptr<Channel> _event_channel;    // eventfd描述符的事件管理
    Poller _poller;                     // 进行所有 套接字描述符的事件管理
    std::vector<Functor> _tasks;        // 任务池
    std::mutex _mutex;                  // 实现任务池操作的线程安全
    TimerWheel _timer_wheel;            // 定时器模块
private:
    // 执行任务池中的所有任务
    void RunAllTask() 
    {
        std::vector<Functor> functor;
        {
            // 直接置换出来，方便后续任务插入
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor);
        }
        // 执行任务
        for(auto& f : functor)
        {
            f();
        }
        return;
    }

    // 创建eventfd，用于 事件通知
    static int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if(efd < 0) 
        {
            ERR_LOG("CREATE EVENTFD FAILED!");
            abort();    // 让程序异常退出
        }
        return efd;
    }

    // 从eventfd中读取，计数器清零
    void ReadEventFd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if(ret < 0)
        {
            // EINTR -- 被信号打断； EAGAIN -- 表示无数据可读
            if(errno == EINTR || errno == EAGAIN) 
                return;
            // 读取失败
            ERR_LOG("READ EVENTFD FAILED");
            abort();
        }
    }

    // 向eventfd中写入，计数器增加，进行事件通知
    void WakeUpEventFd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof(val));
        if(ret < 0)
        {
            if(errno == EINTR)
                return;
            // 写入失败
            ERR_LOG("READ EVENTFD FAILED");
            abort();
        }
        return;
    }

public:
    EventLoop()
        :_thread_id(std::this_thread::get_id()), 
        _event_fd(CreateEventFd()),
        _event_channel(new Channel(this, _event_fd)),
        _timer_wheel(this)
    {
        // 给eventfd添加可读事件回调函数，读取eventfd事件通知次数
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventFd, this));
        // 启动eventfd的读事件监控
        _event_channel->EnableRead();
    }

    // 三步走-- 事件监控->就绪事件处理->执行任务
    void Start()
    {
        while(1)
        {
            // 1. 事件监控
            std::vector<Channel*> actives;
            _poller.Poll(&actives);
            // 2. 事件处理
            for(auto& channel : actives)
            {
                channel->HandleEvent();
            }
            // 3. 执行任务
            RunAllTask();
        }
    }

    // 用于判断当前线程是否是EventLoop对应的线程
    bool IsInLoop()
    {
        return (_thread_id == std::this_thread::get_id());
    }
    void AssertInLoop()
    {
        assert(_thread_id == std::this_thread::get_id());
    }

    // 判断将要执行的任务是否处于当前线程中，如果是则执行，不是则压入队列
    void RunInLoop(const Functor& cb)
    {
        if(IsInLoop()) {
            return cb();
        }
        return QueueInLoop(cb);
    }

    // 将操作压入任务池
    void QueueInLoop(const Functor& cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        // 唤醒有可能因没有事件就绪，而导致的epoll阻塞
        // 其实就是给eventfd写入一个数据，eventfd就会触发可读事件
        WakeUpEventFd();
    }

    // 添加/修改 描述符的事件监控
    void UpdateEvent(Channel* channel) { return _poller.UpdateEvent(channel); }
    // 移除 描述符的监控
    void RemoveEvent(Channel* channel) { return _poller.RemoveEvent(channel); }

    // 增加定时任务：任务ID、定时时间、执行函数
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& cb)
    {
        return _timer_wheel.TimerAdd(id, delay, cb);
    }

    // 刷新定时时间
    void TimerRefresh(uint64_t id)
    {
        return _timer_wheel.TimerRefresh(id);
    }

    // 取消定时任务
    void TimerCancel(uint64_t id)
    {
        return _timer_wheel.TimerCancel(id);
    }

    // 判断是否存在对应的定时任务
    bool HasTimer(uint64_t id)
    {
        return _timer_wheel.HasTimer(id);
    }
    
};

// 相当于封装了一个EventLoop的线程
class LoopThread
{
private:
    /*用于实现_loop获取的同步关系，避免线程创立了但是_loop还没有实例化之前去获取_loop*/
    std::mutex _mutex;                  // 互斥锁
    std::condition_variable _cond;      // 条件变量
    EventLoop* _loop;                   // EventLoop指针变量，这个对象需要在线程内实例化
    std::thread _thread;                // EventLoop对应的线程
private:
    // 实例化 EventLoop对象，唤醒_cond上有可能阻塞的线程，并且开始运行EventLoop模块的功能
    void ThreadEntry()
    {
        EventLoop loop;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;
            _cond.notify_all();
        }
        loop.Start();
    }

public:
    // 创建线程，设定线程入口函数
    LoopThread()
        :_loop(NULL),
        _thread(std::thread(&LoopThread::ThreadEntry, this))
    {}

    // 返回当前线程关联的EventLoop对象指针
    EventLoop* GetLoop()
    {
        EventLoop* loop = NULL;
        {
            std::unique_lock<std::mutex> lock(_mutex);// 加速
            _cond.wait(lock, [&](){ return _loop != NULL; });// loop为NULL就一直阻塞
            loop = _loop;
        }
        return loop;
    }
};

// 在这里实现避免前向声明的EventLoop类无法调用对应的方法
// 移除事件监控
void Channel::Remove() { return _loop->RemoveEvent(this); }
// 更新事件监控
void Channel::Update() { return _loop->UpdateEvent(this); }

// 添加定时任务
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& cb)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

// // 刷新/延迟任务
void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}

void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInLoop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

// 仿照实现std::any类, 用于保存上下文信息
class Any
{
private:
    class holder
    {
    public:
        virtual ~holder() {}
        virtual const std::type_info& type() = 0;
        virtual holder* clone() = 0;
    };

    template<class T>
    class placeholder: public holder
    {
    public:
        placeholder(const T& val): _val(val) {}
        // 获取子类对象保存的数据类型
        virtual const std::type_info& type() { return typeid(T); }

        virtual holder* clone() { return new placeholder(_val); }
    public:
        T _val;
    };

    holder* _content;

public:
    Any():_content(nullptr) {}

    template<class T>
    Any(const T& val):_content(new placeholder<T>(val)) {}

    Any(const Any& other):_content(other._content ? other._content->clone() : nullptr) {}

    ~Any() { delete _content; }

    Any& swap(Any& other)
    {
        std::swap(_content, other._content);
        return *this;
    }

    template<class T>
    T* get()
    {
        // 想要获取的数据类型必须和保存的数据类型一致
        assert(typeid(T) == _content->type());
        return &(((placeholder<T>*)_content)->_val);
    }

    Any& operator=(const Any& other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

class Connection;
// 连接所有状态
// DISCONNECTED -- 连接关闭状态
// CONNECTING   -- 连接建立成功-待处理状态
// CONNECTED    -- 连接建立完成-各种设置已经就绪，可以通信的状态
// DISCONNECTING-- 连接待关闭状态
typedef enum
{
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
} ConnStatus;

// Connection 就是封装Socket和Channel，对套接字操作和事件监控进行统一管理
using PtrConnection = std::shared_ptr<Connection>;
class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _conn_id;      // 连接的唯一ID，用于连接的管理和查找
    int _sockfd;            // 连接的文件描述符
    bool _enable_inactive_release;  // 连接是否启动非活跃销毁的判断标志，默认为false
    EventLoop* _loop;       // 连接所关联的EventLoop
    ConnStatus _status;     // 连接当前状态
    Socket _socket;         // 套接字操作管理
    Channel _channel;       // 连接的事件管理
    Buffer _in_buffer;      // 输入缓冲区--存放从socket中读取到的数据
    Buffer _out_buffer;     // 输出缓冲区--存放要发送给对端的数据
    Any _context;           // 请求的接收处理上下文

    /*这四个回调函数，是让服务器模块来设置的（其实服务器模块的处理回调也是组件使用者设置的）*/
    /*换句话说，这几个回调都是组件使用者使用的*/
    using ConnectedCallback = std::function<void(const PtrConnection&)>;
    using MessageCallback = std::function<void(const PtrConnection&, Buffer *)>;    // Buffer是输出参数
    using ClosedCallback = std::function<void(const PtrConnection&)>;
    using AnyEventCallback = std::function<void(const PtrConnection&)>;
    ConnectedCallback _connected_callback;  // 连接建立回调
    MessageCallback _message_callback;      // 消息事件回调
    ClosedCallback _closed_callback;        // 对端连接关闭回调
    AnyEventCallback _event_callback;       // 任意事件回调
    /*组件内的连接关闭回调--组件内设置的，因为服务器组件内会把所有的连接管理起来，一旦某个连接要关闭*/
    /*就应该从管理的地方移除掉自己的信息*/
    ClosedCallback _server_closed_callback;

private:
    /*五个channel的事件回调函数，在外卖封装了一层数据缓冲区*/
    //描述符可读事件触发后调用的函数，接收socket数据放到接收缓冲区中，然后调用_message_callback
    void HandleRead()
    {
        // 1. 接收socket的数据，放到缓冲区
        char buf[65536];
        ssize_t ret = _socket.NonBlockRecv(buf, 65536);
        if(ret < 0) {
            // 接收出错，不能直接关闭连接
            return ShutdownInLoop();
        }
        // 这里ret==0表示没有读取到数据，并非连接断开，NonBlockRecv的连接断开返回的是-1
        // 将数据放入输入缓冲区，写入之后顺便将写偏移向后移动
        _in_buffer.WriteAndPush(buf, ret);
        // 2. 调用message_callback进行业务处理
        if(_in_buffer.ReadAbleSize() > 0)
        {
            // shared_from_this -- 从当前对象自身获取自身的shared_ptr管理对象
            return _message_callback(shared_from_this(), &_in_buffer);
        }
    }

    // 描述符可写事件触发后调用的函数，将发送缓冲区的数据进行发送
    void HandleWrite()
    {
        // _out_buffer中保存的数据就是要发送的数据
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadAbleSize());
        if(ret < 0) 
        {
            // 发送错误则应该关闭连接
            // 先把对应连接的缓冲区中的数据处理完毕
            if(_in_buffer.ReadAbleSize() > 0) 
            {
                _message_callback(shared_from_this(), &_in_buffer);
            }
            // 实际的关闭连接操作
            return Release();
        }
        // 读偏移向后移动
        _out_buffer.MoveReadOffset(ret);
        if(_out_buffer.ReadAbleSize() == 0)
        {
            // 没有数据要发送了，关闭写事件监控，否则会因为写缓冲区中有空间可而一直触发
            _channel.DisableWrite();    
            // 如果当时连接是连接待关闭状态，则有数据，发送完数据后就释放连接，没有数据则直接释放
            if(_status == DISCONNECTING)
            {
                return Release();
            }
        }
        return;
    }

    // 描述符触发挂断事件
    void HandleClose()
    {
        // 一旦连接挂断，那么套接字就已经操作不了了。处理一下缓冲区的数据，然后就关闭就行
        if(_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        return Release();
    }

    // 描述符触发出错事件
    void HandleError()
    {
        return HandleClose();
    }

    // 描述符触发任意事件：1. 刷新连接的活跃度--(避免超时自动断开连接) 2. 调用组件使用者的任意事件回调
    void HandleEvent()
    {
        if(_enable_inactive_release == true) { _loop->TimerRefresh(_conn_id); };
        if(_event_callback) { _event_callback(shared_from_this()); }
    }

    // 连接获取之后，所处的状态下要进行各种设置(启动读监控，调用回调函数)
    void EstablishedInLoop()
    {
        // 1. 修改连接状态； 2. 启动读事件监控； 3. 调用回调函数
        assert(_status == CONNECTING);  // 当前状态应该是连接建立中
        _status = CONNECTED;
        // 一旦启动读事件监控就有可能立即触发事件，如果这时候启动了非活跃连接销毁
        // 可能会导致处理完事件之后直接超时然后销毁？
        _channel.EnableRead();
        if(_connected_callback) _connected_callback(shared_from_this());
    }

    // 实际释放连接的接口
    void ReleaseInLoop()
    {
        // 1. 修改连接状态，将其设置为DISCONNECTED
        _status = DISCONNECTED;
        // 2. 移除连接的事件监控
        _channel.Remove();
        // 3. 关闭描述符
        _socket.Close();
        // 4. 如果当前定时器队列中还有定时销毁任务，则取消任务
        if(_loop->HasTimer(_conn_id)) { CancelInactiveReleaseInLoop(); }
        // 5. 调用关闭连接回调函数，避免先移除服务器管理的连接信息导致Connection被释放，再去处理会出错
        if(_closed_callback) _closed_callback(shared_from_this());
        // 移除服务器内部管理的连接信息
        if(_server_closed_callback) _server_closed_callback(shared_from_this());
    }

    // 这个接口并不是真正的发送接口，而只是把数据放到了发送缓冲区中，然后启动可写事件监控
    void SendInLoop(Buffer& buf)
    {
        if(_status == DISCONNECTED) return;
        _out_buffer.WriteBufferAndPush(buf);
        if(_channel.WriteAble() == false)
        {
            _channel.EnableWrite();
        }
    }

    // 这个关闭连接操作并非事件的连接释放操作，需要判断还有没有数据待处理/待发送
    void ShutdownInLoop()
    {
        _status = DISCONNECTING;    // 状态设置为半关闭状态
        if(_in_buffer.ReadAbleSize() > 0) 
        {
            if(_message_callback) _message_callback(shared_from_this(), &_in_buffer);
        }
        // 要么就是写入数据的时候出错关闭，要么就是没有待发送数据，直接关闭
        if(_out_buffer.ReadAbleSize() > 0)
        {
            if(_channel.WriteAble() == false)
            {
                _channel.EnableWrite();
            }
        }
        if(_out_buffer.ReadAbleSize() == 0)
        {
            Release();
        }

    }

    // 启动非活跃连接超时释放规则
    void EnableInactiveReleaseInLoop(int sec)
    {
        // 1. 将判断标志 _enable_inactive_release 置为true
        _enable_inactive_release = true;
        // 2. 如果当前定时销毁任务已经存在，那就刷新一下延迟即可
        if(_loop->HasTimer(_conn_id))
        {
            return _loop->TimerRefresh(_conn_id);
        }
        // 3. 如果不存在定时销毁任务则新增
        _loop->TimerAdd(_conn_id, sec, std::bind(&Connection::Release, this));
    }

    // 取消非活跃连接超时释放规则
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if(_loop->HasTimer(_conn_id)) { _loop->TimerCancel(_conn_id); }
    }

    // 设置/更新 回调函数
    void UpgradeInLoop(const Any& context,
                    const ConnectedCallback& conn,
                    const MessageCallback& msg,
                    const ClosedCallback& closed,
                    const AnyEventCallback& event)
    {
        _context = context;
        _connected_callback = conn;
        _message_callback = msg;
        _closed_callback = closed;
        _event_callback = event;
    }

public:
    Connection(EventLoop* loop, uint64_t conn_id, int sockfd)
        :_conn_id(conn_id), _sockfd(sockfd), _enable_inactive_release(false),
        _loop(loop), _status(CONNECTING), _socket(_sockfd), _channel(loop, _sockfd)
    {
        _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this));
        _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
        _channel.SetReadCallback(std::bind(&Connection::HandleRead, this));
        _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this));
        _channel.SetErrorCallback(std::bind(&Connection::HandleError, this));
    }

    ~Connection() { DBG_LOG("RELEASE CONNECTION:%p", this); }

    // 获取管理的文件描述符
    int Fd() { return _sockfd; }

    // 获取连接ID
    int Id() { return _conn_id; }

    EventLoop* GetLoop() { return _loop; }

    // 是否处于CONNECTED状态
    bool Connected() { return (_status == CONNECTED); }

    // 设置上下文--连接建立完成时进行调用
    void SetContext(const Any& context) { _context = context; }

    // 获取上下文，返回的是指针
    Any* GetContext() { return &_context; }
    void SetConnectedCallback(const ConnectedCallback& cb) { _connected_callback = cb; }
    void SetMessageCallback(const MessageCallback& cb) { _message_callback = cb; }
    void SetClosedCallback(const ClosedCallback& cb) { _closed_callback = cb; }
    void SetAnyEventCallback(const AnyEventCallback& cb) { _event_callback = cb; }
    void SetSrvClosedCallback(const ClosedCallback& cb) { _server_closed_callback = cb; }

    // 连接建立就绪后，进行channel回调设置，启动读监控，调用_connected_callback
    // TODO
    void Established()
    {
        _loop->RunInLoop(std::bind(&Connection::EstablishedInLoop, this));
    }

    // 发送数据，将数据放到发送缓冲区，启动写事件监控
    void Send(const char* data, size_t len)
    {
        // 外界传入的data，可能是个临时的空间，我们现在直接压入任务池有可能没有被立即执行
        // 因此有可能在执行的时候，data指向的空间有可能已经被释放了，所以这里先定义一个buffer
        Buffer buf;
        buf.WriteAndPush(data, len);
        // std::move直接将资源转移
        _loop->RunInLoop(std::bind(&Connection::SendInLoop, this, std::move(buf)));
    }

    // 提供给组件使用者的关闭接口--并不是直接立马关闭，而是先判断是否还有数据待处理
    void Shutdown()
    {
        _loop->RunInLoop(std::bind(&Connection::ShutdownInLoop, this));
    }

    // TODO
    // 直接关闭
    void Release()
    {
        _loop->QueueInLoop(std::bind(&Connection::ReleaseInLoop, this));
    }

    // 启动非活跃销毁，并定义多长实际无通信就是非活跃，添加定时任务
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInLoop(std::bind(&Connection::EnableInactiveReleaseInLoop, this, sec));
    }

    // 取消非活跃销毁
    void CancelInactiveRelease()
    {
        _loop->RunInLoop(std::bind(&Connection::CancelInactiveReleaseInLoop, this));
    }

    //切换协议---重置上下文以及阶段性回调处理函数 -- 而是这个接口必须在EventLoop线程中立即执行
    //防备新的事件触发后，处理的时候，切换任务还没有被执行--会导致数据使用原协议处理了。
    void Upgrade(const Any& context, const ConnectedCallback& conn, const MessageCallback& msg,
                 const ClosedCallback& closed, const AnyEventCallback& event)
    {
        _loop->AssertInLoop();
        _loop->RunInLoop(std::bind(&Connection::UpgradeInLoop, this, context, conn, msg, closed, event));
    }
};