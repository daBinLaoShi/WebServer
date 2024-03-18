/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#include "log.h"

using namespace std;

// 默认构造函数
Log::Log() {
    lineCount_ = 0; // 记录当前日志文件中的行数
    isAsync_ = false; // 默认不启用异步写日志
    writeThread_ = nullptr; // 异步写日志的线程对象
    deque_ = nullptr; // 异步写日志时的日志队列
    toDay_ = 0; // 记录上一次记录日志的日期
    fp_ = nullptr; // 指向当前日志文件的文件指针
}


// 析构函数
Log::~Log() {
    if(writeThread_ && writeThread_->joinable()) { // 检查异步写日志线程是否可加入
        while(!deque_->empty()) { // 循环刷新日志队列，确保队列中的所有日志消息都被写入到日志文件中。
            deque_->flush();
        };
        deque_->Close(); // 关闭日志队列，释放相关资源
        writeThread_->join(); // 等待异步写日志线程结束
    }
    if(fp_) {
        lock_guard<mutex> locker(mtx_);
        flush(); // 刷新日志缓冲区
        fclose(fp_); // 关闭日志文件
    }
}

// 获取日志的当前记录级别
int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

// 设置日志的当前记录级别
void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

// 初始化日志记录器
void Log::init(int level = 1, const char* path, const char* suffix,
    int maxQueueSize) {
    isOpen_ = true; // 表示日志记录器已经打开
    level_ = level; // 将日志级别设置为传入的 level 值
    if(maxQueueSize > 0) {
        isAsync_ = true; // 启用异步日志记录
        if(!deque_) { // 检查是否已经创建了日志队列 deque_
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>); // 创建一个新的 BlockDeque<std::string> 对象
            deque_ = move(newDeque); // 将其移动到 deque_ 成员变量中
            
            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread)); // 创建一个新的线程用于异步刷新日志
            writeThread_ = move(NewThread);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0; // 表示当前日志行数为 0。

    time_t timer = time(nullptr); // 获取当前系统时间
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;

    {
        lock_guard<mutex> locker(mtx_); // 进行加锁
        buff_.RetrieveAll(); // 清空日志缓冲区
        if(fp_) { 
            flush(); // 刷新日志文件
            fclose(fp_); // 关闭之前的日志文件（如果已打开）
        }

        fp_ = fopen(fileName, "a"); // 然后打开新的日志文件
        if(fp_ == nullptr) {
            mkdir(path_, 0777); // 创建日志文件所在的目录
            fp_ = fopen(fileName, "a");
        } 
        assert(fp_ != nullptr); // 确保日志文件已经成功打开
    }
}

// 写入日志
void Log::write(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr); // 获取当前时间，将结果存储在 now 变量中
    time_t tSec = now.tv_sec; // 从 now 结构体中提取秒数
    struct tm *sysTime = localtime(&tSec); // 使用 localtime 函数将秒数转换为本地时间，并将结果存储在 sysTime 指针变量中
    struct tm t = *sysTime;
    va_list vaList; // 存储可变参数列表

    /* 日志日期 日志行数 */
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_  %  MAX_LINES == 0))) // 如果当前日期与上一次记录日志的日期不同，或者当前日志行数达到了预设的最大行数 MAX_LINES
    {
        unique_lock<mutex> locker(mtx_); // 创建了一个 unique_lock 对象 locker
        locker.unlock(); // 进行解锁操作
        
        char newFile[LOG_NAME_LEN]; // 生成新的日志文件名
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (toDay_ != t.tm_mday) // 如果当前日期与上一次记录日志的日期不同
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }
        else {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_  / MAX_LINES), suffix_);
        }
        
        locker.lock(); // 进行加锁
        flush(); // 将日志缓冲区中的内容写入到文件中
        fclose(fp_); // 关闭当前日志文件
        fp_ = fopen(newFile, "a"); // 打开或创建新的日志文件，以追加写入模式（"a"）
        assert(fp_ != nullptr); // 检查文件是否成功打开
    }

    {
        unique_lock<mutex> locker(mtx_); // 进行加锁
        lineCount_++; // 增加日志行数计数器，表示写入了一行日志
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);// 使用 snprintf 函数将当前时间信息格式化为字符串，并将其写入到日志缓冲区中,返回写入缓冲区的字符数
                    
        buff_.HasWritten(n); // 告知日志缓冲区已经写入了 n 个字符
        AppendLogLevelTitle_(level); // 在日志消息中添加日志级别的标题

        va_start(vaList, format); // 初始化一个va_list对象，它表示了参数列表中可变参数的起始位置
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList); // 根据指定的格式format以及可变参数列表vaList来格式化字符串，并将结果写入到缓冲区中
        va_end(vaList); // 结束对可变参数列表的访问

        buff_.HasWritten(m);// 告知日志缓冲区已经写入了 m 个字符
        buff_.Append("\n\0", 2); // 向缓冲区中追加一个字符串

        if(isAsync_ && deque_ && !deque_->full()) { //检查是否启用了异步写日志、日志队列是否可用、以及日志队列是否未满
            deque_->push_back(buff_.RetrieveAllToStr()); // 将日志缓冲区中的内容转换为字符串，并将其放入日志队列的尾部
        } else {
            fputs(buff_.Peek(), fp_); // 直接将日志消息写入到日志文件中
        }
        buff_.RetrieveAll(); // 清空日志缓冲区，以便下次写入使用
    }
}

// 根据给定的日志级别向日志缓冲区中追加对应的日志级别标题
void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

// 刷新日志缓冲区
void Log::flush() {
    if(isAsync_) { 
        deque_->flush(); 
    }
    fflush(fp_); // 刷新文件缓冲区
}

// 异步写入日志消息到日志文件中
void Log::AsyncWrite_() {
    string str = "";
    while(deque_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        fputs(str.c_str(), fp_); // 将字符串str中的内容写入到日志文件中
    }
}

// 获取 Log 类的单例对象
Log* Log::Instance() {
    static Log inst;
    return &inst;
}

// 在一个单独的线程中异步刷新日志
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}