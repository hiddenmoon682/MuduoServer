#include <iostream>
#include <cstdint>
#include <functional>
#include <memory>
#include <unistd.h>
#include <vector>
#include <unordered_map>

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

// 时间轮
// 使用秒针，走到哪里就释放到哪里；释放哪里，就相当于析构，就相当于执行任务
// 不过再加以使用智能指针shared_ptr就可以实现刷新/延迟任务时间

// weak_ptr可以与shared_ptr指向同一个对象，但是weak_ptr不参与计数
// 使用weak_ptr拷贝构造一个shared_ptr，这个shared_ptr会与其他的shared_ptr一起计数
class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;
    int _tick;              // 当前的秒针，走到哪里就释放到哪里；释放哪里，就相当于执行哪里的任务
    int _capacity;          // 表盘的最大数量，其实就是最大延迟时间
    std::vector<std::vector<PtrTask>> _wheel;
    std::unordered_map<uint64_t, WeakTask> _timers;
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
public:
    TimerWheel()
        :_tick(0), _capacity(60), _wheel(_capacity)
    {}

    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc& cb)
    {
        PtrTask pt(new TimerTask(id, delay, cb));
        pt->SetRelease(std::bind(&TimerWheel::RemoveTimer, this, id));
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
        _timers[id] = WeakTask(pt);
    }

    // 刷新/延迟任务
    void TimerRefresh(uint64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end())
        {
            return;
        }

        // lock获取weak_ptr管理对象对应的shared_ptr
        PtrTask pt = it->second.lock();
        int delay = pt->DelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    void TimerCancel(uint64_t id)
    {
        auto it = _timers.find(id);
        if(it == _timers.end())
        {
            return;
        }
        PtrTask pt = it->second.lock();
        if(pt) pt->Cancel();
    }

    // 这个函数应该每秒钟被执行一次，相当于秒钟向后走一步
    void RunTimerTask()
    {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();  // 清空对应位置数组，就会把数组中管理的所有的shared_ptr释放掉
    }
};

class Test {
    public:
        Test() {std::cout << "构造" << std::endl;}
        ~Test() {std::cout << "析构" << std::endl;}
};

void DelTest(Test *t) {
    delete t;
}

int main()
{
    TimerWheel tw;

    Test *t = new Test();

    tw.TimerAdd(888, 5, std::bind(DelTest, t));

    for(int i = 0; i < 5; i++) {
        sleep(1);
        tw.TimerRefresh(888);//刷新定时任务
        tw.RunTimerTask();//向后移动秒针
        std::cout << "刷新了一下定时任务，重新需要5s中后才会销毁\n";
    }
    //tw.TimerCancel(888);
    while(1) {
        sleep(1);
        std::cout << "-------------------\n";
        tw.RunTimerTask();//向后移动秒针
    }
    return 0;
}