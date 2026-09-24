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
#ifndef UBS_COMM_URMA_SOCKET_H
#define UBS_COMM_URMA_SOCKET_H

#include <pthread.h>

#include "core/ubsocket_socket.h"
#include "urma_backend.h"
#include "urma_socket_buf.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaSocket;

struct UrmaSockSegExInfo; /* defined in urma_socket_seg_ex.h */

/*
 * Installed onto an UrmaSocket by the event poll it is registered on. When the socket becomes
 * readable, NotifySocketReadable asks the notifier to surface the event without a kernel wakeup
 * (inline record into the poll's pending-readable queue). The notifier returns false when it
 * cannot deliver inline (different thread, non-dispatcher poll, socket not registered there) and
 * the socket then falls back to writing its eventfd.
 *
 * Keeping this behind an interface keeps the socket layer independent of the poll implementation
 * (in particular of the poll's thread-local "active wait" bookkeeping).
 */
class UrmaReadableNotifier {
public:
    virtual ~UrmaReadableNotifier() = default;

    /* @return true if the readable event was delivered inline; false -> caller uses the eventfd. */
    virtual bool NotifyReadable(UrmaSocket &sock) noexcept = 0;
};

class UrmaSocket : public SocketBase {
public:
    explicit UrmaSocket(int fd) noexcept;
    ~UrmaSocket() override
    {
        UnInitialize();
    }

    Result Initialize() noexcept override;
    void UnInitialize() noexcept override;

    Result CreateUrmaResources();
    void DestroyUrmaResources();

    Result ExchangeSocketResources(bool send_first = true);

    /*
     * 全异步 server 建链专用入口（无 friend 注入通道，供外部状态机复用本 socket 资源状态）：
     * - BuildLocalSegExInfo: CreateUrmaResources() 完成后调用，填充本端段信息 wire 帧，
     *   并缓存本端控制面 target seg 供 ImportPeerSegments/CommitRemotePeer 使用。
     * - ImportPeerSegments: 收到对端段信息后调用（重活线程），导入对端 jetty、注册对端段、
     *   绑定本地收包 seg 并预投空 recv WR。
     * - CommitRemotePeer: 收到对端 ack 后调用（轻量），记录对端地址信息并置 bind-remote。
     * 以上方法与同步路径的阻塞流程互不共享，仅在本 socket 对象上做等价的状态推进。
     */
    Result BuildLocalSegExInfo(UrmaSockSegExInfo &local) noexcept;
    Result ImportPeerSegments(const UrmaSockSegExInfo &peer) noexcept;
    void CommitRemotePeer(const UrmaSockSegExInfo &peer) noexcept;

    UrmaJettyPtr Jetty() const
    {
        return jetty_;
    }

    bool IsBindRemote() override;

    void SetBindRemote(bool bound)
    {
        is_bind_remote_ = bound;
    }

    uint32_t GetNegotiatedVersion() const
    {
        return negotiated_version_;
    }

    void SetNegotiatedVersion(uint32_t version)
    {
        negotiated_version_ = version;
    }

    uint32_t GetPeerVersion() const
    {
        return peer_version_;
    }

    void SetPeerVersion(uint32_t version)
    {
        peer_version_ = version;
    }

    urma_eid_t GetPeerEid() const
    {
        return peer_eid_;
    }

    void SetPeerEid(const urma_eid_t &eid)
    {
        peer_eid_ = eid;
    }

    void SetTransMode(ub_trans_mode mode)
    {
        trans_mode = mode;
    }

    urma_transport_mode_t GetUrmaTransMode()
    {
        return UrmaTransModeOf(trans_mode);
    }

    urma_tp_type_t GetUrmaTpType()
    {
        return UrmaTpTypeOf(trans_mode);
    }

    void SetConnFlag(uint32_t flag)
    {
        conn_flag = flag;
    }

    bool DegradeEnable()
    {
        return (conn_flag & UBS_DEGRADABLE_MASK) != 0;
    }

    bool LinkSwitchEnable()
    {
        // TODO check used_port
        return (conn_flag & UBS_RETRYABLE_MASK) != 0 && false;
    }

