/*
 * @Author       : mark
 * @Date         : 2020-06-15
 * @copyleft Apache 2.0
 */ 

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>


class ThreadPool {
public:
    // 代参构造函数
    explicit ThreadPool(size_t threadCount = 8): pool_(std::make_shared<Pool>()) {
            assert(threadCount > 0);
            for(size_t i = 0; i < threadCount; i++) {
                std::thread([pool = pool_] {
                    std::unique_lock<std::mutex> locker(pool->mtx); // 创建了一个独占锁 locker，锁定了线程池的互斥量 pool->mtx
                    while(true) {
                        if(!pool->tasks.empty()) { // 线程会不断地从任务队列中取出任务执行
                            auto task = std::move(pool->tasks.front());
                            pool->tasks.pop();
                            locker.unlock();
                            task();
                            locker.lock();
                        } 
                        else if(pool->isClosed) break;
                        else pool->cond.wait(locker);
                    }
                }).detach(); // 在循环中创建了一个新的线程，并使用 lambda 表达式作为线程的执行体
            }
    }

    // 默认构造函数
    ThreadPool() = default;

    // 移动构造函数
    ThreadPool(ThreadPool&&) = default;

    // 析构函数
    ~ThreadPool() {
        if(static_cast<bool>(pool_)) { // 检查 pool_ 的指针是否指向有效的对象
            {
                std::lock_ugard<std::mutex> locker(pool_->mtx); // 创建了一个互斥量的独占锁 locker，并锁定了线程池对象中的互斥量 pool_->mtx
                pool_->isClosed = true; // 表示线程池已关闭
            }
            pool_->cond.notify_all(); // 通知所有等待在条件变量 pool_->cond 上的线程
        }
    }

    // 用于向线程池中添加任务
    template<class F> // 声明了一个模板参数 F
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(pool_->mtx); // 创建了一个互斥量的独占锁 locker，并锁定了线程池对象中的互斥量 pool_->mtx
            pool_->tasks.emplace(std::forward<F>(task)); // 将传入的任务 task 移动（或复制）到线程池的任务队列 pool_->tasks 中
        }
        pool_->cond.notify_one(); // 通知一个等待在条件变量 pool_->cond 上的线程
    }

private:
    // 用于线程池管理的数据结构和状态
    struct Pool {
        std::mutex mtx; // 互斥量 mtx 用于在多线程环境下对共享资源进行互斥访问
        std::condition_variable cond; // 条件变量 cond 用于在多线程环境下实现线程的等待和通知机制
        bool isClosed; // 指示线程池是否已关闭
        std::queue<std::function<void()>> tasks; // 任务队列 tasks 用于存储待执行的任务
    };
    std::shared_ptr<Pool> pool_;
};


#endif //THREADPOOL_H