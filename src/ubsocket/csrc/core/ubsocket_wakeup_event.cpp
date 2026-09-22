/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Description: Wake up ready event fd utility implementation (adapted for ubs-comm_new)
 */

#include "ubsocket_wakeup_event.h"
#include "ubsocket_socket.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ock {
namespace ubs {

UbsocketWakeupEvent::UbsocketWakeupEvent() : epollFd_(-1), readyEventFd_(-1) {}

UbsocketWakeupEvent::~UbsocketWakeupEvent()
{
    CleanUp();
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
    UBS_VLOG_DEBUG("UbsocketWakeupEvent: ready event fd %d initialized\n", readyEventFd_);
    return 0;
}

void UbsocketWakeupEvent::CleanUp()
{
    if (readyEventFd_ >= 0) {
        if (epollFd_ >= 0) {
            /* Drop this listener's entry from the poll's wakeup table before the eventfd goes:
             * the registered callback captures our owner, which is being torn down. */
            EventPollPtr aepRef = ArraySet<EventPoll>::GetInstance().GetItem(epollFd_);
            auto *aep = (AsyncEventPoll *)aepRef.Get();
            if (aep != nullptr) {
                aep->RemoveWakeupCallback(&ready_event_);
            }
            epoll_ctl(epollFd_, EPOLL_CTL_DEL, readyEventFd_, nullptr);
        }
        close(readyEventFd_);
        readyEventFd_ = -1;
    }
}

void UbsocketWakeupEvent::WakeUpReadyEventFd(int fd)
{
    if (UNLIKELY(readyEventFd_ < 0)) {
        UBS_VLOG_WARN("UbsocketWakeupEvent: WakeUpReadyEventFd failed, not initialized. listen_fd=%d\n", fd);
        return;
    }

    uint64_t notification = 1;
    if (eventfd_write(readyEventFd_, notification) < 0) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: WakeUpReadyEventFd eventfd_write failed. eventfd=%d listen_fd=%d "
                     "errno=%d (%s)\n",
                     readyEventFd_, fd, errno, strerror(errno));
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
    // Step2: 通过 ArraySet<Socket> 线程安全地获取 listen_fd_ 对应的 SocketBase
    //         （原子加载 + 引用计数），再从 SocketBase 中取出注册 epoll 时保存的
    //         event.data（brpc SocketId）和 event.events，构造返回事件。
    //         避免直接访问 socket_data_（unordered_map）与 RemoveSocketEventData
    //         持锁 erase 的并发数据竞争。
    auto sock = ArraySet<Socket>::GetInstance().GetItem(listen_fd_);
    if (UNLIKELY(sock == nullptr)) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: listen_fd_=%d not found in ArraySet<Socket>\n", listen_fd_);
        return 0;
    }

    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (UNLIKELY(sockBase == nullptr)) {
        UBS_VLOG_ERR("UbsocketWakeupEvent: listen_fd_=%d failed to convert to SocketBase\n", listen_fd_);
        return 0;
    }
    events[0].events = sockBase->GetEvents();
    events[0].data = sockBase->GetEpollData();

    UBS_VLOG_DEBUG("UbsocketWakeupEvent: ProcessReadyEvents done, pending:%llu, listen_fd:%d",
                   (unsigned long long)u, listen_fd_);
    return 1;
}

} // namespace ubs
} // namespace ock
