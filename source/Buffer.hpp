#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdint>

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