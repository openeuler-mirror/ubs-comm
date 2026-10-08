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
#ifndef UBS_COMM_UBSOCKET_CONNECTION_H
#define UBS_COMM_UBSOCKET_CONNECTION_H

#include <sys/socket.h>
#include <chrono>

#include "common/ubsocket_common_includes.h"
#include "include/ubsocket_def.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "ubsocket_core_types.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_event_epoll.h"
#include "ubsocket_socket_acceptor.h"
#include "ubsocket_socket_connector.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {
class SocketBase;
using SocketBasePtr = Ref<SocketBase>;

class SocketBase : public Socket {
public:
    static Result Create(int fd, SocketType t, SocketPtr &sock);

    static Result GenerateSocketCommOps(const SocketPtr &sock);

public:
    SocketBase(int fd, SocketType type) : Socket(fd, type)
    {
        stats_mgr_.InitStatsMgr();
    }
    ~SocketBase() override
    {
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            Statistics::StatsMgr::SubMConnCount();
            if (IsClient()) {
                Statistics::StatsMgr::SubMActiveConnCount();
            }
        }
    }

    virtual Result Initialize() noexcept = 0;
    virtual void UnInitialize() noexcept = 0;

    int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);
    int Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    int WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);
    int ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);
    int GetSockOpt(int fd, int level, int optname, void *optval, socklen_t *optlen);

    EventPoll *GetAddedEpollFd(epoll_data_t &data) const;
    virtual void SetAddedEpollFd(EventPoll *fd, const epoll_data_t &data = {});

    void SetEvents(uint32_t events)
    {
        events_.store(events, std::memory_order_release);
    }

    uint32_t GetEvents() const
    {
        return events_.load(std::memory_order_acquire);
    }

    void SetEpollData(epoll_data_t data)
    {
        added_epoll_data_ = data;
    }

    epoll_data_t GetEpollData() const
    {
        return added_epoll_data_;
    }

    bool ExchangeWritableReady(bool b)
    {
        return writable_ready_.exchange(b, std::memory_order_acq_rel);
    }

    void SetWritableReady(bool b)
    {
        return writable_ready_.store(b, std::memory_order_release);
    }

    int NotifyReadable(bool epollout = false);
    int NotifyWritable();

    DataRx *GetRx()
    {
        return &rx_;
    }
    DataTx *GetTx()
    {
        return &tx_;
    }
    Statistics::StatsMgr *GetStatsMgr()
    {
        return &stats_mgr_;
    }

    bool IsClient()
    {
        return connector_->IsClient();
    }

private:
    int DoNotifyWritable();

protected:
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops);
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops);
    static Result CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor);
    static Result CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector);

protected:
    DataTx tx_;                      /* take charge of send */
    DataRx rx_;                      /* take charge of receive */
    Acceptor *acceptor_ = nullptr;   /* acceptor of ubsocket */
    Connector *connector_ = nullptr; /* connector of ubsocket */
    EventPoll *added_epoll_fd_ = nullptr;
    std::atomic<uint32_t> events_{0};        // 上层关注的 epoll events 事件
    epoll_data_t added_epoll_data_ = {};     // 上层关注的 epoll data
    std::atomic<bool> writable_ready_{true}; // 为 true 表示已经接收到对端的流控回复报文，可写

    Statistics::StatsMgr stats_mgr_ = {};

    friend class DataTx;
    friend class DataRx;
    friend class Acceptor;
    friend class Connector;
};

ALWAYS_INLINE int SocketBase::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    if (acceptor_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return acceptor_->Accept(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    if (connector_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return connector_->Connect(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    return tx_.WriteV(sock, iov, iovcnt);
}

ALWAYS_INLINE int SocketBase::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    return rx_.ReadV(sock, iov, iovcnt);
}

ALWAYS_INLINE EventPoll *SocketBase::GetAddedEpollFd(epoll_data_t &data) const
{
    data = added_epoll_data_;
    return added_epoll_fd_;
}

ALWAYS_INLINE void SocketBase::SetAddedEpollFd(EventPoll *fd, const epoll_data_t &data)
{
    added_epoll_fd_ = fd;
    added_epoll_data_ = data;
}

ALWAYS_INLINE int SocketBase::NotifyReadable(bool epollout)
{
    if (added_epoll_fd_ == nullptr) {
        return eventfd_write(event_fd_, 1);
    }

    if (epollout && !(GetEvents() & EPOLLOUT)) {
        UBS_VLOG_ERR("An EPOLLOUT event generated even if the socket(fd=%d) is not interested in EPOLLOUT", Fd());
    }

    auto *ep = static_cast<AsyncEventPoll *>(added_epoll_fd_);
    if (ep->AddReadableEvent(EPOLLIN | (epollout ? +EPOLLOUT : 0), added_epoll_data_) != 0) {
        return -1;
    }

    return ((AsyncEventPoll *)added_epoll_fd_)->SetReadableEventFd();
}

ALWAYS_INLINE int SocketBase::NotifyWritable()
{
    const uint32_t current = events_.load(std::memory_order_acquire);

    // 如果上层还未关注 EPOLLOUT 事件，说明 UB 链路上的流控回复报文先于 `epoll_ctl(.., MOD, ..)` 到达了.
    if (!(current & EPOLLOUT)) {
        writable_ready_.store(true, std::memory_order_release);
        // 如果另外一个线程 EpollCtlMod 也修改了 events...
        if (events_.load(std::memory_order_acquire) & EPOLLOUT) {
            if (writable_ready_.exchange(false, std::memory_order_acq_rel)) {
                return DoNotifyWritable();
            }
        }
        return 0;
    }

    // 上层已关注 EPOLLOUT 事件
    return DoNotifyWritable();
}

ALWAYS_INLINE int SocketBase::DoNotifyWritable()
{
    if (added_epoll_fd_ == nullptr) {
        return eventfd_write(event_fd_, 1);
    }

    auto *ep = static_cast<AsyncEventPoll *>(added_epoll_fd_);
    if (ep->AddReadableEvent(EPOLLOUT, added_epoll_data_) != 0) {
        return -1;
    }

    return ((AsyncEventPoll *)added_epoll_fd_)->SetReadableEventFd();
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
