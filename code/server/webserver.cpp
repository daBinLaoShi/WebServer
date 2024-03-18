/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */

#include "webserver.h"

using namespace std;


// 端口 ET模式 timeoutMs 优雅退出
// sql端口、账号、密码、数据库
// 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量
WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
    srcDir_ = getcwd(nullptr, 256); // 获取当前工作目录
    assert(srcDir_); // 在代码中加入断言
    strncat(srcDir_, "/resources/", 16); // 将"/resources/"字符串连接到srcDir_字符串的末尾
    HttpConn::userCount = 0; // 将HttpConn类的静态成员变量userCount设置为0
    HttpConn::srcDir = srcDir_; // 将HttpConn类的静态成员变量srcDir设置为先前获取的工作目录路径
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum); // 数据库连接池初始化------------------------------------------ 调用了SqlConnPool类的静态成员函数Instance()获取其单例对象，然后调用Init()函数对数据库连接池进行初始化

    InitEventMode_(trigMode); // 初始化事件模式 ----------------------------------------------------------------------------------------------
    if(!InitSocket_()) { isClose_ = true;} // 初始化套接字 -----------------------------------------------------------------------------------

    // 日志记录
    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize); // 初始化日志记录器
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); } // 如果 isClose_ 为 true，表示服务器初始化出错
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

 // 析构函数
WebServer::~WebServer() {
    close(listenFd_); // 关闭服务器的监听套接字 listenFd_
    isClose_ = true; // 表示服务器已关闭
    free(srcDir_); // 释放存储资源目录路径的内存空间
    SqlConnPool::Instance()->ClosePool(); // 关闭数据库连接池
}

// 初始化事件模式
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET); // 判断是否采用边缘触发模式
}

 // 服务器的主事件循环
void WebServer::Start() {
    int timeMS = -1;  // epoll 等待的超时时间为无限，即没有事件时会一直阻塞。
    if(!isClose_) { LOG_INFO("========== Server start =========="); } // 记录信息日志表示服务器已经启动。
    while(!isClose_) {
        if(timeoutMS_ > 0) { // 如果设置了超时时间
            timeMS = timer_->GetNextTick(); // 获取下一个计时器超时时间
        }
        int eventCnt = epoller_->Wait(timeMS); // 等待事件发生,当有事件发生时，获取事件数量 eventCnt
        for(int i = 0; i < eventCnt; i++) { // 遍历处理每个事件
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if(fd == listenFd_) { // 如果事件是来自监听套接字 listenFd_
                DealListen_(); // 处理监听事件
            }
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 如果事件是错误事件或连接关闭事件
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]); // 关闭连接
            }
            else if(events & EPOLLIN) { // 事件是可读事件
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]); // 处理读取事件
            }
            else if(events & EPOLLOUT) { // 事件是可写事件
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); // 处理写入事件
            } else {
                LOG_ERROR("Unexpected event"); // 错误日志表示出现了意外情况
            }
        }
    }
}

// 向客户端发送错误信息并关闭连接
void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0); // 调用 send() 函数向客户端发送错误信息
    if(ret < 0) { // 表示发送错误
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd); // 关闭与客户端的连接
}

// 用于关闭客户端连接
void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd()); // 从 epoll 实例中删除客户端的文件描述符
    client->Close(); // 关闭客户端连接
}

// 向服务器添加新的客户端连接
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0); // 确保传入的文件描述符 fd 是有效的
    users_[fd].init(fd, addr); // 初始化新的 HttpConn 对象
    if(timeoutMS_ > 0) { // 如果设置了超时时间
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd])); // 为这个连接添加一个计时器。这个计时器会在一段时间后触发，调用 WebServer::CloseConn_() 函数关闭这个连接。
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_); // 将这个文件描述符添加到 epoll 实例中进行事件监听，指定监听的事件类型为 EPOLLIN | connEvent_，表示监听可读事件和连接事件。
    SetFdNonblock(fd); // 将套接字设置为非阻塞模式。
    LOG_INFO("Client[%d] in!", users_[fd].GetFd()); // 记录信息日志，表示新的客户端连接已经添加到服务器中
}

