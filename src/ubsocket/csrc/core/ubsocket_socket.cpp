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
#include "ubsocket_socket.h"
#include "ubsocket_conn_reactor.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_raw_socket.h"
#include "ubsocket_tx_cqe_poller.h"
#include "urma/urma_conn_ops.h"
#include "urma/urma_data_rx_ops.h"
#include "urma/urma_data_tx_ops.h"
#include "urma/urma_socket.h"
#include "urma/urma_socket_acceptor_ops.h"
#include "urma/urma_socket_connector_ops.h"

namespace ock {
namespace ubs {
namespace {
/*
 * POSIX flags parsed by the UB managed data path. Any other bit - supported by send/recv but not
 * implemented here, or undefined - is rejected with -1/EINVAL instead of being silently ignored.
 */
constexpr int MANAGED_SEND_FLAG_MASK = MSG_DONTWAIT | MSG_NOSIGNAL;
constexpr int MANAGED_RECV_FLAG_MASK = MSG_DONTWAIT | MSG_WAITALL | MSG_NOSIGNAL;

} // namespace

ssize_t SocketBase::Send(const SocketPtr &sock, const void *buf, size_t len, int flags)
{
    if (UNLIKELY(sock->State() == SOCK_STAT_RAW_ESTABLISHED)) {
        return LibcApi::send(sock->Fd(), buf, len, flags);
    }

    if (UNLIKELY((flags & ~MANAGED_SEND_FLAG_MASK) != 0)) {
        errno = EINVAL;
        UBS_VLOG_WARN("send unsupported flags, fd: %d, flags: %#x, supported: %#x\n", sock->Fd(), flags,
                      MANAGED_SEND_FLAG_MASK);
        return -1;
    }

    uint32_t ioFlags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    if ((flags & MSG_DONTWAIT) != 0) {
        ioFlags |= UBS_IO_FLAG_DONT_WAIT;
    }

    struct iovec iov = {const_cast<void *>(buf), len};
    return tx_.WriteV(sock, &iov, 1, ioFlags);
}

ssize_t SocketBase::Recv(const SocketPtr &sock, void *buf, size_t len, int flags)
{
    if (UNLIKELY(sock->State() == SOCK_STAT_RAW_ESTABLISHED)) {
        return LibcApi::recv(sock->Fd(), buf, len, flags);
    }

    if (UNLIKELY((flags & ~MANAGED_RECV_FLAG_MASK) != 0)) {
        errno = EINVAL;
        UBS_VLOG_WARN("recv unsupported flags, fd: %d, flags: %#x, supported: %#x\n", sock->Fd(), flags,
                      MANAGED_RECV_FLAG_MASK);
        return -1;
    }

    const bool dontWait = (flags & MSG_DONTWAIT) != 0;
    const bool waitAll = (flags & MSG_WAITALL) != 0;
    uint32_t ioFlags = UBS_IO_FLAG_USE_BRPC_BLOCK;
    if (dontWait) {
        ioFlags |= UBS_IO_FLAG_DONT_WAIT;
    }

    size_t total = 0;
    while (total < len) {
        struct iovec iov = {static_cast<char *>(buf) + total, len - total};
        ssize_t ret = rx_.ReadV(sock, &iov, 1, ioFlags);
        if (ret > 0) {
            total += static_cast<size_t>(ret);
            if (!waitAll || dontWait) {
                return static_cast<ssize_t>(total);
            }
            continue; /* MSG_WAITALL on a blocking fd: keep reading until len is filled */
        }
        if (ret == 0) { /* EOF */
            return total > 0 ? static_cast<ssize_t>(total) : 0;
        }
        /* error/EAGAIN: bytes already read are returned, otherwise the error is passed through */
        return total > 0 ? static_cast<ssize_t>(total) : -1;
    }
    return static_cast<ssize_t>(total);
}

Result SocketBase::Create(int fd, ock::ubs::SocketType t, SocketPtr &outSocket)
{
    if (t == SocketType::SOCK_TYPE_TCP) {
        auto rawSock = MakeRef<RawSocket>(fd);
        if (rawSock == nullptr) {
            return UBS_MALLOC_FAILED;
        }
        rawSock->State(SocketState::SOCK_STAT_RAW_ESTABLISHED);
        auto sock = RefStaticCast<Socket>(rawSock);
        outSocket = sock;
        return UBS_OK;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (t == SocketType::SOCK_TYPE_URMA) {
        using namespace urma;
        auto urmaSock = MakeRef<UrmaSocket>(fd);
        if (urmaSock == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        auto result = urmaSock->Initialize();
        if (result != UBS_OK) {
            return result;
        }

        auto sock = RefStaticCast<Socket>(urmaSock);
        auto sockBase = RefStaticCast<SocketBase>(urmaSock);

        AcceptorOps *acceptorOps = nullptr;
        result = CreateAcceptorOps(t, sock, acceptorOps);
        if (result != UBS_OK) {
            return result;
        }

        ConnectorOps *connectorOps = nullptr;
        result = CreateConnectorOps(t, sock, connectorOps);
        if (UNLIKELY(result != UBS_OK)) {
            delete acceptorOps;
            return result;
        }

        sockBase->acceptor_ = new (std::nothrow) Acceptor(sock, acceptorOps);
        sockBase->connector_ = new (std::nothrow) Connector(sock, connectorOps);

        outSocket = sock;
        return UBS_OK;
    }
#endif

    return UBS_INVALID_PARAM;
} // namespace ubs

Result SocketBase::GenerateSocketCommOps(const SocketPtr &sock)
{
    auto sockBase = RefStaticCastToRaw<SocketBase>(sock);
    DataTxOps *txOps = nullptr;
    Result result = CreateTxOps(sock->type_, sock, txOps);
    if (UNLIKELY(result != UBS_OK)) {
        return result;
    }

    DataRxOps *rxOps = nullptr;
    result = CreateRxOps(sock->type_, sock, rxOps);
    if (UNLIKELY(result != UBS_OK)) {
        delete txOps;
        return result;
    }

    sockBase->tx_ = DataTx(sock, txOps);
    sockBase->rx_ = DataRx(sock, rxOps);
    return UBS_OK;
}

Result SocketBase::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (value == SocketType::SOCK_TYPE_URMA) {
        return urma::UrmaBackend::CreateTxOps(value, sock, ops);
    }
#endif

    /* invalid type */
    return UBS_INVALID_PARAM;
}

Result SocketBase::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (value == SocketType::SOCK_TYPE_URMA) {
        return urma::UrmaBackend::CreateRxOps(value, sock, ops);
    }
#endif

    /* invalid type */
    return UBS_INVALID_PARAM;
}
Result SocketBase::CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptorOps)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (value == SocketType::SOCK_TYPE_URMA) {
        return urma::UrmaBackend::CreateAcceptorOps(value, sock, acceptorOps);
    }
#endif
    /* invalid type */
    return UBS_INVALID_PARAM;
}

