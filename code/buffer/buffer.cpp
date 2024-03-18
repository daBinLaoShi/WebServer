/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */ 
#include "buffer.h"

// 构造函数
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

// 计算缓冲区中可读取数据的字节数
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

// 计算缓冲区中可写入数据的字节数
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}

// 计算缓冲区中可预留的空间字节数。
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 获取缓冲区中可读取数据的起始地址
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_; // 调用 BeginPtr_() 方法获取缓冲区的起始地址，然后加上当前读取位置的偏移量
}

// 从缓冲区中消耗（读取）指定长度的数据
void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

// 从缓冲区中消耗（读取）数据直到指定的结束位置。
void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

// 清空缓冲区中的所有数据
void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

// 将缓冲区中的所有数据读取为字符串，并清空缓冲区
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

// 获取缓冲区中可写入数据的起始地址。
const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

// 获取缓冲区中可写入数据的起始地址。
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

// 更新缓冲区的写入位置
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

// 向缓冲区中追加指定长度的数据
void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

// 向缓冲区中追加指定长度的字符数组
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite()); // 将指定长度的字符数组 str 中的数据复制到缓冲区中的可写入位置
    HasWritten(len); // 更新缓冲区的写入位置
}

// 向缓冲区中追加另一个缓冲区的数据
void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 确保缓冲区中有足够的可写入空间
void Buffer::EnsureWriteable(size_t len) {
    if(WritableBytes() < len) { // 获取当前缓冲区中可写入数据的字节数，然后与给定的长度 len 进行比较
        MakeSpace_(len); // 扩展缓冲区，以确保有足够的可写入空间
    }
    assert(WritableBytes() >= len);
}

// 从文件描述符 fd 中读取数据到缓冲区中
ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535]; // 暂时存储未能完全写入到缓冲区中的数据。
    struct iovec iov[2]; // 描述多个连续内存区域的地址和长度。
    const size_t writable = WritableBytes(); // 获取当前缓冲区中可写入数据的大小
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_; // 指定其基地址为当前缓冲区中可写入数据的起始地址
    iov[0].iov_len = writable; // 长度为可写入数据的大小
    iov[1].iov_base = buff; // 指定其基地址为临时缓冲区 buff 的起始地址
    iov[1].iov_len = sizeof(buff); // 长度为 buff 的大小

    const ssize_t len = readv(fd, iov, 2); // 从文件描述符 fd 中读取数据，并将数据分散写入到 iov 数组指定的内存区域
    if(len < 0) {
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len) <= writable) { // 读取到的数据量小于等于当前缓冲区中可写入数据的大小
        writePos_ += len; // 更新写入位置
    }
    else {
        writePos_ = buffer_.size(); // 将部分数据写入到缓冲区中
        Append(buff, len - writable); // 将剩余的数据追加到缓冲区中
    }
    return len;
}

// 将缓冲区中的数据写入到文件描述符 fd 中
ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes(); // 获取当前缓冲区中可读取数据的大小
    ssize_t len = write(fd, Peek(), readSize); // 调用 write() 函数将缓冲区中的数据写入到文件描述符 fd 中，并返回实际写入的字节数。
    if(len < 0) {
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len; // 更新缓冲区的读取位置
    return len; // 返回实际写入的字节数
}

// 返回缓冲区的起始地址
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

// 返回缓冲区的起始地址
const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

// 确保缓冲区中有足够的可写空间
void Buffer::MakeSpace_(size_t len) {
    if(WritableBytes() + PrependableBytes() < len) { // 前缓冲区中可写空间加上前置空间的大小小于 len
        buffer_.resize(writePos_ + len + 1); // 对缓冲区进行扩展
    } 
    else {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_()); // 将已有数据复制到缓冲区的起始位置
        readPos_ = 0; // 更新读取位置和写入位置
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes()); // 确保复制操作的正确性
    }
}