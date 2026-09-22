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
#include "core/umq/umq_data_plane.h"
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
    /* 握手期上下文：acceptor 壳 + 惰性 connector。两者只在握手期有意义——
     * UMQ 链路在 OnEstablished 收尾时整体释放（监听/TCP 链路随析构释放），
     * 稳态链路不再为 24B Acceptor 壳 + connector 指针付费。 */
    struct HandshakeCtx {
        Acceptor acceptor;              /* acceptor of ubsocket */
        Connector *connector = nullptr; /* connector of ubsocket（Connect 首次调用惰性创建） */
        ~HandshakeCtx()
        {
            if (connector != nullptr) {
                delete connector;
                connector = nullptr;
            }
        }
    };


    static Result Create(int fd, SocketType t, SocketPtr &sock);

    static Result GenerateSocketCommOps(const SocketPtr &sock);

public:
    /* Per-link statistics (8 recorders, 72 B) only exist when UBS_MONITOR_ENABLE:
     * every UpdateTraceStats call site is gated by that flag, and the flag is
     * fixed at process init, so a socket created with tracing off can never be
     * asked to record. Allocated once here (not lazily on the data path); the
     * CLI/output readers tolerate a null manager. Saves 64 B per link when
     * tracing is off (the common production setting). */
    SocketBase(int fd, SocketType type) : Socket(fd, type)
    {
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            SocketExt *ext = EnsureExt();
            if (ext != nullptr) {
                ext->stats_mgr = new (std::nothrow) Statistics::StatsMgr();
            }
        }
    }

    ~SocketBase() override
    {
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            Statistics::StatsMgr::SubMConnCount();
            if (IsClient()) {
                Statistics::StatsMgr::SubMActiveConnCount();
            }
        }
        SocketExt *ext = ExtOrNull();
        if (ext != nullptr) {
            /* 本层拥有的侧车槽：握手上下文与 trace 统计（侧车本体由 ~Socket 释放） */
            delete static_cast<HandshakeCtx *>(ext->hs);
            ext->hs = nullptr;
            delete ext->stats_mgr;
            ext->stats_mgr = nullptr;
        }
    }

    /* 失败路径上主动释放本 socket 占用的数据面条目。默认空实现（非 UMQ 类型无条目）。
     * 见 UmqSocket 的覆写：清槽必须发生在 fd 可能被复用之前。 */
    virtual void ReleaseDataPlane() noexcept {}
    /* 建链失败出口：umq 若从未 bind（无在途 WR），当场销毁、立刻归还 id，不等 socket
     * 对象析构。bind 过的 umq 仍走析构侧的 flush + destroy。 */
    virtual void DiscardUnboundUmq() noexcept {}

    virtual Result Initialize() noexcept = 0;
    virtual void UnInitialize() noexcept = 0;

    /* 建链成功钩子：accept/connect 成功后由核心层调用一次。派生层在此收拢
     * 建链期仍需保留的数据并释放协商 ops（见 UmqSocket::OnEstablished）。 */
    virtual void OnEstablished() {}

    int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);
    int Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    int WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);
    int ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);
    int GetSockOpt(int fd, int level, int optname, void *optval, socklen_t *optlen);
    int SetSockOpt(int fd, int level, int optname, const void *optval, socklen_t optlen);

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

    // 可写时设置 bit0 为 1，且增加版本号，必定成功
    bool SetWritableReady()
    {
        uint32_t new_state = 0;
        uint32_t old = versioned_writable_ready_.load(std::memory_order_relaxed);
        do {
            new_state = ((Version(old) + 1) << 1) | 1;
        } while (!versioned_writable_ready_.compare_exchange_weak(old, new_state, std::memory_order_acq_rel,
                                                                  std::memory_order_relaxed));
        return true;
    }

    // 不可写时仅尝试标记 bit0 为 0，允许失败
    bool SetNotWritableReadyIfUnchanged(uint32_t pre)
    {
        uint32_t new_state = (Version(pre) << 1) | 0;
        return versioned_writable_ready_.compare_exchange_strong(pre, new_state, std::memory_order_acq_rel,
                                                                 std::memory_order_relaxed);
    }

    // bit0 存在 ABA 回绕问题。触发概率非常小，需要在 ~10 cycles 下 NotifyWritable 通知 2**31 次
    bool ReadyAndExchange()
    {
        uint32_t old = versioned_writable_ready_.load(std::memory_order_relaxed);
        if (Ready(old)) {
            const uint32_t new_state = (Version(old) << 1) | 0;
            return versioned_writable_ready_.compare_exchange_strong(old, new_state, std::memory_order_acq_rel,
                                                                     std::memory_order_relaxed);
        }
        return false;
    }

    uint32_t GetVersionedWritableReady(std::memory_order m = std::memory_order_relaxed)
    {
        return versioned_writable_ready_.load(m);
    }

    uint32_t Version(std::memory_order m = std::memory_order_relaxed) const
    {
        return Version(versioned_writable_ready_.load(m));
    }

    static uint32_t Version(uint32_t v)
    {
        return v >> 1;
    }

    bool Ready(std::memory_order m = std::memory_order_relaxed) const
    {
        return Ready(versioned_writable_ready_.load(m));
    }

    static bool Ready(uint32_t v)
    {
        return static_cast<bool>(v & 1);
    }

    int NotifyReadable(bool epollout = false);
    int NotifyWritable();

    /* RNR backpressure gate: transport implementations (e.g. UmqSocket) override
     * this to suppress EPOLLOUT while the receiver is not ready. Default returns
     * true so pure-TCP / non-RNR transports are unaffected. */
    virtual bool CanNotifyWritable() const
    {
        return true;
    }

    /* RNR 反压 fatal 断链认领钩子：写路径 (writev/ubs_post) 因反压返回 EAGAIN 时
     * 调用，顺带检查反压超时；默认无操作，UmqSocket 重载后执行断链。
     * 返回 true 表示本线程已执行断链（shutdown + State(CLOSE)）。 */
    virtual bool FatalIfWriteBlocked()
    {
        return false;
    }

    /* 数据面壳/ops 直达访问器：条目未装配（握手前/表分配失败）时为 nullptr。
     * 已建链上下文（poller/bigdata/数据路径）条目恒存在。 */
    DataRx *GetRx()
    {
        umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
        return e != nullptr ? &e->rxw : nullptr;
    }
    DataTx *GetTx()
    {
        umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
        return e != nullptr ? &e->txw : nullptr;
    }
    /* 经壳转发而非直取条目内 ops：生产路径两者等价（Generate 把壳指向条目），
     * 但保留壳指针这一"可替换接缝"——UT 以栈上 mock ops 注入壳（见
     * tx_unified_poller UT），直取会把 mock 短路。 */
    DataTxOps *GetTxOps()
    {
        umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
        return e != nullptr ? e->txw.GetTxOps() : nullptr;
    }
    DataRxOps *GetRxOps()
    {
        umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
        return e != nullptr ? e->rxw.GetRxOps() : nullptr;
    }
    /* nullptr when UBS_MONITOR_ENABLE is off — callers on the data path are
     * gated by that flag; CLI/output readers must null-check. */
    Statistics::StatsMgr *GetStatsMgr()
    {
        SocketExt *ext = ExtOrNull();
        return ext != nullptr ? ext->stats_mgr : nullptr;
    }

    /* 握手期上下文槽（存于 SocketExt，类型由本层收敛） */
    HandshakeCtx *Hs() const noexcept
    {
        SocketExt *ext = ExtOrNull();
        return ext != nullptr ? static_cast<HandshakeCtx *>(ext->hs) : nullptr;
    }