    int NotifySocketReadable();
    int NotifyClosed();
    /* Closed state check: after NotifyClosed injects, HUP bit is persistent */
    bool IsClosed() const
    {
        return (GetReachEpollEvents() & EPOLLHUP) != 0;
    }

    void HandleRecvCqe(const WriteImmData &imm);
    void HandlePostWrite(const WriteImmData &imm);
    void HandleWriteInBandAck(const WriteImmData &imm);
    void HandlePreWrite(const WriteImmData &imm);
    void HandleWriteInBand(const WriteImmData &imm);
    void DataReadFinished(uint32_t seq_no);
    void PostEmptyRecvWorkerRecords(uint32_t count = 1);

    bool ExchangeSocketReadableReady(bool b)
    {
        return socket_readable_ready_.exchange(b, std::memory_order_acq_rel);
    }

    void SetSocketReadableReady(bool b)
    {
        socket_readable_ready_.store(b, std::memory_order_release);
    }

    void SetReachEpollEvents(uint32_t events)
    {
        reach_epoll_events_.store(events, std::memory_order_relaxed);
    }

    uint32_t FetchOrReachEpollEvents(uint32_t events)
    {
        /* acq_rel: pairs with clear-side FetchAnd via release/acquire,
         * ensures post-clear re-check sees pre-set state writes */
        return reach_epoll_events_.fetch_or(events, std::memory_order_acq_rel);
    }

    uint32_t FetchAndReachEpollEvents(uint32_t events)
    {
        /* acq_rel: pairs with set-side FetchOr via release/acquire,
         * ensures post-clear re-check sees notifier's prior state writes */
        return reach_epoll_events_.fetch_and(~events, std::memory_order_acq_rel);
    }

    uint32_t GetReachEpollEvents() const
    {
        return reach_epoll_events_.load(std::memory_order_relaxed);
    }

    /* The poll this socket is registered on may deliver readable events inline. */
    void SetReadableNotifier(UrmaReadableNotifier *notifier) noexcept
    {
        readable_notifier_.store(notifier, std::memory_order_release);
    }

    UrmaReadableNotifier *GetReadableNotifier() const noexcept
    {
        return readable_notifier_.load(std::memory_order_acquire);
    }

    /*
     * pthread_cond doorbell shared by blocking readv/writev: let blocked
     * threads sleep on I/O events instead of busy-polling the transport.
     */
    void RegisterBlockingWaiter() noexcept;
    void UnregisterBlockingWaiter() noexcept;

    /*
     * Sleep until an event of interest fires and return the bits that fired.
     * A return of 0 means the explicit deadline expired. When deadline is
     * nullptr, the wait rechecks every 2 minutes so an unset SO_*TIMEO cannot
     * stall forever on a lost wakeup. Only interest bits are consumed;
     * CLOSED/PEER_CLOSED stay sticky so a local close is never missed.
     */
    uint32_t WaitBlockingSignal(uint32_t interest_flags, const struct timespec *deadline) noexcept;

    void NotifyBlockingWritable() noexcept;
    void NotifyBlockingReadable() noexcept;

    /*
     * TODO: peer death surfaces only through error CQEs (HandleCqe, currently
     * log-only). Wire that branch here so blocked I/O stops waiting on a peer
     * that is gone.
     */
    void NotifyBlockingPeerClosed() noexcept;

    /*
     * Read SO_SNDTIMEO/SO_RCVTIMEO (sec + usec) and convert it into an absolute
     * CLOCK_MONOTONIC deadline. Returns false when the option is unset
     * ({0, 0}) or getsockopt fails.
     */
    bool GetBlockTimeoutDeadline(int optname, struct timespec &deadline) noexcept;

    void NotifyLocalClose() noexcept override;

    UrmaContextPtr GetContext() noexcept
    {
        return context_;
    }

protected:
    int DoNotifyWritable() override;

private:
    int DeliverReadable();
    Result DoExchangeSocketResources(bool send_first);

