/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id; //标识定时器节点
    TimeStamp expires;//定时器节点的过期时间
    TimeoutCallBack cb;//定时器节点的回调函数，当定时器到期时会调用该回调函数。
    bool operator<(const TimerNode& t) {
        return expires < t.expires;
    }//用于比较两个 TimerNode 对象的过期时间
};
class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); } // 预先分配内存空间，大小为 64

    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();

private:
    void del_(size_t i);
    
    void siftup_(size_t i);

    bool siftdown_(size_t index, size_t n);

    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_;//小根堆

    std::unordered_map<int, size_t> ref_;//记录各个id的位置
};

#endif //HEAP_TIMER_H