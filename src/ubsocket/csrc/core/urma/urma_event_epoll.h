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
#ifndef UBS_COMM_URMA_EVENT_EPOLL_H
#define UBS_COMM_URMA_EVENT_EPOLL_H

#include "core/ubsocket_event_epoll.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaEventPoll : public EventPoll {
public:
    explicit UrmaEventPoll(int epoll_fd) : EventPoll(epoll_fd) {}
    ~UrmaEventPoll() override;

    int EpollCtl(int op, int fd, struct epoll_event *event) override;
    int EpollWait(struct epoll_event *events, int maxevents, int timeout) override;
    void WakeUpEpollFd() override;

private:
    int EpollCtlAdd(int fd, struct epoll_event *event);
    int EpollCtlMod(int fd, struct epoll_event *event);
    int EpollCtlDel(int fd);
    int AddRawSocketEvent(int fd, struct epoll_event *event);
    int ModRawSocketEvent(int fd, struct epoll_event *event);
    int DelRawSocketEvent(int fd);
    int AddUrmaSocketEvent(UrmaSocketPtr &socket, struct epoll_event *event);
    int ModUrmaSocketEvent(UrmaSocketPtr &socket, struct epoll_event *event);
    int DelUrmaSocketEvent(UrmaSocketPtr &socket);

    ALWAYS_INLINE EpollEvent *AllocateEpollEvent(EpollEventType type, int socket, const struct epoll_event &evt)
    {
        EpollEvent *result = nullptr;

        Locker sLock(mutex_);
        if (removed_head_ != nullptr) {
            result = removed_head_;
            removed_head_ = removed_head_->next;
        }
        sLock.Unlock();
        if (result == nullptr) {
            return new (std::nothrow) EpollEvent(type, socket, evt);
        }

        result->event_type = type;
        result->socket_fd = socket;
        result->event = evt;
        result->next = nullptr;
        return result;
    }

    ALWAYS_INLINE bool IsSocketEventDataExist(int fd) noexcept
    {
        Locker sLock(mutex_);
        return socket_data_.find(fd) != socket_data_.end();
    }

    ALWAYS_INLINE bool InsertSocketEventData(int fd, EpollEvent *data) noexcept
    {
        Locker sLock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos != socket_data_.end())) {
            return false;
        }
        socket_data_.emplace(fd, data);
        return true;
    }

    ALWAYS_INLINE bool RemoveSocketEventData(int fd) noexcept
    {
        Locker sLock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return false;
        }
        auto removed = pos->second;
        socket_data_.erase(pos);
        if (removed != nullptr) {
            removed->next = removed_head_;
            removed_head_ = removed;
        }
        return true;
    }

    ALWAYS_INLINE EpollEvent *GetSocketEventData(int fd) noexcept
    {
        Locker slock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            return nullptr;
        }
        auto res = pos->second;
        slock.Unlock();
        return res;
    }

    std::unordered_map<int, EpollEvent *> socket_data_;
    EpollEvent *removed_head_ = nullptr;
};
using UrmaEventPollPtr = Ref<UrmaEventPoll>;

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_EVENT_EPOLL_H
