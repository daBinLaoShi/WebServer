/*
 * @Author       : mark
 * @Date         : 2020-06-19
 * @copyleft Apache 2.0
 */

#include "epoller.h"

// 构造函数
Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){ // epoll_create(512)创建一个epoll实例,参数 512 是 epoll 实例能够监视的文件描述符的初始数量的建议值
    assert(epollFd_ >= 0 && events_.size() > 0);
}

// 析构函数
Epoller::~Epoller() {
    close(epollFd_);
}

// 向 epoll 实例中添加文件描述符和关注的事件
bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) return false; // 检查传入的文件描述符
    epoll_event ev = {0}; // 创建了一个 epoll_event 结构体实例 ev
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev); // 调用 epoll_ctl 函数向 epoll 实例中添加文件描述符和关注的事件
}

// 修改指定文件描述符的 epoll 事件监听
bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) return false;
    epoll_event ev = {0}; // 创建一个 epoll_event 结构体 ev
    ev.data.fd = fd; // 要修改的文件描述符
    ev.events = events; // 要修改的新的事件监听类型
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev); // 将新的事件监听类型应用到 epoll 实例中的指定文件描述符上
}

// 从 epoll 实例中删除指定的文件描述符
bool Epoller::DelFd(int fd) {
    if(fd < 0) return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev); // 从 epoll 实例中删除指定的文件描述符
}

// 等待事件的发生
int Epoller::Wait(int timeoutMs) {
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

// 获取指定事件在 events_ 向量中的文件描述符
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

// 获取 events_ 向量中指定位置的事件的事件类型
uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}