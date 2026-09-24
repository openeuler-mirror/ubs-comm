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
#include "urma_data_rx_ops.h"
#include "urma_event_dispatch.h"

namespace ock {
namespace ubs {
namespace urma {

namespace {
/* Depth of the deferred readable-event queue (see ReadableEventQueue). */
constexpr uint32_t READABLE_EVENT_QUEUE_CAPACITY = 4096;

/* 当前正在执行 epoll_wait 内联派发的 UrmaEventPoll(thread_local, 单 wait 线程) */
thread_local UrmaEventPoll *g_tls_active_event_poll = nullptr;

class ActiveEventPollGuard {
public:
    explicit ActiveEventPollGuard(UrmaEventPoll *ep) noexcept : prev_(g_tls_active_event_poll)
    {
        g_tls_active_event_poll = ep;
    }

    ~ActiveEventPollGuard() noexcept
    {
        g_tls_active_event_poll = prev_;
    }

private:
    UrmaEventPoll *prev_;
};
} // namespace

UrmaEventPoll::UrmaEventPoll(int epoll_fd) noexcept : EventPoll(epoll_fd), dispatch_caps_(UrmaHandleModeCapsNow()) {}

Result UrmaEventPoll::Init() noexcept
{
    if (inited_) {
        return UBS_OK;
    }
    /*
     * Queue/Dispatcher mode: epoll_wait registers the pollers' JFCE fds and polls the JFC
     * inline. Any failure rolls back the already-registered handlers and returns UBS_ERROR.
     */
    if (!dispatch_caps_.epoll_wait_driven) {
        inited_ = true;
        return UBS_OK;
    }

    auto init_ret = readable_events_.Init(READABLE_EVENT_QUEUE_CAPACITY);
    if (UNLIKELY(init_ret != UBS_OK)) {
        UBS_VLOG_ERR("urma epoll(%d) init failed: alloc readable event queue.", epoll_fd_);
        return init_ret;
    }

    auto poller_set = UrmaBackend::Instance().GetPollerSet();
    if (poller_set == nullptr) {
        UBS_VLOG_ERR("urma epoll(%d) init failed: poller_set is nullptr.", epoll_fd_);
        return UBS_ERROR;
    }
    auto pollers = poller_set->GetAllPollers();
    if (UNLIKELY(pollers.empty())) {
        UBS_VLOG_ERR("urma epoll(%d) init failed: no poller is registered.", epoll_fd_);
        return UBS_ERROR;
    }

    bool failed = false;
    for (auto &iter : pollers) {
        if (UNLIKELY(iter.second == nullptr)) {
            UBS_VLOG_ERR("urma epoll(%d) init failed: poller is nullptr.", epoll_fd_);
            failed = true;
            break;
        }

        auto fd = iter.second->Jfce()->Fd();
        if (UNLIKELY(fd < 0)) {
            UBS_VLOG_ERR("urma epoll(%d) init failed: invalid interrupt fd(%d) of poller.", epoll_fd_, fd);
            failed = true;
            break;
        }

        struct epoll_event raw_event {};
        auto event_data = AllocateEpollEvent(EPOLL_EVENT_JFCE_FD, fd, raw_event);
        if (UNLIKELY(event_data == nullptr)) {
            UBS_VLOG_ERR("urma epoll(%d) init failed: alloc EpollEvent for JFCE fd(%d).", epoll_fd_, fd);
            failed = true;
            break;
        }

        /*
         * Consistent with socket registration: the kernel epoll's data.ptr points to the
         * EpollEvent, whose payload directly stores the poller owning this interrupt fd, so
         * handling a JFCE event does not need a jfc->poller lookup.
         */
        event_data->event.data.ptr = iter.second.Get();
        raw_event.events = EPOLLIN;
        raw_event.data.ptr = event_data;
        auto ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &raw_event);
        if (UNLIKELY(ret != 0)) {
            delete event_data;
            UBS_VLOG_ERR("urma epoll(%d) add JFCE fd(%d) failed: %s", epoll_fd_, fd, strerror(errno));
            failed = true;
            break;
        }

        jfce_handlers_.emplace(fd, event_data);
    }

