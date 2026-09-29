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

#include <unordered_map>

#include "core/ubsocket_event_epoll.h"
#include "urma_event_dispatch.h"
#include "urma_readable_event_queue.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

/*
 * In DISPATCHER mode a JFCE event is polled/dispatched inline inside epoll_wait. The readable
 * urma-socket events produced that way are buffered in a ReadableEventQueue (single epoll_wait
 * thread) and drained into the output array before this epoll_wait returns; what does not fit is
 * served by the next wait before it blocks, so none are lost.
 */
class UrmaEventPoll
    : public EventPoll
    , public UrmaReadableNotifier {
public:
    explicit UrmaEventPoll(int epoll_fd) noexcept;
    ~UrmaEventPoll() override;

    /*
     * Two-phase init (codebase Result style): Queue/Dispatcher mode registers every poller's
     * JFCE interrupt fd into the kernel epoll and allocates the pending-readable event ring.
     * In other modes this is a no-op returning UBS_OK. Must be called once before EpollWait.
     * On failure the partially registered handlers are rolled back and UBS_ERROR is returned
     * (no exception is thrown from the constructor).
     */
    Result Init() noexcept;

    int EpollCtl(int op, int fd, struct epoll_event *event) override;
    int EpollWait(struct epoll_event *events, int maxevents, int timeout) override;
    void WakeUpEpollFd() override;
    /* socket fd 关闭: 清 socket_data_ 注册残留 */
    void OnSocketClosed(int fd) override;

    /*
     * UrmaReadableNotifier (installed on sockets registered on this poll): record a readable
     * event inline when this poll drives the JFC polling from its own epoll_wait thread
     * (Queue/Dispatcher mode). Returns false otherwise so the socket falls back to eventfd.
     */
    bool NotifyReadable(UrmaSocket &sock) noexcept override;

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
    void HandleJfceEvent(EpollEvent *event_data);
    bool RecordReadableSocket(UrmaSocket *urma_sock);
    /* Move events buffered by readable_events_ into out (up to maxevents), dropping stale fds. */
    uint32_t DrainPendingReadable(struct epoll_event *events, uint32_t maxevents);
    /* Detach the inline-reader hook from sockets still registered here (see dtor). */
    void UnbindReadableNotifiers() noexcept;

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
        result->oneshot_disabled = false;
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

    ALWAYS_INLINE void ClearJfceHandlers() noexcept
    {
        for (auto &it : jfce_handlers_) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it.first, nullptr);
            delete it.second;
        }
        jfce_handlers_.clear();
    }

    std::unordered_map<int, EpollEvent *> jfce_handlers_; /* jfce interrupt fd -> 注册用 EpollEvent */
    std::unordered_map<int, EpollEvent *> socket_data_;
    UrmaHandleModeCaps dispatch_caps_;   /* ctor snapshot of the handle-mode capabilities of this epoll */
    ReadableEventQueue readable_events_; /* deferred readable urma-socket events (single wait thread) */
    bool inited_{false};                 /* Init() ran successfully (or was a no-op in non-dispatcher mode) */
    EpollEvent *removed_head_ = nullptr;
};
using UrmaEventPollPtr = Ref<UrmaEventPoll>;

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_EVENT_EPOLL_H
