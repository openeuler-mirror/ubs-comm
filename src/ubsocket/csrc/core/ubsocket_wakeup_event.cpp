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

    struct epoll_event event {
    };
    event.events = EPOLLIN;
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

    uint64_t notification = 1;
    if (eventfd_write(readyEventFd_, notification) < 0) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: WakeUpReadyEventFd eventfd_write failed.\n");
    }
}

int UbsocketWakeupEvent::ProcessReadyEvents(struct epoll_event *events, int maxevents,
                                            std::unordered_map<int, EpollEvent *> &socket_data)
{
    // Step 1: consume the eventfd counter (wakeup notification)
    uint64_t u;
    ssize_t s = read(readyEventFd_, &u, sizeof(uint64_t));
    if (s != sizeof(uint64_t)) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: ProcessReadyEvents read failed\n");
    }
    // Step2: 通过 listen_fd_ 从 socket_data 取出对应的 EpollEvent*
    //         填到 events[0].data.ptr，使 ArrangeWakeUpEvents 能索引到原始 socketid
    auto it = socket_data.find(listen_fd_);
    if (UNLIKELY(it == socket_data.end())) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: listen_fd_=%d not found in socket_data\n", listen_fd_);
        return 0;
    }
    events[0] = it->second->event;

    UBS_VLOG_INFO("UbsocketWakeupEvent: ProcessReadyEvents done, pending:%llu, listen_fd:%d, epollEvent:%p\n",
                  (unsigned long long)u, listen_fd_, it->second);
    return 1;
}

} // namespace ubs
} // namespace ock
