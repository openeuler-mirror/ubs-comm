/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "urma_event_epoll.h"
#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace urma {

UrmaEventPoll::~UrmaEventPoll()
{
    Locker sLock(mutex_);
    auto removed_head = removed_head_;
    removed_head_ = nullptr;
    while (removed_head != nullptr) {
        auto next = removed_head->next;
        delete removed_head;
        removed_head = next;
    }
}

int UrmaEventPoll::EpollCtl(int op, int fd, struct epoll_event *event)
{
    int ret = -1;
    switch (op) {
        case EPOLL_CTL_ADD:
            ret = EpollCtlAdd(fd, event);
            break;
        case EPOLL_CTL_MOD:
            ret = EpollCtlMod(fd, event);
            break;
        case EPOLL_CTL_DEL:
            ret = EpollCtlDel(fd);
            break;
        default:
            UBS_VLOG_ERR("epollctl(epfd:%d) for fd(%d) with unknown op %d", epoll_fd_, fd, op);
            errno = EINVAL;
            break;
    }
    return ret;
}

int UrmaEventPoll::EpollWait(struct epoll_event *events, int maxevents, int timeout)
{
    if (UNLIKELY(events == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll EpollWait events is null.\n");
        errno = EFAULT;
        return -1;
    }

    if (UNLIKELY(maxevents < 0)) {
        UBS_VLOG_ERR("urma_event_poll EpollWait maxevents(%d) invalid.\n", maxevents);
        errno = EINVAL;
        return -1;
    }

    uint64_t event_read_val;
    UBS_VLOG_TRACE("urma epoll wait(epfd=%d, max=%d, timeout=%d) start...", epoll_fd_, maxevents, timeout);
    int ret = epoll_wait(epoll_fd_, events, maxevents, timeout);
    for (auto i = 0; i < ret && i < maxevents; ++i) {
        auto event_data = static_cast<EpollEvent *>(events[i].data.ptr);
        if (UNLIKELY(event_data == nullptr)) {
            UBS_VLOG_ERR("urma epoll wait event(%d) data null.", i);
            continue;
        }

        if (event_data->event_type == EPOLL_EVENT_RAW_SOCKET) {
            events[i].data = event_data->event.data;
            UBS_VLOG_TRACE("urma epoll(%d) raw socket event(evt=0x%x, data=%lx)", epoll_fd_, events[i].events,
                           events[i].data.u64);
            continue;
        }

        events[i].events = 0;
        events[i].data = event_data->event.data;
        auto sock = ArraySet<Socket>::GetInstance().GetItem(event_data->socket_fd);
        if (UNLIKELY(sock == nullptr)) {
            UBS_VLOG_ERR("event wait wake up socket(%d) not found.", event_data->socket_fd);
            continue;
        }

        auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
        events[i].events = urma_sock->GetReachEpollEvents() & event_data->event.events;
        UBS_VLOG_TRACE("urma epoll(%d) urma socket event(evt=0x%x, data=%lx)", epoll_fd_, events[i].events,
                       events[i].data.u64);
        eventfd_read(urma_sock->event_fd_, &event_read_val);
    }

    return ret;
}

void UrmaEventPoll::WakeUpEpollFd() {}

int UrmaEventPoll::EpollCtlAdd(int fd, struct epoll_event *event)
{
    if (UNLIKELY(event == nullptr || fd < 0)) {
        UBS_VLOG_ERR("urma_event_poll AddEvent invalid args fd:%d, event:%p\n", fd, event);
        errno = EINVAL;
        return -1;
    }

    UBS_VLOG_TRACE("urma epoll(%d) add(fd=%d, event=0x%x, data=%lx)", epoll_fd_, fd, event->events, event->data);
    if (UNLIKELY(IsSocketEventDataExist(fd))) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlAdd(socket=%d) already added.", fd);
        errno = EEXIST;
        return -1;
    }

    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (UNLIKELY(sock == nullptr || !sock->IsBindRemote())) {
        return AddRawSocketEvent(fd, event);
    }

    auto urma_sock = RefStaticCast<UrmaSocket>(sock);
    auto ret = AddUrmaSocketEvent(urma_sock, event);
    if (ret != 0) {
        return ret;
    }

    urma_sock->SetAddedEpollFd(nullptr, event->data);
    urma_sock->SetEvents(event->events);
    uint32_t notify_events = 0;
    if ((event->events & EPOLLOUT) && urma_sock->ExchangeWritableReady(false)) {
        notify_events |= EPOLLOUT;
    }
    if ((event->events & EPOLLIN) && urma_sock->ExchangeSocketReadableReady(false)) {
        notify_events |= EPOLLIN;
    }
    if (notify_events != 0) {
        urma_sock->SetReachEpollEvents(notify_events);
        eventfd_write(urma_sock->event_fd_, 1);
    }
    return 0;
}