private:
    int DoNotifyWritable();

protected:
    static Result CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor);
    static Result CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector);
    Result EnsureConnector(const SocketPtr &sock);

protected:
    /* Which host epoll this socket is registered in (nullptr = not yet). Read
     * lock-free by the share-JFR RX runner while another thread is doing the
     * epoll_ctl(ADD): see the handoff protocol at UmqSocket::SetAddedEpollFd. */
    std::atomic<EventPoll *> added_epoll_fd_{nullptr};
    std::atomic<uint32_t> events_{0};                     // 上层关注的 epoll events 事件
    epoll_data_t added_epoll_data_ = {};                  // 上层关注的 epoll data (written before
                                                          // added_epoll_fd_ is published, read after it is seen)
    std::atomic<uint32_t> versioned_writable_ready_{0x1}; // bit0 为 1 表示已经接收到对端的流控回复报文、poll
        // tx 已释放出资源，可写。其余 bit1-bit31 表示版本号，
        // 用以判断在 umq_post 过程中如果有可写通知的情况

    friend class DataTx;
    friend class DataRx;
    friend class Acceptor;
    friend class Connector;
};

ALWAYS_INLINE int SocketBase::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    HandshakeCtx *hs = Hs();
    if (hs == nullptr || !hs->acceptor.HasOps()) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return hs->acceptor.Accept(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    /* connector 惰性创建：首次 connect 时构建（accepted 链路不再为其付出 ~0.3KB） */
    if ((Hs() == nullptr || Hs()->connector == nullptr) && EnsureConnector(sock) != UBS_OK) {
        errno = ENOMEM;
        return UBS_ERROR;
    }

    return Hs()->connector->Connect(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    /* TCP 直通在此短路（原在 DataTx::WriteV 内）；UMQ 链路经数据面条目 */
    if (State() == SOCK_STAT_RAW_ESTABLISHED) {
        return LibcApi::writev(raw_socket_, iov, iovcnt);
    }
    umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
    if (e == nullptr) {
        errno = ENOTCONN;
        return UBS_ERROR;
    }
    return e->txw.WriteV(sock, iov, iovcnt);
}

ALWAYS_INLINE int SocketBase::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    if (State() == SOCK_STAT_RAW_ESTABLISHED) {
        return LibcApi::readv(raw_socket_, iov, iovcnt);
    }
    umq::DataPlaneEntry *e = umq::DataPlaneTable::Live(raw_socket_);
    if (e == nullptr) {
        errno = ENOTCONN;
        return UBS_ERROR;
    }
    return e->rxw.ReadV(sock, iov, iovcnt);
}