Result SocketBase::CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connectorOps)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    if (value == SocketType::SOCK_TYPE_URMA) {
        return urma::UrmaBackend::CreateConnectorOps(value, sock, connectorOps);
    }
#endif
    /* invalid type */
    return UBS_INVALID_PARAM;
}

Result SocketBase::CreateConnOps(ConnRole role, int fd, const SocketPtr &sock, const ConnectorOpsPtr &connector_ops,
                                 const struct sockaddr &addr, socklen_t addr_len, SocketType t,
                                 std::shared_ptr<UBConnOpsBase> &ops)
{
    if (t != SocketType::SOCK_TYPE_URMA) {
        UBS_VLOG_ERR("CreateConnOps unsupported socket type: %d\n", static_cast<int>(t));
        return UBS_INVALID_PARAM;
    }
    if (role == ConnRole::kClient && (sock == nullptr || connector_ops == nullptr)) {
        return UBS_INVALID_PARAM;
    }

#ifdef URMA_DLOPEN_BACKEND_ENABLED
    std::shared_ptr<UBConnOpsBase> conn_ops =
        std::make_shared<urma::UrmaConnOps>(role, fd, sock, connector_ops, addr, addr_len);
    if (conn_ops == nullptr) {
        return UBS_MALLOC_FAILED;
    }
    ops = std::move(conn_ops);
    return UBS_OK;
#else
    (void)role; (void)fd; (void)sock; (void)connector_ops; (void)addr; (void)addr_len;
    return UBS_INVALID_PARAM;
#endif
}