int UrmaEventPoll::EpollCtlMod(int fd, struct epoll_event *event)
{
    if (UNLIKELY(fd < 0 || event == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll ModEvent invalid args fd:%d, event:%p\n", fd, event);
        errno = EINVAL;
        return -1;
    }

    UBS_VLOG_TRACE("urma epoll(%d) mod(fd=%d, event=0x%x, data=%lx)", epoll_fd_, fd, event->events, event->data);
    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (UNLIKELY(sock == nullptr || !sock->IsBindRemote())) {
        return ModRawSocketEvent(fd, event);
    }

    auto urma_sock = RefStaticCast<UrmaSocket>(sock);
    auto ret = ModUrmaSocketEvent(urma_sock, event);
    if (ret != 0) {
        return ret;
    }

    urma_sock->SetEvents(event->events);
    urma_sock->SetEpollData(event->data);
    uint32_t notify_events = 0;
    if ((event->events & EPOLLOUT) && urma_sock->ExchangeWritableReady(false)) {
        notify_events |= EPOLLOUT;
    }
    if ((event->events & EPOLLIN) && urma_sock->ExchangeSocketReadableReady(false)) {
        notify_events |= EPOLLIN;
    }
    if (notify_events != 0) {
        urma_sock->SetReachEpollEvents(notify_events);
        eventfd_write(urma_sock->event_fd_, 1);
    }

    return 0;
}

int UrmaEventPoll::EpollCtlDel(int fd)
{
    if (UNLIKELY(fd < 0)) {
        UBS_VLOG_ERR("urma_event_poll DelEvent invalid args fd:%d", fd);
        errno = EINVAL;
        return -1;
    }

    UBS_VLOG_TRACE("urma epoll(%d) del(fd=%d)", epoll_fd_, fd);
    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (UNLIKELY(sock == nullptr || !sock->IsBindRemote())) {
        UBS_VLOG_DEBUG("sock is nullptr for origin sock, socket: %d", fd);
        return DelRawSocketEvent(fd);
    }

    auto urma_sock = RefStaticCast<UrmaSocket>(sock);
    auto ret = DelUrmaSocketEvent(urma_sock);
    if (ret != 0) {
        return ret;
    }

    urma_sock->SetEvents(0);
    urma_sock->SetEpollData({});
    return 0;
}

int UrmaEventPoll::AddRawSocketEvent(int fd, struct epoll_event *event)
{
    struct epoll_event raw_event {};
    auto event_data = AllocateEpollEvent(EPOLL_EVENT_RAW_SOCKET, fd, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll add out event for socket fd: %d alloc failed.\n", fd);
        return -1;
    }

    raw_event.events = event->events;
    raw_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll add pure event for socket fd: %d failed: %d : %s\n", fd, errno, strerror(errno));
        delete event_data;
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(fd, event_data))) {
        UBS_VLOG_ERR("urma_event_poll add pure event for socket fd: %d insert event data failed\n", fd);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int UrmaEventPoll::ModRawSocketEvent(int fd, struct epoll_event *event)
{
    auto event_data = GetSocketEventData(fd);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlMod(socket:%d) failed, event_data null", fd);
        errno = EINVAL;
        return -1;
    }

    struct epoll_event raw_event {};
    raw_event.events = event->events;
    raw_event.data.ptr = event_data;
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlMod(socket:%d) failed: %d : %s\n", fd, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int UrmaEventPoll::DelRawSocketEvent(int fd)
{
    if (!RemoveSocketEventData(fd)) {
        UBS_VLOG_WARN("urma_event_poll del pure event for socket: %d failed, RemoveSocketEventData failed\n", fd);
        return 0;
    }
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll del pure event for socket: %d failed: %d : %s\n", fd, errno, strerror(errno));
        return -1;
    }

    return 0;
}