ALWAYS_INLINE EventPoll *SocketBase::GetAddedEpollFd(epoll_data_t &data) const
{
    /* fd first (acquire), data second: whoever sees the fd also sees the data
     * that was stored before it was published. The old order (data, then fd)
     * could hand a dispatcher a valid epoll with a stale/zero epoll_data. */
    EventPoll *fd = added_epoll_fd_.load(std::memory_order_acquire);
    data = added_epoll_data_;
    return fd;
}

ALWAYS_INLINE void SocketBase::SetAddedEpollFd(EventPoll *fd, const epoll_data_t &data)
{
    added_epoll_data_ = data;
    added_epoll_fd_.store(fd, std::memory_order_release);
}

ALWAYS_INLINE int SocketBase::NotifyReadable(bool epollout)
{
    auto *ep = static_cast<AsyncEventPoll *>(added_epoll_fd_.load(std::memory_order_acquire));
    if (ep == nullptr) {
        UBS_VLOG_WARN("NotifyReadable: socket not registered to any epoll, notification dropped\n");
        return -1;
    }

    if (epollout && !(GetEvents() & EPOLLOUT)) {
        UBS_VLOG_DEBUG("An EPOLLOUT event generated even if the socket(fd=%d) is not interested in EPOLLOUT", Fd());
    }

    const uint32_t events = EPOLLIN | (epollout ? +EPOLLOUT : 0);
    if (TryDirectDispatchEvent(events, added_epoll_data_)) {
        return 0;
    }

    if (ep->AddReadableEvent(events, added_epoll_data_) != 0) {
        return -1;
    }

    return ep->SetReadableEventFd();
}

ALWAYS_INLINE int SocketBase::NotifyWritable()
{
    // RNR 反压期间不主动产生 EPOLLOUT 事件
    if (!CanNotifyWritable()) {
        return 0;
    }

    const uint32_t current = events_.load(std::memory_order_acquire);

    // 如果上层还未关注 EPOLLOUT 事件，说明 UB 链路上的流控回复报文先于 `epoll_ctl(.., MOD, ..)` 到达了.
    if (!(current & EPOLLOUT)) {
        // 版本号+1 且可写
        SetWritableReady();

        // 如果另外一个线程 EpollCtlMod 也修改了 events...
        if (events_.load(std::memory_order_acquire) & EPOLLOUT) {
            // 尝试与 EpollCtlMod 竞争谁来做 DoNotifyWritable() 的工作
            if (ReadyAndExchange()) {
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
    auto *ep = static_cast<AsyncEventPoll *>(added_epoll_fd_.load(std::memory_order_acquire));
    if (ep == nullptr) {
        UBS_VLOG_WARN("DoNotifyWritable: socket not registered to any epoll, notification dropped\n");
        return -1;
    }

    if (TryDirectDispatchEvent(EPOLLOUT, added_epoll_data_)) {
        return 0;
    }

    if (ep->AddReadableEvent(EPOLLOUT, added_epoll_data_) != 0) {
        return -1;
    }

    return ep->SetReadableEventFd();
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