    if (UNLIKELY(failed)) {
        ClearJfceHandlers();
        return UBS_ERROR;
    }
    inited_ = true;
    return UBS_OK;
}

UrmaEventPoll::~UrmaEventPoll()
{
    UnbindReadableNotifiers();
    ClearJfceHandlers();
    Locker sLock(mutex_);
    auto removed_head = removed_head_;
    removed_head_ = nullptr;
    while (removed_head != nullptr) {
        auto next = removed_head->next;
        delete removed_head;
        removed_head = next;
    }
}

void UrmaEventPoll::UnbindReadableNotifiers() noexcept
{
    /*
     * The poll is going away: sockets still registered here keep a readable_notifier_ hook that
     * points at this object, so detach it to avoid a dangling call on a later NotifySocketReadable.
     */
    Locker lock(mutex_);
    for (auto &it : socket_data_) {
        auto *event_data = it.second;
        if (UNLIKELY(event_data == nullptr || event_data->event_type != EPOLL_EVENT_UB_SOCKET_IN)) {
            continue;
        }
        auto sock = ArraySet<Socket>::GetInstance().GetItem(event_data->socket_fd);
        if (UNLIKELY(sock == nullptr || sock->Type() != SocketType::SOCK_TYPE_URMA)) {
            continue;
        }
        auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
        if (urma_sock->GetReadableNotifier() == this) {
            urma_sock->SetReadableNotifier(nullptr);
        }
    }
}

int UrmaEventPoll::EpollCtl(int op, int fd, struct epoll_event *event)
{
    if (UNLIKELY(fd == epoll_fd_)) {
        UBS_VLOG_ERR("urma epoll(%d) ctl(fd=%d) target equals epfd\n", epoll_fd_, fd);
        errno = EINVAL;
        return -1;
    }

    int ret = -1;
    bool mapper_create = false;
    EpollMapper *mapper = nullptr;
    if (op == EPOLL_CTL_ADD) {
        mapper_create = CreateSocketEpollMapper(fd, mapper);
    } else {
        mapper = GetSocketEpollMapper(fd);
    }
    switch (op) {
        case EPOLL_CTL_ADD:
            ret = EpollCtlAdd(fd, event);
            if (ret == 0 && mapper != nullptr) {
                mapper->Add(epoll_fd_);
            } else if (mapper_create) {
                CleanSocketEpollMapper(fd);
                mapper = nullptr;
            }
            break;
        case EPOLL_CTL_MOD:
            ret = EpollCtlMod(fd, event);
            break;
        case EPOLL_CTL_DEL:
            ret = EpollCtlDel(fd);
            if (ret == 0 && mapper != nullptr) {
                if (mapper->Del(epoll_fd_)) {
                    CleanSocketEpollMapper(fd);
                    mapper = nullptr;
                }
            }
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

    if (UNLIKELY(maxevents <= 0)) {
        UBS_VLOG_ERR("urma_event_poll EpollWait maxevents(%d) invalid.\n", maxevents);
        errno = EINVAL;
        return -1;
    }

    /* 先吐出上次 JFCE 内联产出、未放下的可读 socket 事件(不阻塞) */
    uint32_t out = DrainPendingReadable(events, static_cast<uint32_t>(maxevents));
    if (out > 0) {
        return static_cast<int>(out);
    }

    ActiveEventPollGuard guard(this);
    uint64_t event_read_val;
    UBS_VLOG_TRACE("urma epoll wait(epfd=%d, max=%d, timeout=%d) start...", epoll_fd_, maxevents, timeout);
    int ret = epoll_wait(epoll_fd_, events, maxevents, timeout);
    if (UNLIKELY(ret < 0)) {
        return ret;
    }
    PROF_START(__URMA_DEAL_EPOLL_EVENT);
    for (auto i = 0; i < ret && i < maxevents; ++i) {
        auto event_data = static_cast<EpollEvent *>(events[i].data.ptr);
        if (UNLIKELY(event_data == nullptr)) {
            UBS_VLOG_ERR("urma epoll wait event(%d) data null.", i);
            continue;
        }

        /* 异步 accept 唤醒事件: 需放回出参数组, 供上层处理 (处理到事件槽位) */
        if (ready_event_ != nullptr && event_data == ready_event_ && wakeup_callback_ != nullptr) {
            if (out < static_cast<uint32_t>(maxevents)) {
                auto processed = wakeup_callback_(events + out, maxevents - static_cast<int>(out), socket_data_);
                out += static_cast<uint32_t>(processed > 0 ? processed : 0);
            }
            continue;
        }

        /* JFCE 中断事件: 仅内部唤醒, 不外发; 内联 poll+派发, 直接产出可读 socket 事件 */
        if (event_data->event_type == EPOLL_EVENT_JFCE_FD) {
            HandleJfceEvent(event_data);
            continue;
        }

        if (out >= static_cast<uint32_t>(maxevents)) {
            /* 出参已满: 内核中仍就绪的事件留待下次 epoll_wait, 不丢失 */
            continue;
        }

        if (event_data->event_type == EPOLL_EVENT_RAW_SOCKET) {
            events[out] = events[i];
            events[out].data = event_data->event.data; /* 对外事件需携带应用注册的 data */
            UBS_VLOG_TRACE("urma epoll(%d) raw socket event(evt=0x%x, data=%lx)", epoll_fd_, events[out].events,
                           events[out].data.u64);
            out++;
            continue;
        }

        /* 常规 urma socket eventfd 事件(如 EPOLLOUT / 跨线程通知), 与原有处理一致 */
        auto sock = ArraySet<Socket>::GetInstance().GetItem(event_data->socket_fd);
        if (UNLIKELY(sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP)) {
            UBS_VLOG_ERR("event wait wake up socket(%d) invalid.", event_data->socket_fd);
            continue;
        }

        auto urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
        uint32_t reach = urma_sock->GetReachEpollEvents();
        /* Closed state (HUP|RDHUP) reported unconditionally regardless of user interest, aligning with kernel semantics */
        uint32_t closed_bits = reach & (EPOLLHUP | EPOLLRDHUP);
        uint32_t reported = (reach & event_data->event.events) | closed_bits;
        if (event_data->oneshot_disabled.load(std::memory_order_acquire)) {
            /* ONESHOT disabled period: reachability bits preserved, token consumed (prevent busy polling), but no events reported */
            reported = 0;
        }
        events[out].events = reported;
        events[out].data = event_data->event.data;
        UBS_VLOG_TRACE("urma epoll(%d) urma socket event(evt=0x%x, data=%lx)", epoll_fd_, events[out].events,
                       events[out].data.u64);
        /* The kernel eventfd path already delivers this fd: clear pending to avoid a duplicate. */
        readable_events_.ClearPending(event_data->socket_fd);
        eventfd_read(urma_sock->event_fd_, &event_read_val);
        if (event_data->oneshot_disabled.load(std::memory_order_relaxed)) {
            /* Disabled period: do not touch reachability bits (including HUP persistent bit), wait for MOD re-arm to refill and report */
        } else if (reported != 0 && (event_data->event.events & EPOLLONESHOT) != 0) {
            /* ONESHOT triggered: report once then disable, do not rewrite eventfd (align with kernel: no more wakeups before re-arm);
             * clear normal bits but preserve closed_bits terminal state for immediate re-check after re-arm */
            event_data->oneshot_disabled.store(true, std::memory_order_release);
            urma_sock->FetchAndReachEpollEvents(reported & ~closed_bits);
            if (closed_bits != 0) {
                UBS_VLOG_INFO("urma epoll(%d) socket(%d) closed, oneshot fired once(0x%x)", epoll_fd_,
                              event_data->socket_fd, closed_bits);
            }
        } else if (closed_bits != 0) {
            UBS_VLOG_INFO("urma epoll(%d) socket(%d) closed, report HUP(0x%x)", epoll_fd_, event_data->socket_fd,
                          closed_bits);
        } else if (reported != 0) {
            if ((event_data->event.events & EPOLLET) != 0) {
                /* ET: event reported and consumed, clear bits to avoid duplicate notification */
                urma_sock->FetchAndReachEpollEvents(reported);
            } else {
                /* LT: preserve reachability and rewrite eventfd for continuous wakeup until
                 * data plane clears bits and "reported == 0" terminates naturally */
                eventfd_write(urma_sock->event_fd_, 1);
            }
        }
        if (ubsocket_prof_enabled != 0) {
            UrmaRxOps *rxOps = static_cast<UrmaRxOps *>(urma_sock->GetRx()->GetRxOps());
            uint64_t traceTime = rxOps->CurrentTraceNsTime();
            if (traceTime != 0) {
                auto used = ubsocket_get_timeNs() - traceTime;
                PROF_RECORD(__URMA_SLOT_EPOLL_PROC, used, true);
            }
        }
        /* 空事件(reported==0)：eventfd 残留 token 但达位已被数据面(readv)清除，
         * 不返回给调用方，否则 epoll_wait 返回 events==0 的伪事件，
         * 破坏 LT/ET/ONESHOT 静默期语义（epoll_wait 应返回 0 而非空事件） */
        if (reported != 0) {
            out++;
        }
    }
    PROF_END(__URMA_DEAL_EPOLL_EVENT, true);

    /* 把本次 JFCE 内联产出的可读 socket 事件尽量带回, 放不下的留给下次 */
    if (out < static_cast<uint32_t>(maxevents)) {
        out += DrainPendingReadable(events + out, static_cast<uint32_t>(maxevents) - out);
    }
    return static_cast<int>(out);
}

void UrmaEventPoll::HandleJfceEvent(EpollEvent *event_data)
{
    auto poller = static_cast<UrmaAggPoller *>(event_data->event.data.ptr);
    if (UNLIKELY(poller == nullptr)) {
        UBS_VLOG_ERR("urma epoll(%d) jfce event with null poller.", epoll_fd_);
        return;
    }

    UBS_VLOG_TRACE("urma epoll(%d) jfce event, poller(%p) poll and dispatch inline.", epoll_fd_,
                   static_cast<void *>(poller));
    poller->poll();
}

bool UrmaEventPoll::RecordReadableSocket(UrmaSocket *urma_sock)
{
    auto fd = urma_sock->Fd();

    EpollEvent *event_data = nullptr;
    {
        Locker lock(mutex_);
        auto pos = socket_data_.find(fd);
        if (UNLIKELY(pos == socket_data_.end())) {
            /* socket is not registered on this epoll yet; fall back to the eventfd path */
            return false;
        }
        event_data = pos->second;
    }

    struct epoll_event ev {};
    ev.events = urma_sock->GetReachEpollEvents() & event_data->event.events;
    ev.data = event_data->event.data;
    if (UNLIKELY((ev.events & EPOLLIN) == 0)) {
        /* the app registered without EPOLLIN: do not produce a readable event */
        return false;
    }

    /* De-dup and buffering live in ReadableEventQueue; a full ring falls back to eventfd. */
    if (UNLIKELY(!readable_events_.TryRecord(fd, ev))) {
        UBS_VLOG_ERR("urma epoll(%d) readable event queue full, fd(%d) fallback to eventfd.", epoll_fd_, fd);
        return false;
    }
    return true;
}

uint32_t UrmaEventPoll::DrainPendingReadable(struct epoll_event *events, uint32_t maxevents)
{
    return readable_events_.Drain(events, maxevents, [this](int fd) {
        Locker lock(mutex_);
        return socket_data_.find(fd) != socket_data_.end();
    });
}

bool UrmaEventPoll::NotifyReadable(UrmaSocket &sock) noexcept
{
    /*
     * Only this poll's own epoll_wait thread, while it is inside the inline JFCE dispatch
     * (Queue/Dispatcher mode), may record the socket readable inline. Other threads or modes
     * report false so the socket falls back to the eventfd notification.
     */
    if (g_tls_active_event_poll != this || !dispatch_caps_.epoll_wait_driven) {
        return false;
    }
    return RecordReadableSocket(&sock);
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

    auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    /* 建链中（异步 connect）：最终类型未定，先记录应用 events/data，待收尾按最终类型注册（延迟注册） */
    if (sock != nullptr && sock->State() == SOCK_STAT_CONNECTING) {
        auto sockBase = RefStaticCastToRaw<SocketBase>(sock);
        if (sockBase != nullptr) {
            sockBase->DeferEpollAdd(this, event->events, event->data);
            return 0;
        }
    }

    if (UNLIKELY(IsSocketEventDataExist(fd))) {
        UBS_VLOG_ERR("urma_event_poll EpollCtlAdd(socket=%d) already added.", fd);
        errno = EEXIST;
        return -1;
    }

    if (UNLIKELY(sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP || !sock->IsBindRemote())) {
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
    /* 建链中：仅更新延迟注册的 events/data，不碰内核（尚未注册） */
    if (sock != nullptr && sock->State() == SOCK_STAT_CONNECTING) {
        auto sockBase = RefStaticCastToRaw<SocketBase>(sock);
        if (sockBase != nullptr) {
            sockBase->UpdatePendingEpoll(event->events, event->data);
            return 0;
        }
    }

    if (UNLIKELY(sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP || !sock->IsBindRemote())) {
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
        urma_sock->FetchOrReachEpollEvents(notify_events);
    }

    /* Re-arm: wakeup refill. Pending events injected during disabled period only appear in reachability bits (no eventfd token),
     * blocked wait has no one to wake it, must check reach & mask match and refill token.
     * FetchOr(0) is RMW read, atomic serial with injector-side fetch_or, prevents "check-inject" window race;
     * when seed matches mask this check also applies, token write unified from here */
    uint32_t reach = urma_sock->FetchOrReachEpollEvents(0);
    if ((reach & event->events) != 0) {
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
    /* 建链中：本就没注册内核，仅清空延迟注册的 pending（不能走 IsSocketEventDataExist 的 ENOENT 分支） */
    if (sock != nullptr && sock->State() == SOCK_STAT_CONNECTING) {
        auto sockBase = RefStaticCastToRaw<SocketBase>(sock);
        if (sockBase != nullptr) {
            sockBase->ClearPendingEpoll();
            return 0;
        }
    }

    /* May run on another thread: ForgetFd clears only the pending mark; the drain drops the entry as stale. */
    readable_events_.ForgetFd(fd);
    if (UNLIKELY(sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP || !sock->IsBindRemote())) {
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
        errno = ENOENT;
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
        /* Kernel semantics: DEL on unregistered fd returns ENOENT, not silent success */
        UBS_VLOG_ERR("urma_event_poll del raw event for socket: %d not registered\n", fd);
        errno = ENOENT;
        return -1;
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

    /* This poll may now deliver the socket's readable events inline (see UrmaReadableNotifier). */
    socket->SetReadableNotifier(this);
    return 0;
}

int UrmaEventPoll::ModUrmaSocketEvent(UrmaSocketPtr &socket, struct epoll_event *event)
{
    auto event_data = GetSocketEventData(socket->raw_socket_);
    if (UNLIKELY(event_data == nullptr)) {
        /* Kernel semantics: MOD on unregistered fd returns ENOENT (consistent with DEL) */
        UBS_VLOG_ERR("urma_event_poll EpollCtlMod(socket: %d, event fd: %d) failed, event_data null",
                     socket->raw_socket_, socket->event_fd_);
        errno = ENOENT;
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
    event_data->oneshot_disabled.store(false, std::memory_order_release);
    return 0;
}

int UrmaEventPoll::DelUrmaSocketEvent(UrmaSocketPtr &socket)
{
    if (!RemoveSocketEventData(socket->raw_socket_)) {
        /* Kernel semantics: DEL unregistered fd returns ENOENT */
        UBS_VLOG_ERR("urma_event_poll del urma event for socket: %d not registered\n", socket->raw_socket_);
        errno = ENOENT;
        return -1;
    }

    /* The socket is no longer on this poll: drop our inline-reader hook if it points here. */
    if (socket->GetReadableNotifier() == this) {
        socket->SetReadableNotifier(nullptr);
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