int SocketBase::ConnectAsync(SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    if (connector_ == nullptr || sock.Get() == nullptr || address == nullptr) {
        return UBS_ERROR;
    }
    ConnectorOpsPtr connector_ops = connector_->GetConnectorOps();
    if (connector_ops == nullptr) {
        return UBS_ERROR;
    }

    /* 进入异步建链：State=CONNECTING、清 writable_ready_（§8.4 初值陷阱）、清 events 与 connect_error_ */
    sock->State(SOCK_STAT_CONNECTING);
    SetWritableReady(false);
    SetEvents(0);
    connect_error_.store(0, std::memory_order_release);

    std::shared_ptr<UBConnOpsBase> ops;
    Result ret =
        CreateConnOps(ConnRole::kClient, raw_socket_, sock, connector_ops, *address, address_len, sock->Type(), ops);
    if (ret == UBS_OK) {
        ret = ConnDispatcher::Instance().Submit(raw_socket_, *address, address_len, ConnReactorThread::DoneCallback(),
                                                ops);
    }
    if (ret != UBS_OK) {
        /* 提交失败：回退同步 connect，调用方据返回值决定后续 */
        sock->State(SOCK_STAT_INIT);
        return ret;
    }
    return 0; /* 提交成功：调用方返回 -1 且 errno=EINPROGRESS */
}

void SocketBase::DeferEpollAdd(EventPoll *ep, uint32_t events, const epoll_data_t &data)
{
    bool need_register = false;
    {
        std::lock_guard<std::mutex> lock(pending_epoll_mutex_);
        pending_epoll_ = ep;
        pending_epoll_events_ = events;
        pending_epoll_data_ = data;
        /* 写后读：若收尾已切终态，则自行消费 pending（与收尾的读后写构成双重检查） */
        need_register = (state_ != SOCK_STAT_CONNECTING);
    }
    if (need_register) {
        RegisterPendingEpollAdd();
    }
}

void SocketBase::UpdatePendingEpoll(uint32_t events, const epoll_data_t &data)
{
    std::lock_guard<std::mutex> lock(pending_epoll_mutex_);
    if (pending_epoll_ != nullptr) {
        pending_epoll_events_ = events;
        pending_epoll_data_ = data;
    }
}

void SocketBase::ClearPendingEpoll()
{
    std::lock_guard<std::mutex> lock(pending_epoll_mutex_);
    pending_epoll_ = nullptr;
    pending_epoll_events_ = 0;
    pending_epoll_data_ = {};
}

void SocketBase::RegisterPendingEpollAdd()
{
    EventPoll *ep = nullptr;
    uint32_t events = 0;
    epoll_data_t data = {};
    {
        std::lock_guard<std::mutex> lock(pending_epoll_mutex_);
        if (pending_epoll_ == nullptr) {
            return; /* 应用尚未 ADD，无需注册 */
        }
        ep = pending_epoll_;
        events = pending_epoll_events_;
        data = pending_epoll_data_;
        /* 先清，避免重入 EEXIST；此时 ArraySet[fd] 已是最终类型（UB/降级 TCP/失败） */
        pending_epoll_ = nullptr;
        pending_epoll_events_ = 0;
        pending_epoll_data_ = {};
    }

    struct epoll_event ev = {};
    ev.events = events;
    ev.data = data;
    (void)ep->EpollCtl(EPOLL_CTL_ADD, raw_socket_, &ev);
}

void SocketBase::NotifyConnectFailed()
{
    {
        std::lock_guard<std::mutex> lock(pending_epoll_mutex_);
        if (pending_epoll_ != nullptr) {
            pending_epoll_events_ |= EPOLLERR;
        }
    }
    RegisterPendingEpollAdd();
}

int SocketBase::GetSockOpt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (optval == nullptr || optlen == nullptr) {
        errno = EINVAL;
        UBS_VLOG_ERR("getsockopt optval or optlen is null\n");
        return -1;
    }

    if (level == static_cast<int>(UbsocketLevel::SOL_UB)) {
        switch (static_cast<UbSocketOpt>(optname)) {
            case UbSocketOpt::UBS_OPT_PROTOCOL: {
                int connectType = static_cast<int>(type_);
                memcpy(optval, &connectType, sizeof(int));
                *optlen = sizeof(int);
                return 0;
            }
            default: {
                errno = EINVAL;
                return -1;
            }
        }
    }
    errno = EINVAL;
    return -1;
}
} // namespace ubs
} // namespace ock
