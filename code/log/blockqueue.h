/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <sys/time.h>

template<class T>
class BlockDeque {
public:
    explicit BlockDeque(size_t MaxCapacity = 1000);

    ~BlockDeque();

    void clear();

    bool empty();

    bool full();

    void Close();

    size_t size();

    size_t capacity();

    T front();

    T back();

    void push_back(const T &item);

    void push_front(const T &item);

    bool pop(T &item);

    bool pop(T &item, int timeout);

    void flush();

private:
    std::deque<T> deq_; // 存储队列中的元素

    size_t capacity_;// 队列的最大容量

    std::mutex mtx_; // 互斥量

    bool isClose_; // 队列是否已关闭

    std::condition_variable condConsumer_; // 消费者条件变量，用于通知等待在消费者条件上的线程队列中有新的元素可以消费或者队列已关闭。

    std::condition_variable condProducer_; // 生产者条件变量，用于通知等待在生产者条件上的线程队列中有新的空位可以生产新的元素。
};

// 构造函数
template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) :capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    isClose_ = false;
}

// 析构函数
template<class T>
BlockDeque<T>::~BlockDeque() {
    Close();
};

// 关闭阻塞队列
template<class T>
void BlockDeque<T>::Close() {
    {
        std::lock_guard<std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    condProducer_.notify_all(); // 通知所有等待在生产者条件上的线程，队列已经关闭。
    condConsumer_.notify_all(); // 通知所有等待在消费者条件上的线程，队列已经关闭。
};

// 清空队列中的所有元素
template<class T>
void BlockDeque<T>::flush() {
    condConsumer_.notify_one(); // 通知一个等待在消费者条件上的线程
};

// 清空阻塞队列中的所有元素
template<class T>
void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> locker(mtx_);
    deq_.clear(); // 将队列 deq_ 中的所有元素清空。
}

// 返回队列 deq_ 的第一个元素
template<class T>
T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

// 返回队列 deq_ 的最后一个元素
template<class T>
T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

// 获取阻塞队列的当前大小
template<class T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

// 获取阻塞队列的最大容量
template<class T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

// 用于向队列尾部插入元素
template<class T>
void BlockDeque<T>::push_back(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker); // 线程会等待生产者条件变量 condProducer_
    }
    deq_.push_back(item); // 将元素 item 插入到队列 deq_ 的尾部
    condConsumer_.notify_one(); // 通知一个等待在消费者条件上的线程
}

// 向队列头部插入元素
template<class T>
void BlockDeque<T>::push_front(const T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.size() >= capacity_) {
        condProducer_.wait(locker); // 线程会等待生产者条件变量 condProducer_
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

// 判断阻塞队列是否为空
template<class T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.empty();
}

// 判断阻塞队列是否已满
template<class T>
bool BlockDeque<T>::full(){
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

// 从队列中取出元素
template<class T>
bool BlockDeque<T>::pop(T &item) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()){
        condConsumer_.wait(locker); // 线程会等待消费者条件变量 condConsumer_
        if(isClose_){
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

// pop() 方法的重载版本，允许设置超时时间
template<class T>
bool BlockDeque<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty()){
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) 
                == std::cv_status::timeout){
            return false;
        }
        if(isClose_){
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

#endif // BLOCKQUEUE_H