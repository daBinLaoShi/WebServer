/*
 * @Author       : mark
 * @Date         : 2020-06-17
 * @copyleft Apache 2.0
 */ 
#include "heaptimer.h"

//向上调整
void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2;//计算节点 i 的父节点索引
    while(j >= 0) {
        if(heap_[j] < heap_[i]) { break; }
        SwapNode_(i, j);//首先检查节点 i 是否小于其父节点 j
        i = j;
        j = (i - 1) / 2;
    }
}

// 交换节点
void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());//确保指定的索引位置在有效范围内
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);//交换堆中索引为 i 和 j 的两个节点的值
    ref_[heap_[i].id] = i;//更新哈希表 ref_ 中的映射关系
    ref_[heap_[j].id] = j;
} 

//向下调整
bool HeapTimer::siftdown_(size_t index, size_t n) {
    assert(index >= 0 && index < heap_.size());//确保指定的索引位置在有效范围内
    assert(n >= 0 && n <= heap_.size());
    size_t i = index;//当前节点的索引
    size_t j = i * 2 + 1;//其左子节点的索引
    while(j < n) {//用于向下调整节点 i
        if(j + 1 < n && heap_[j + 1] < heap_[j]) j++;//首先检查右子节点是否存在且比左子节点小
        if(heap_[i] < heap_[j]) break;//检查当前节点 i 是否小于其较小的子节点 j
        SwapNode_(i, j);//交换节点 i 和节点 j 的位置
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

//向堆中添加一个新的定时器节点
void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb) {
    assert(id >= 0);
    size_t i;
    if(ref_.count(id) == 0) {//检查是否已经存在指定id节点
                             //新节点：堆尾插入，调整堆
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb}); // id、过期时间以及调用函数
        siftup_(i); //向上调整堆
    } 
    else {
        // 已有结点：调整堆
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout); // 更新过期时间
        heap_[i].cb = cb; // 更新过期函数
        if(!siftdown_(i, heap_.size())) { // 向下调整位置
            siftup_(i); //向上调整位置
        }
    }
}

//删除指定 id 的定时器节点
void HeapTimer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    if(heap_.empty() || ref_.count(id) == 0) {//检查堆是否为空或者指定的 id 是否存在于 ref_ 中
        return;
    }
    size_t i = ref_[id];//获取指定 id 对应的在堆中的索引位置 i
    TimerNode node = heap_[i];//获取堆中索引为 i 的定时器节点
    node.cb();
    del_(i);
}

//删除堆中指定位置的定时器节点。
void HeapTimer::del_(size_t index) {
    /* 删除指定位置的结点 */
    assert(!heap_.empty() && index >= 0 && index < heap_.size());//确保堆不为空，并且指定的索引位置在有效范围内
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if(i < n) {
        SwapNode_(i, n);//将要删除的节点与堆中最后一个节点进行交换。
        if(!siftdown_(i, n)) {//尝试将其向下调整
            siftup_(i);//向上调整
        }
    }
    /* 队尾元素删除 */
    ref_.erase(heap_.back().id);//从 ref_ 中移除队尾元素对应的 id
    heap_.pop_back();//将队尾元素从堆中移除
}

//重新调整定时器节点的超时时间
void HeapTimer::adjust(int id, int timeout) {
    assert(!heap_.empty() && ref_.count(id) > 0);
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);//更新定时器节点的过期时间
    siftdown_(ref_[id], heap_.size());//向下调整堆
}

//处理堆中已经超时的定时器节点
void HeapTimer::tick() {
    /* 清除超时结点 */
    if(heap_.empty()) {//检查堆是否为空
        return;
    }
    while(!heap_.empty()) {//处理堆中已经超时的定时器节点
        TimerNode node = heap_.front();//获取堆中位于顶部的定时器节点
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) { //检查该节点的过期时间是否已经到达或超过当前时间
            break; 
        }
        node.cb();
        pop();//将堆中最顶部的节点移除
    }
}

//从堆中移除顶部的定时器节点
void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

//清空堆中所有定时器节点的功能
void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

//获取下一个定时器节点的超时时间
int HeapTimer::GetNextTick() {
    tick();//处理堆中已经超时的定时器节点
    size_t res = -1;//保存下一个定时器节点的超时时间
    if(!heap_.empty()) {//如果堆不为空
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();//堆顶部定时器节点的超时时间，并计算相对于当前时间的时间差
        if(res < 0) { res = 0; }//如果计算得到的超时时间小于 0
    }
    return res;
}