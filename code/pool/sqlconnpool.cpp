/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 

#include "sqlconnpool.h"
using namespace std;

// 默认构造函数
SqlConnPool::SqlConnPool() {
    useCount_ = 0; // 使用计数
    freeCount_ = 0; // 空闲连接计数
}

// 获取 SqlConnPool 类的单例对象
SqlConnPool* SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

 // 数据库连接池初始化
 // 主机名 host、端口号 port、用户名 user、密码 pwd、数据库名称 dbName 连接池的大小 connSize
void SqlConnPool::Init(const char* host, int port,
            const char* user,const char* pwd, const char* dbName,
            int connSize = 10) {
    assert(connSize > 0); // 断言连接池的大小必须大于0，如果不满足该条件，程序将会终止，并输出错误信息。
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr; // 创建一个 MYSQL 对象 sql
        sql = mysql_init(sql); // 进行初始化
        if (!sql) { // 初始化失败
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        sql = mysql_real_connect(sql, host,
                                 user, pwd,
                                 dbName, port, nullptr, 0); // 连接到 MySQL 服务器
        if (!sql) { // 连接失败记录日志
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(sql); // 加入数据库连接池队列
    }
    MAX_CONN_ = connSize;
    sem_init(&semId_, 0, MAX_CONN_); // 始化一个信号量 semId_，用于控制连接的获取和释放
}

// 从连接池中获取一个 MYSQL 连接对象
MYSQL* SqlConnPool::GetConn() {
    MYSQL *sql = nullptr; // 用于存储获取的 MYSQL 连接对象
    if(connQue_.empty()){ // 检查连接池中是否有可用的连接
        LOG_WARN("SqlConnPool busy!"); // 连接池忙碌，无法获取连接。
        return nullptr;
    }
    sem_wait(&semId_); // 使用信号量进行同步，等待可用连接
    {
        lock_guard<mutex> locker(mtx_); // 使用 std::lock_guard 对连接池的互斥量 mtx_ 进行加锁
        sql = connQue_.front();
        connQue_.pop();
    }
    return sql;
}

// 将不再使用的 MYSQL 连接对象放回连接池中
void SqlConnPool::FreeConn(MYSQL* sql) {
    assert(sql);
    lock_guard<mutex> locker(mtx_); // 使用 std::lock_guard 对连接池的互斥量 mtx_ 进行加锁
    connQue_.push(sql);
    sem_post(&semId_); // 使用信号量进行同步，增加可用连接的计数。
}

// 关闭连接池并释放所有连接资源
void SqlConnPool::ClosePool() {
    lock_guard<mutex> locker(mtx_);
    while(!connQue_.empty()) {
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item); // 关闭 MYSQL 连接对象，释放连接资源。
    }
    mysql_library_end(); // 关闭 MySQL 客户端库，释放相关资源。
}

// 获取连接池中空闲连接的数量
int SqlConnPool::GetFreeConnCount() {
    lock_guard<mutex> locker(mtx_);
    return connQue_.size(); // 返回连接队列 connQue_ 的大小
}

// 析构函数
SqlConnPool::~SqlConnPool() {
    ClosePool();
}
