/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>      

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;
    
    sockaddr_in GetAddr() const;
    
    bool process();

    // 表示待写入的字节数
    int ToWriteBytes() { 
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }

    static bool isET;
    static const char* srcDir;
    static std::atomic<int> userCount;
    
private:
   
    int fd_; // 网络连接的文件描述符
    struct  sockaddr_in addr_; // 服务器的地址信息

    bool isClose_; // 当前连接是否关闭
    
    int iovCnt_; // 当前 I/O 向量的数量
    struct iovec iov_[2]; // 用于进行分散/聚集 I/O 操作。
    
    Buffer readBuff_; // 存储读取数据的缓冲区
    Buffer writeBuff_; // 存储写入数据的缓冲区

    HttpRequest request_; // 当前连接的 HTTP 请求对象
    HttpResponse response_; // 当前连接的 HTTP 响应对象
};


#endif //HTTP_CONN_H