int UrmaEventPoll::AddUrmaSocketEvent(UrmaSocketPtr &socket, struct epoll_event *event)
{
    struct epoll_event raw_event {};
    auto event_data = AllocateEpollEvent(EPOLL_EVENT_UB_SOCKET_IN, socket->raw_socket_, *event);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll add event for urma socket fd: %d alloc failed.", socket->event_fd_);
        errno = ENOMEM;
        return -1;
    }

    raw_event.events = EPOLLIN;
    raw_event.data.ptr = event_data;
    UBS_VLOG_TRACE("real epoll(%d) add(fd=%d, event=0x%x, data=%lx)", epoll_fd_, socket->event_fd_, raw_event.events,
                   raw_event.data);
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket->event_fd_, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll add pure event for socket fd: %d, event fd: %d failed: %d : %s",
                     socket->raw_socket_, socket->event_fd_, errno, strerror(errno));
        delete event_data;
        return -1;
    }

    if (UNLIKELY(!InsertSocketEventData(socket->raw_socket_, event_data))) {
        UBS_VLOG_ERR("urma_event_poll add pure event for socket fd: %d, event fd: %d insert event data failed",
                     socket->raw_socket_, socket->event_fd_);
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, socket->event_fd_, nullptr);
        delete event_data;
        return -1;
    }

    return 0;
}

int UrmaEventPoll::ModUrmaSocketEvent(UrmaSocketPtr &socket, struct epoll_event *event)
{
    auto event_data = GetSocketEventData(socket->raw_socket_);
    if (UNLIKELY(event_data == nullptr)) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlMod(socket: %d, event fd: %d) failed, event_data null",
                     socket->raw_socket_, socket->event_fd_);
        errno = EINVAL;
        return -1;
    }

    struct epoll_event raw_event {};
    raw_event.events = EPOLLIN;
    raw_event.data.ptr = event_data;
    event_data->event = *event;
    UBS_VLOG_TRACE("real epoll(%d) mod(fd=%d, event=0x%x, data=%lx)", epoll_fd_, socket->event_fd_, raw_event.events,
                   raw_event.data);
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, socket->event_fd_, &raw_event);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlMod(socket:%d, event fd: %d) failed: %d : %s\n", socket->raw_socket_,
                     socket->event_fd_, errno, strerror(errno));
        return -1;
    }
    return 0;
}

int UrmaEventPoll::DelUrmaSocketEvent(UrmaSocketPtr &socket)
{
    if (!RemoveSocketEventData(socket->raw_socket_)) {
        UBS_VLOG_WARN("urma_event_poll del urma event for socket: %d failed, RemoveSocketEventData failed\n",
                      socket->raw_socket_);
        return 0;
    }
    UBS_VLOG_TRACE("real epoll(%d) del(fd=%d, event=0x%x, data=%lx)", epoll_fd_, socket->event_fd_);
    auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, socket->event_fd_, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("urma_event_poll del urma event for socket: %d, event fd: %d failed: %d : %s\n",
                     socket->raw_socket_, socket->event_fd_, errno, strerror(errno));
        return -1;
    }

    return 0;
}

} // namespace urma
} // namespace ubs
} // namespace ock
