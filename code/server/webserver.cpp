#include "webserver.h"
using namespace std;

// 端口 ET模式 timeoutMs 优雅退出
// sql端口、账号、密码、数据库
// 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量
WebServer::WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger,
        int sqlPort, const char *sqlUser, const char *sqlPwd,
        const char *dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize) :
        port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
        timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()) {
    srcDir_ = getcwd(nullptr, 256); // 返回当前工作目录的路径名
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16); // 将"/resources/"字符串连接到末尾
    HttpConn::userCount = 0; // 连接的用户数量
    HttpConn::srcDir = srcDir_; // HTTP服务器的根目录
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName,
                                  connPoolNum); // 创建一个数据库连接池

    InitEventMode_(
            trigMode); // 初始化事件模式为ET模式(3)
    if (!InitSocket_()) { isClose_ = true; } // 初始化套接字

    // 日志记录
    if (openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize); // 初始化日志记录器
        if (isClose_) { LOG_ERROR("========== Server init error!=========="); } // 如果 isClose_ 为 true，表示服务器初始化出错
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
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
    listenEvent_ = EPOLLRDHUP; // 监听事件;EPOLLRDHUP是epoll中的一个事件类型，指示对端关闭了连接
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; // 连接事件;EPOLLONESHOT表示事件在被触发后只会发生一次
    switch (trigMode) {
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
    int timeMS = -1;  // epoll等待的超时时间为无限
    if (!isClose_) { LOG_INFO("========== Server start =========="); } // 记录信息日志表示服务器已经启动。
    while (!isClose_) {
        if (timeoutMS_ > 0) { // 如果设置了超时时间
            timeMS = timer_->GetNextTick(); // 获取下一个计时器超时时间
        }
        int eventCnt = epoller_->Wait(timeMS); // 等待事件发生,当有事件发生时,获取事件数量
        for (int i = 0; i < eventCnt; i++) { // 遍历处理每个事件

            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if (fd == listenFd_) { // 事件是监听套接字实例
                DealListen_(); // 处理监听事件
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 错误事件或连接关闭事件
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]); // 关闭用户连接
            } else if (events & EPOLLIN) { // 可读事件
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]); // 处理读取事件
            } else if (events & EPOLLOUT) { // 可写事件
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); // 处理可写事件
            } else {
                LOG_ERROR("Unexpected event"); // 错误日志
            }
        }
    }
}

// 向客户端发送错误信息并关闭连接
void WebServer::SendError_(int fd, const char *info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0); // 调用send()函数向客户端发送错误信息
    if (ret < 0) { // 表示发送错误
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd); // 关闭与客户端的连接
}

// 用于关闭客户端连接
void WebServer::CloseConn_(HttpConn *client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd()); // 从 epoll 实例中删除客户端的文件描述符
    client->Close(); // 关闭客户端连接
}

// 添加新的客户端连接
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr); // 初始化新的连接
    if (timeoutMS_ > 0) { // 添加超时时间
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this,
                                              &users_[fd])); // 为连接添加计时器。
    }
    epoller_->AddFd(fd,
                    EPOLLIN | connEvent_); // 将文件描述符添加到epoll实例中，监听事件类型为可读事件和连接事件。
    SetFdNonblock(fd); // 设置为非阻塞模式。
    LOG_INFO("Client[%d] in!", users_[fd].GetFd()); // 记录信息日志，新的客户端连接已经添加到服务器
}

// 处理监听套接字实例连接事件
void WebServer::DealListen_() {
    struct sockaddr_in addr; // 地址信息
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (struct sockaddr *) &addr, &len); // 接受客户端的连接请求
        if (fd <= 0) { return; }
        else if (HttpConn::userCount >= MAX_FD) { // 服务器用户连接数已满
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        AddClient_(fd, addr); // 添加客户端连接
    } while (listenEvent_ & EPOLLET); // 监听事件采用了边缘触发模式
}

// 处理客户端套接字可读事件
void WebServer::DealRead_(HttpConn *client) {
    assert(client);
    ExtentTime_(client); // 更新客户端连接的定时器时间
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client)); // 线程池中添加任务，处理客户端可读事件
}

// 处理客户端套接字可写事件
void WebServer::DealWrite_(HttpConn *client) {
    assert(client);
    ExtentTime_(client); // 更新过期时间
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

// 更新客户端的活动时间
void WebServer::ExtentTime_(HttpConn *client) {
    assert(client);
    if (timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); } // 更新客户端的计时器，以确保在一段时间后触发超时处理。
}

// 处理客户端的可读事件
void WebServer::OnRead_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno); // 客户端读取数据，存放在读缓冲区中
    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client); // 关闭客户端连接
        return;
    }
    OnProcess(client); // 对读取的数据进行处理
}

// 处理客户端读取到的数据
void WebServer::OnProess(HttpConn *client) {
    if (client->process()) {//对客户端读取到的数据进行处理
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT); // 修改客户端套接字的监听事件，将其设置为可写事件
    } else {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN); // 设置为可读事件
    }
}

// 处理客户端套接字的写入事件
void WebServer::OnWrite_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno); // 向客户端套接字写入数据，并返回写入的字节数
    if (client->ToWriteBytes() == 0) { // 检查客户端还有待写入的字节数
        /* 传输完成 */
        if (client->IsKeepAlive()) { // 首先检查是否需要保持连接
            OnProcess(client); // 处理客户端请求
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) { // 检查错误码是否为 EAGAIN
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
    if (port_ > 65535 || port_ < 1024) { // 检查端口号
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    addr.sin_family = AF_INET; // 设置addr.sin_family为 AF_INET，表示使用IPv4地址。
    addr.sin_addr.s_addr = htonl(INADDR_ANY); //将套接字绑定到本地任意可用的IP地址上
    addr.sin_port = htons(port_); // 将port_转换为网络字节序，并存储在addr.sin_port 中
    struct linger optLinger = {0}; // 优雅关闭结构体
    if (openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1; // 启用优雅关闭
        optLinger.l_linger = 1; // 等待 1 秒钟后关闭连接
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0); // 创建了一个套接字实例
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger)); // 设置优雅关闭
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int)); //允许地址重用
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    ret = bind(listenFd_, (struct sockaddr *) &addr, sizeof(addr)); // 套接字绑定到具体的网络地址上
    if (ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    ret = listen(listenFd_, 6); // 套接字设置为监听状态，同时连接的最大客户端数量为6
    if (ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_); // 关闭套接字 listenFd_
        return false;
    }
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN); // 将套接字添加到epoll实例中进行事件监听
    if (ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_); // 将套接字设置为非阻塞模式
    LOG_INFO("Server port:%d", port_); // 记录日志,服务器已经成功启动
    return true;
}

// 将指定文件描述符设置为非阻塞模式
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK); // 使用 O_NONBLOCK 宏将获取到的文件状态标志设置为非阻塞模式
}
