/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 
#include "httpconn.h"
using namespace std;

const char* HttpConn::srcDir; // 存储 HTTP 服务器的根目录
std::atomic<int> HttpConn::userCount; // 记录当前连接的用户数量。
bool HttpConn::isET; // 是否采用边缘触发模式

// 默认构造函数
HttpConn::HttpConn() { 
    fd_ = -1; // 当前网络连接的文件描述符尚未被指定或无效。
    addr_ = { 0 };
    isClose_ = true;
};

// 析构函数
HttpConn::~HttpConn() { 
    Close(); 
};

// 初始化
void HttpConn::init(int fd, const sockaddr_in& addr) {
    assert(fd > 0); // 使用断言确保传入的文件描述符 fd 大于 0
    userCount++; // 增加连接用户计数器
    addr_ = addr; // 将传入的远程地址信息 addr 复制给成员变量 addr_
    fd_ = fd; // 将传入的文件描述符 fd 赋值给成员变量 fd_
    writeBuff_.RetrieveAll(); // 清空写缓冲区
    readBuff_.RetrieveAll(); // 清空读缓冲区
    isClose_ = false; // 表示当前连接处于打开状态。
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd_, GetIP(), GetPort(), (int)userCount); // 记录连接建立的日志信息
}

// 关闭当前的 HTTP 连接
void HttpConn::Close() {
    response_.UnmapFile(); // 取消映射的文件
    if(isClose_ == false){ // 检查当前连接是否已关闭
        isClose_ = true; 
        userCount--; // 用户计数减一
        close(fd_); // 关闭套接字文件描述符 fd_。
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);// 记录连接关闭的日志信息
    }
}

// 获取网络连接的文件描述符
int HttpConn::GetFd() const {
    return fd_;
};

//获取当前连接的客户端地址信息
struct sockaddr_in HttpConn::GetAddr() const {
    return addr_;
}

//获取当前连接的客户端 IP 地址
const char* HttpConn::GetIP() const {
    return inet_ntoa(addr_.sin_addr);
}

// 获取端口
int HttpConn::GetPort() const {
    return addr_.sin_port;
}

// 从套接字文件描述符中读取数据到读缓冲区中
ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = readBuff_.ReadFd(fd_, saveErrno);//调用 readBuff_ 对象的 ReadFd 方法从套接字文件描述符 fd_ 中读取数据到读缓冲区中。这个方法会返回读取的字节数。
        if (len <= 0) {
            break;
        }
    } while (isET); // isET 变量用于表示是否使用了边缘触发模式。
    return len;
}

// 将写缓冲区中的数据写入到套接字文件描述符中
ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        len = writev(fd_, iov_, iovCnt_); // 调用 writev 函数将分散的数据一并写入到套接字文件描述符中，并返回写入的字节数。
        if(len <= 0) { //写入的字节数小于等于 0
            *saveErrno = errno; // 将错误码保存到指定的变量中
            break;
        }
        if(iov_[0].iov_len + iov_[1].iov_len  == 0) { break; } //如果待发送的数据量为 0，表示传输结束，跳出循环。
        else if(static_cast<size_t>(len) > iov_[0].iov_len) { // 如果写入的字节数大于第一个缓冲区中的数据量
            iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);// 更新 iov_ 数组中的数据信息，并将写缓冲区中已发送的数据部分删除。
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            if(iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        else {
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; // 更新 iov_ 数组中的数据信息，并将写缓冲区中已发送的数据删除。
            iov_[0].iov_len -= len; 
            writeBuff_.Retrieve(len);
        }
    } while(isET || ToWriteBytes() > 10240); // 直到发送完所有数据或者写缓冲区中的数据量超过一定阈值（10KB）为止
    return len;
}

// 用于处理 HTTP 请求
bool HttpConn::process() {
    request_.Init(); // 初始化 HTTP 请求对象。
    if(readBuff_.ReadableBytes() <= 0) { // 如果读缓冲区中没有可读字节
        return false;
    }
    else if(request_.parse(readBuff_)) { // 如果能够成功解析 HTTP 请求
        LOG_DEBUG("%s", request_.path().c_str()); // 记录请求路径的日志信息
        response_.Init(srcDir, request_.path(), request_.IsKeepAlive(), 200); // 初始化 HTTP 响应对象，设置相应的路径、是否保持连接以及响应状态码。
    } else {
        response_.Init(srcDir, request_.path(), false, 400); // 初始化 HTTP 响应对象，设置相应的路径、保持连接状态为 false，以及响应状态码为 400。
    }

    response_.MakeResponse(writeBuff_); // 根据响应对象生成相应的响应内容，存储到写缓冲区中。
    /* 响应头 */
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek()); // 设置 iov_ 结构体数组，用于在 write 方法中进行写操作。
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    /* 文件 */
    if(response_.FileLen() > 0  && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2;
    }
    LOG_DEBUG("filesize:%d, %d  to %d", response_.FileLen() , iovCnt_, ToWriteBytes());
    return true;
}
