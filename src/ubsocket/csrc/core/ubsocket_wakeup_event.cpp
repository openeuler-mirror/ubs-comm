/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Description: Wake up ready event fd utility implementation (adapted for ubs-comm_new)
 */

#include "ubsocket_wakeup_event.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ock {
namespace ubs {

UbsocketWakeupEvent::UbsocketWakeupEvent() : epollFd_(-1), readyEventFd_(-1), ready_event_mutex_(nullptr)
{
    ready_event_mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
}

UbsocketWakeupEvent::~UbsocketWakeupEvent()
{
    CleanUp();
    if (ready_event_mutex_ != nullptr) {
        LockRegistry::LOCK_OPS.destroy(ready_event_mutex_);
        ready_event_mutex_ = nullptr;
    }
}

int UbsocketWakeupEvent::Initialize(int epollFd)
{
    epollFd_ = epollFd;

    if (LIKELY(readyEventFd_ >= 0)) {
        return 0;
    }

    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: create ready event fd failed: %d : %s\n", errno, strerror(errno));
        return -1;
    }

    struct epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &ready_event_;
    int ret = epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: epoll_ctl add ready event fd failed: %d : %s\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    readyEventFd_ = fd;
    UBS_VLOG_INFO("UbsocketWakeupEvent: ready event fd %d initialized\n", readyEventFd_);
    return 0;
}

void UbsocketWakeupEvent::CleanUp()
{
    if (readyEventFd_ >= 0) {
        if (epollFd_ >= 0) {
            epoll_ctl(epollFd_, EPOLL_CTL_DEL, readyEventFd_, nullptr);
        }
        close(readyEventFd_);
        readyEventFd_ = -1;
    }

    if (ready_event_mutex_ != nullptr) {
        Locker sLock(ready_event_mutex_);
        while (!ready_event_queue_.empty()) {
            ready_event_queue_.pop();
        }
    }
}

void UbsocketWakeupEvent::WakeUpReadyEventFd(int fd)
{
    if (UNLIKELY(readyEventFd_ < 0)) {
        UBS_VLOG_WARN("UbsocketWakeupEvent: WakeUpReadyEventFd failed, not initialized.\n");
        return;
    }

    {
        Locker sLock(ready_event_mutex_);
        ready_event_queue_.push(fd);
    }

    uint64_t notification = 1;
    if (eventfd_write(readyEventFd_, notification) < 0) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: WakeUpReadyEventFd eventfd_write failed.\n");
    }
}

int UbsocketWakeupEvent::ProcessReadyEvents(struct epoll_event *events, int maxevents,
                                            std::unordered_map<int, EpollEvent *> &socket_data)
{
    int num = 0;
    Locker sLock(ready_event_mutex_);

    while (!ready_event_queue_.empty() && num < maxevents) {
        int fd = ready_event_queue_.front();
        ready_event_queue_.pop();

        auto it = socket_data.find(fd);
        if (it != socket_data.end()) {
            events[num].events = EPOLLIN;
            events[num].data.ptr = it->second;
            num++;
        }
    }
    return num;
}

} // namespace ubs
} // namespace ock