// 处理监听套接字上的连接事件
void WebServer::DealListen_() {
    struct sockaddr_in addr; // 存储客户端的地址信息
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len); // 使用 accept() 函数接受客户端的连接请求
        if(fd <= 0) { return;}
        else if(HttpConn::userCount >= MAX_FD) { // 服务器当前的连接数已经达到了最大连接数 MAX_FD
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr); // 添加客户端
    } while(listenEvent_ & EPOLLET); // 如果监听事件采用了边缘触发模式（EPOLLET）
}

// 处理客户端套接字可读事件
void WebServer::DealRead_(HttpConn* client) {
    assert(client); // 确保传入的 client 指针是有效的
    ExtentTime_(client); // 更新客户端的活动时间
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client)); // 向线程池中添加一个任务
}

// 处理客户端套接字可写事件
void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 更新客户端的活动时间
void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); } // 更新客户端的计时器，以确保在一段时间后触发超时处理。
}

// 处理客户端套接字的读取事件
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno); // 进行读取操作
    if(ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client); // 关闭客户端连接
        return;
    }
    OnProcess(client); // 对读取的数据进行处理
}

// 处理客户端套接字读取c到的数据
void WebServer::OnProess(HttpConn* client) {
    if(client->process()) {//对客户端读取到的数据进行处理
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT); // 修改客户端套接字的监听事件，将其设置为可写事件 EPOLLOUT
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN); // 修改客户端套接字的监听事件，将其设置为可读事件 EPOLLIN
    }
}

// 处理客户端套接字的写入事件
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno); // 向客户端套接字写入数据，并返回写入的字节数
    if(client->ToWriteBytes() == 0) { // 检查客户端还有待写入的字节数
        /* 传输完成 */
        if(client->IsKeepAlive()) { // 首先检查是否需要保持连接
            OnProcess(client); // 处理客户端请求
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) { // 检查错误码是否为 EAGAIN
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT); // 将客户端套接字的监听事件设置为可写事件 EPOLLOUT
            return;
        }
    }
    CloseConn_(client); // 关闭客户端连接
}

// 初始化套接字
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr; // 套接字地址信息
    if(port_ > 65535 || port_ < 1024) { // 检查 port_ 变量是否在有效范围内
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET; // 设置 addr.sin_family 为 AF_INET，表示使用 IPv4 地址。
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 将套接字绑定到本地任意可用的 IP 地址上
    addr.sin_port = htons(port_); // 将 port_ 转换为网络字节序，并存储在 addr.sin_port 中
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1; // 启用优雅关闭
        optLinger.l_linger = 1; // 等待 1 秒钟后关闭连接
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0); // 创建了一个套接字
    if(listenFd_ < 0) { // 创建失败
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger)); // 设置套接字 listenFd_ 的 SO_LINGER 选项
    if(ret < 0) { // 设置失败
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)); // 设置套接字选项 SO_REUSEADDR
    if(ret == -1) { //设置失败
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr)); // 将套接字 listenFd_ 绑定到一个具体的网络地址上
    if(ret < 0) { // 绑定失败
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6); // 将套接字设置为监听状态，参数 6 是指定允许同时连接的最大客户端数量
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_); // 关闭套接字 listenFd_
        return false;
    }
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN); // 将套接字添加到 epoll 实例中进行事件监听
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_); // 将套接字设置为非阻塞模式，以便使用 epoll 边缘触发模式。
    LOG_INFO("Server port:%d", port_); // 记录信息日志表示服务器已经成功启动
    return true;
}

// 将指定文件描述符设置为非阻塞模式
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK); // 使用 O_NONBLOCK 宏将获取到的文件状态标志设置为非阻塞模式
}