    UrmaJettyPtr jetty_;      /* could be dedicated jetty or shared jetty */
    uintptr_t send_buf_addr_; /* dedicate send buffer, i.e. not shared */
    uintptr_t recv_buf_addr_; /* dedicate send buffer, i.e. not shared */
    uintptr_t rmt_send_buf_addr_;
    uintptr_t rmt_recv_buf_addr_;
    uint32_t send_buf_size_;           /* size of dedicate send buffer, max size 4GB */
    uint32_t recv_buf_size_;           /* size of dedicate recv buffer, max size 4GB */
    UrmaSockBufPtr send_buf_{nullptr}; /* local/send buffer and remote buffer allocator */
    UrmaContextPtr context_{nullptr};
    UrmaJfcPtr jfc_{nullptr};
    UrmaJfsPtr jfs_{nullptr};
    UrmaJfrPtr jfr_{nullptr};
    urma_target_seg_t *local_ctrl_target_seg_{nullptr};
    urma_target_seg_t *remote_ctrl_target_seg_{nullptr};
    RemoteSegmentsPtr remote_target_segs_{nullptr};

    ub_trans_mode trans_mode{RM_CTP};
    uint32_t conn_flag{0};

    urma_eid_t local_eid_{};
    urma_eid_t peer_eid_{};

    size_t recv_data_size_{0};
    size_t recv_data_offset_{0};
    bool recv_data_ready_{false};

    bool is_bind_remote_{false};
    uint32_t remote_uasid_{0};
    uint32_t negotiated_version_{0};
    uint32_t peer_version_{0};
    std::atomic<uint32_t> reach_epoll_events_{0};
    std::atomic<bool> socket_readable_ready_{false};                 // socket初始状态为不可读
    std::atomic<UrmaReadableNotifier *> readable_notifier_{nullptr}; /* poll that may deliver readable inline */

    urma_sge_t empty_recv_sge_;
    urma_jfr_wr_t empty_recv_wr_;

    /* Blocking predicate bits, guarded by block_mutex_. */
    enum BlockingFlag : uint32_t
    {
        BLOCKING_FLAG_WRITABLE = 1U << 0,    /* producer doorbell: send buffer freed (writev) */
        BLOCKING_FLAG_CLOSED = 1U << 1,      /* local close */
        BLOCKING_FLAG_PEER_CLOSED = 1U << 2, /* peer close (future URMA error event hook) */
        BLOCKING_FLAG_READABLE = 1U << 3,    /* producer doorbell: data arrived (readv) */
    };

    /* predicate and condvar are guarded by block_mutex_. */
    pthread_mutex_t block_mutex_;
    pthread_cond_t block_cond_; /* initialized with CLOCK_MONOTONIC */
    std::atomic<uint32_t> block_waiters_{0};
    std::atomic<uint32_t> block_flags_{0}; /* predicate bits: BLOCKING_FLAG_* */

    friend class UrmaAcceptorOps;
    friend class UrmaConnectorOps;
    friend class UrmaTxOps;
    friend class UrmaRxOps;
    friend class BlockingWaiter;    /* shared blocking readv/writev wait bookkeeping (BLOCKING_FLAG_*) */
    friend class QueueRxDispatcher; /* drains deferred events: re-reads seq from recv_buf */
};
using UrmaSocketPtr = Ref<UrmaSocket>;

ALWAYS_INLINE int UrmaSocket::DoNotifyWritable()
{
    PROF_START(___URMA_EVENT_WRITE);
    FetchOrReachEpollEvents(EPOLLOUT);
    auto ret = eventfd_write(event_fd_, 1);
    PROF_END(___URMA_EVENT_WRITE, true);
    return ret;
}

ALWAYS_INLINE int UrmaSocket::NotifyClosed()
{
    PROF_START(___URMA_EVENT_CLOSED);
    FetchOrReachEpollEvents(EPOLLHUP | EPOLLRDHUP);
    auto ret = eventfd_write(event_fd_, 1);
    PROF_END(___URMA_EVENT_CLOSED, true);
    return ret;
}

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SOCKET_H
