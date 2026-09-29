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

#include <errno.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"
#include "under_api/urma/dl_urma_api.h"
#include "urma_backend.h"
#include "urma_data_rx_ops.h"
#include "urma_data_tx_ops.h"
#include "urma_socket.h"
#include "urma_socket_seg_ex.h"

namespace ock {
namespace ubs {
namespace urma {

UrmaSocket::UrmaSocket(int fd) noexcept : SocketBase(fd, SocketType::SOCK_TYPE_URMA)
{
    empty_recv_sge_.addr = 0;
    empty_recv_sge_.len = 0;
    empty_recv_sge_.tseg = nullptr;
    empty_recv_sge_.user_tseg = nullptr;

    empty_recv_wr_.src.sge = &empty_recv_sge_;
    empty_recv_wr_.src.num_sge = 1;
    empty_recv_wr_.user_ctx = SenderCRContext{CTRL_DATA_RECEiVE, 0, fd}.value;
    empty_recv_wr_.next = nullptr;

    pthread_mutex_init(&block_mutex_, nullptr);
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&block_cond_, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
}

Result UrmaSocket::Initialize() noexcept
{
    send_buf_size_ = URMA_SEND_BUF_DEFAULT_KB * 1024;
    recv_buf_size_ = URMA_RECV_BUF_DEFAULT_KB * 1024;

    auto buffer = UrmaBackend::Instance().GetAllocator()->direct_allocate(recv_buf_size_ + send_buf_size_);
    if (UNLIKELY(buffer == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: malloc buf failed, fd=" << raw_socket_);
        send_buf_addr_ = 0;
        recv_buf_addr_ = 0;
        return UBS_MALLOC_FAILED;
    }
    recv_buf_addr_ = reinterpret_cast<uintptr_t>(buffer);
    send_buf_addr_ = recv_buf_addr_ + recv_buf_size_;

    UrmaSockBuf *base_ring_buf = new (std::nothrow) UrmaSockRingBuf(send_buf_size_);
    if (UNLIKELY(base_ring_buf == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: ringbuf alloc failed, fd=" << raw_socket_);
        UrmaBackend::Instance().GetAllocator()->direct_release(buffer);
        send_buf_addr_ = 0;
        recv_buf_addr_ = 0;
        return UBS_MALLOC_FAILED;
    }

    send_buf_ = new (std::nothrow) UrmaSockContinuousBuf(base_ring_buf, send_buf_addr_);
    if (UNLIKELY(send_buf_ == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: ringbuf alloc failed, fd=" << raw_socket_);
        delete base_ring_buf;
        UrmaBackend::Instance().GetAllocator()->direct_release(buffer);
        send_buf_addr_ = 0;
        recv_buf_addr_ = 0;
        return UBS_MALLOC_FAILED;
    }

    return UBS_OK;
}

void UrmaSocket::UnInitialize() noexcept
{
    UrmaBackend::Instance().GetAllocator()->rm_remote_segments(context_, peer_node_);
    DestroyUrmaResources();

    send_buf_ = nullptr;
    auto buffer = reinterpret_cast<void *>(recv_buf_addr_);
    if (buffer != 0) {
        UrmaBackend::Instance().GetAllocator()->direct_release(buffer);
    }
    send_buf_addr_ = 0;
    recv_buf_addr_ = 0;

    if (event_fd_ >= 0) {
        close(event_fd_);
        event_fd_ = -1;
    }

    pthread_mutex_destroy(&block_mutex_);
    pthread_cond_destroy(&block_cond_);
}

void UrmaSocket::RegisterBlockingWaiter() noexcept
{
    block_waiters_.fetch_add(1, std::memory_order_relaxed);
}

void UrmaSocket::UnregisterBlockingWaiter() noexcept
{
    uint32_t waiters = block_waiters_.load(std::memory_order_relaxed);
    while (waiters > 0) {
        if (block_waiters_.compare_exchange_weak(waiters, waiters - 1, std::memory_order_relaxed,
                                                 std::memory_order_relaxed)) {
            break;
        }
    }
}

void UrmaSocket::NotifyBlockingWritable() noexcept
{
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_WRITABLE, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
}

void UrmaSocket::NotifyBlockingReadable() noexcept
{
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_READABLE, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
}

void UrmaSocket::NotifyBlockingPeerClosed() noexcept
{
    /* wired from UrmaBackend::HandleCqe error-CQE branch (status != URMA_CR_SUCCESS). */
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_PEER_CLOSED, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
}

void UrmaSocket::OnLocalClose() noexcept
{
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_CLOSED, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
}

/* 对端状态推进: 终态 (CLOSED/RESET) 吸收后续事件, 其余按偏序前进。
 * CAS 无锁: 错误 CQE 与 CLOSE/SHUTDOWN 可能来自不同 poller 线程。 */
void UrmaSocket::AdvancePeerCloseState(PeerCloseState next) noexcept
{
    auto cur = peer_close_.load(std::memory_order_relaxed);
    while (true) {
        if (cur == PeerCloseState::CLOSED || cur == PeerCloseState::RESET) {
            /* 终态吸收: 已上报的读语义 (EOF / ECONNRESET) 不被后续事件翻转 */
            return;
        }
        if (peer_close_.compare_exchange_weak(cur, next, std::memory_order_release,
                                              std::memory_order_relaxed)) {
            return;
        }
    }
}

/* 本端半关闭置位: 位或合并 (SHUT_WR + SHUT_RD → SHUT_RDWR), 幂等 */
void UrmaSocket::MergeLocalCloseState(LocalCloseState next) noexcept
{
    auto cur = local_close_.load(std::memory_order_relaxed);
    while (true) {
        auto merged = static_cast<LocalCloseState>(static_cast<uint8_t>(cur) | static_cast<uint8_t>(next));
        if (merged == cur) {
            return; /* 已置位 */
        }
        if (local_close_.compare_exchange_weak(cur, merged, std::memory_order_release,
                                               std::memory_order_relaxed)) {
            return;
        }
    }
}

void UrmaSocket::OnPeerClose() noexcept
{
    /* 对端有序关闭 (CLOSE opcode) — 数据已排尽 */
    AdvancePeerCloseState(PeerCloseState::CLOSED);
    NotifyClosed();
    NotifyBlockingPeerClosed();
}

void UrmaSocket::OnPeerReset() noexcept
{
    /* 连接级错误 CQE 路径 — 异常断开 */
    AdvancePeerCloseState(PeerCloseState::RESET);
    NotifyClosed();
    NotifyBlockingPeerClosed();
}

/* CLOSE/SHUTDOWN 通知的序号发生器: 独立于数据通道 */
static std::atomic<uint32_t> g_close_seq_gen{0};

int UrmaSocket::SetLingerOpt(const struct linger &value) noexcept
{
    linger_ = value;
    return 0;
}

int UrmaSocket::GetLingerOpt(struct linger &value) noexcept
{
    value = linger_;
    return 0;
}

Result UrmaSocket::PostCtrlOnlyNotification(IOOpcode opcode, OneSideSenderType notify) noexcept
{
    constexpr uint64_t unit = URMA_SND_BUFF_ALLOC_UNIT;
    constexpr uint64_t need_size = URMA_CTRL_ONLY_BUF_SIZE;
    uint64_t buf_offset = 0;
    const uint64_t real_size = send_buf_->Allocate(need_size, need_size, buf_offset);
    if (real_size < need_size) {
        UBS_SLOG_ERR("UrmaSocket(" << raw_socket_ << "): allocate send buf for ctrl notification failed");
        return UBS_ERROR;
    }

    /* 与 WRITE_IN_BAND 一致的 sge/post 范式, 仅 opcode 不同 */
    urma_sge_t src_sge = {};
    src_sge.addr = send_buf_addr_ + buf_offset;
    src_sge.len = static_cast<uint32_t>(real_size);
    src_sge.tseg = local_ctrl_target_seg_;
    urma_sge_t dst_sge = {};
    dst_sge.addr = rmt_recv_buf_addr_ + buf_offset;
    dst_sge.len = static_cast<uint32_t>(real_size);
    dst_sge.tseg = remote_ctrl_target_seg_;

    auto seq_no = g_close_seq_gen.fetch_add(1, std::memory_order_relaxed);
    auto *head = reinterpret_cast<UrmaMessageHead *>(send_buf_addr_ + buf_offset);
    head->request_id = (static_cast<uint64_t>(raw_socket_) << 32) | (seq_no & 0xFFFFFFFFUL);
    head->sequence_no = seq_no;
    head->message_size = 0; /* 无 payload, 接收方仅消费 imm opcode */

    urma_rw_wr_t rw = {};
    rw.src.sge = &src_sge;
    rw.src.num_sge = 1;
    rw.dst.sge = &dst_sge;
    rw.dst.num_sge = 1;
    rw.target_hint = 0;
    rw.notify_data = WriteImmData{opcode, buf_offset / unit}.data;

    /* ctx 第二字段语义随 notify 类型: CLOSE→seq_no (CQE 侧仅按 fd 释放 hold),
     * SHUTDOWN→buf_offset_units (CQE 侧据此 Release ctrl 槽, 不释放会在后续
     * linger close 的 drain 检查中空等到超时) */
    const uint64_t ctx_arg = (notify == SHUTDOWN_NOTIFY) ? (buf_offset / unit) : seq_no;
    const auto user_ctx = SenderCRContext{notify, ctx_arg, raw_socket_}.value;
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) send ctrl notification (opcode %u, imm off %u)", raw_socket_,
                   static_cast<unsigned>(opcode), static_cast<unsigned>(buf_offset / unit));

    auto urma_ret = jetty_->PostWriteImm(rw, user_ctx);
    if (urma_ret != URMA_SUCCESS) {
        send_buf_->Release(buf_offset, real_size);
        UBS_SLOG_ERR("UrmaSocket(" << raw_socket_ << "): post ctrl notification write_imm failed, ret=" << urma_ret);
        return UBS_ERROR;
    }
    return UBS_OK;
}

bool UrmaSocket::PostCloseToPeer(int timeout_ms) noexcept
{
    /* 跳过条件: 未建链 / jetty 无效 / 对端已关 (幂等) / 连接已 reset (对端不可达,
     * drain 空等无意义, post 必失败) / 无发送缓冲 */
    if (!IsBindRemote() || jetty_ == nullptr || IsPeerClosed() || IsPeerReset() || send_buf_ == nullptr) {
        return false;
    }

    /*
     * SO_LINGER 语义分流 (默认调用 timeout_ms<0 时按 linger_ 影子配置):
     *  - onoff=1 && linger=0 → abortive (对齐 TCP RST): 跳过 drain 与 CLOSE post,
     *    本端销毁传输资源后对端收错误 CQE → OnPeerReset → readv 返回 ECONNRESET
     *  - onoff=1 && linger>0 → drain timeout = linger*1000ms (对齐 TCP 阻塞 close)
     *  - onoff=0 (默认)      → 不等待 drain, 立即 post (对齐 TCP close 立即返回;)
     * 显式 timeout_ms>=0 仅覆盖 graceful drain 超时。
     */
    uint32_t drain_ms = 0;
    if (timeout_ms >= 0) {
        drain_ms = static_cast<uint32_t>(timeout_ms);
    } else if (IsAbortiveLinger(linger_)) {
        UBS_VLOG_WARN("UrmaSocket(%d): abortive close (SO_LINGER {1,0}), skip posting CLOSE", raw_socket_);
        return false;
    } else {
        drain_ms = LingerToDrainTimeoutMs(linger_);
    }

    /*
     * 有序关闭: 限时排空在途占用 (所有已发数据被对端 POST_WRITE/WRITE_IN_BAND_ACK 释放后
     * 再 post CLOSE)。超时兜底保证 close 不会无限阻塞。
     */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(drain_ms);
    if (drain_ms > 0 && send_buf_->Used() > 0) {
        while (send_buf_->Used() > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                UBS_VLOG_WARN("UrmaSocket(%d): PostCloseToPeer drain timeout after %ums, remaining used %lu bytes, "
                              "post CLOSE anyway",
                              raw_socket_, drain_ms, send_buf_->Used());
                break;
            }
            usleep(1000); /* 1ms 轮询 */
        }
    }

    /*
     * pending-close hold: fd 已从 ArraySet 移除, 本 close 调用链返回后本地 SocketPtr 释放,
     * 若无 hold, ~UrmaSocket 会立刻销毁 jetty 并 flush 的 CLOSE WR — 对端将收到
     * 错误 CQE (ECONNRESET) 而非 graceful EOF。注册表持引用直至 CLOSE 发送 CQE 到达。
     * 先登记再 post: 发送 CQE 由 poller 线程异步消费, 避免因 post 返回早于登记,
     * ReleasePendingClose 会因查不到条目而静默放过, 随后登记的 hold 永无人释放。
     */
    UrmaBackend::Instance().HoldPendingClose(raw_socket_, this);

    /* 空 ctrl 消息 post CLOSE (imm 携带 CLOSE opcode) */
    if (PostCtrlOnlyNotification(CLOSE, CLOSE_NOTIFY) != UBS_OK) {
        UrmaBackend::Instance().ReleasePendingClose(raw_socket_); /* post 失败不会有 CQE, 回滚 hold */
        return false;
    }

    /*
     * fd placeholder: post 成功后把 fd 号换成 /dev/null (dup3)。原 TCP socket 被内核关闭
     * (FIN 发出), fd 号保持占用直至 hold 释放路径 close — close 返回到 CLOSE CQE 到达的
     * 窗口期内, 该 fd 号不可被内核复用, 陈旧 CQE 不会误路由到新建连接。占位失败或发送 CQE
     * 已抢先释放 hold (WR 已完成) 时返回 false, 由调用方负责 close(fd)。
     */
    const bool placeholder_taken = UrmaBackend::Instance().TakeFdPlaceholder(raw_socket_);

    /* linger{1,N} 阻塞语义: 等 CLOSE WR 上屏 (发送 CQE 释放 hold) 或超时, 与 drain 共享预算 */
    if (drain_ms > 0) {
        while (UrmaBackend::Instance().HasPendingClose(raw_socket_)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                UBS_VLOG_WARN("UrmaSocket(%d): PostCloseToPeer CLOSE-CQE wait timeout after %ums, "
                              "socket held until CQE or sweep",
                              raw_socket_, drain_ms);
                break;
            }
            usleep(1000);
        }
    }
    return placeholder_taken;
}

int UrmaSocket::Shutdown(int how) noexcept
{
    if (how != SHUT_WR && how != SHUT_RDWR && how != SHUT_RD) {
        errno = EINVAL;
        return -1;
    }
    if (!IsBindRemote()) { /* 未建链 shutdown → ENOTCONN */
        errno = ENOTCONN;
        return -1;
    }

    const bool doWr = (how == SHUT_WR || how == SHUT_RDWR);
    const bool doRd = (how == SHUT_RD || how == SHUT_RDWR);
    if (doWr && !IsWriteShutdown()) {
        if (jetty_ == nullptr || send_buf_ == nullptr) { /* 传输资源已销毁视作断链 */
            errno = ENOTCONN;
            return -1;
        }
        /* 先关门再 post: 与并发 writev 竞争时尽早阻断新数据  */
        MergeLocalCloseState(LocalCloseState::SHUT_WR);
        if (PostShutdownWriteToPeer() != UBS_OK) {
            /* best-effort: 半关闭本端语义已生效, 通知失败仅记录 */
            UBS_SLOG_ERR("UrmaSocket(" << raw_socket_ << "): post SHUTDOWN failed, half-close is local-only");
        }
    }
    if (doRd && !IsReadShutdown()) {
        /* SHUT_RD 仅本端生效 (后续 read 返回 EOF), 对对端无可靠通知 */
        MergeLocalCloseState(LocalCloseState::SHUT_RD);
    }
    return 0;
}

Result UrmaSocket::PostShutdownWriteToPeer() noexcept
{
    /* 对端已关/reset: 通知不可达, 跳过 post (本端半关闭态已置, 语义仍成立) */
    if (IsPeerClosed() || IsPeerReset()) {
        return UBS_OK;
    }

    return PostCtrlOnlyNotification(SHUTDOWN, SHUTDOWN_NOTIFY);
}

void UrmaSocket::OnShutdownWriteFinished(uint64_t buf_offset_units) noexcept
{
    /* SHUTDOWN WR: 释放 ctrl 占用 (socket 存活, 不随 close 整体释放) */
    if (send_buf_ != nullptr) {
        send_buf_->Release(buf_offset_units * URMA_SND_BUFF_ALLOC_UNIT, URMA_CTRL_ONLY_BUF_SIZE);
    }
}

void UrmaSocket::OnPeerShutdownWrite() noexcept
{
    /* 对端半关闭 (SHUTDOWN opcode, FIN-like) — 数据排尽后 readv 返回 0/EOF。
     * 区别于 CLOSE: 对端仍可接收, 本端 close 仍需 post CLOSE (只有 CLOSED 才抑制) */
    AdvancePeerCloseState(PeerCloseState::SHUT_WR);
    PROF_START(___URMA_EVENT_CLOSED);
    FetchOrReachEpollEvents(EPOLLRDHUP);
    (void)eventfd_write(event_fd_, 1);
    PROF_END(___URMA_EVENT_CLOSED, true);
    NotifyBlockingReadable();
}

bool UrmaSocket::GetBlockTimeoutDeadline(int optname, struct timespec &deadline) noexcept
{
    struct timeval timeout = {};
    socklen_t len = sizeof(timeout);
    if (LibcApi::getsockopt(raw_socket_, SOL_SOCKET, optname, &timeout, &len) != 0) {
        return false;
    }
    if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
        return false;
    }

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout.tv_sec;
    deadline.tv_nsec += static_cast<long>(timeout.tv_usec) * 1000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    return true;
}

constexpr long kDefaultBlockRecheckSec = 120;

uint32_t UrmaSocket::WaitBlockingSignal(uint32_t interest_flags, const struct timespec *deadline) noexcept
{
    const uint32_t relevant = interest_flags | BLOCKING_FLAG_CLOSED | BLOCKING_FLAG_PEER_CLOSED;

    pthread_mutex_lock(&block_mutex_);
    while ((block_flags_.load(std::memory_order_relaxed) & relevant) == 0) {
        struct timespec default_deadline = {};
        const struct timespec *wait_until = deadline;
        if (wait_until == nullptr) {
            /* default 2min recheck window when SO_*TIMEO unset */
            clock_gettime(CLOCK_MONOTONIC, &default_deadline);
            default_deadline.tv_sec += kDefaultBlockRecheckSec;
            wait_until = &default_deadline;
        }

        int ret = pthread_cond_timedwait(&block_cond_, &block_mutex_, wait_until);
        if (ret == ETIMEDOUT) {
            if (deadline == nullptr) {
                /* window expired: recheck predicate, keep waiting if still empty */
                if ((block_flags_.load(std::memory_order_relaxed) & relevant) == 0) {
                    continue;
                }
                break;
            }
            pthread_mutex_unlock(&block_mutex_);
            return 0;
        }
        if (ret != 0) { /* non-Linux platforms may report EINTR */
            pthread_mutex_unlock(&block_mutex_);
            errno = ret;
            return 0; /* treat as timeout; cannot happen on Linux */
        }
        /* ret == 0: spurious wakeup, re-check predicates in the while condition */
    }

    const uint32_t signalledFlags = block_flags_.load(std::memory_order_relaxed) & relevant;
    /* consume only caller-interest doorbells; CLOSED/PEER sticky */
    block_flags_.fetch_and(~interest_flags, std::memory_order_relaxed);
    pthread_mutex_unlock(&block_mutex_);
    return signalledFlags;
}

Result UrmaSocket::CreateUrmaResources()
{
    if (context_ == nullptr) {
        UBS_SLOG_ERR("UrmaSocket: GetUrmaContext failed, fd=" << raw_socket_);
        return UBS_ERROR;
    }

    auto ret = UrmaBackend::Instance().GetPollerSet()->SelectJfc(context_, jfc_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: SelectPoller failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jfs_cfg_t jfs_cfg = context_->CreateJfsCfg(kDefaultQueueDepth, GetUrmaTransMode(), 0);
    ret = context_->CreateJfs(jfs_cfg, jfc_, jfs_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJfs failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jfr_cfg_t jfr_cfg = context_->CreateJfrCfg(kDefaultQueueDepth, GetUrmaTransMode(), 0, 0);
    ret = context_->CreateJfr(jfr_cfg, jfc_, jfr_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJfr failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jetty_cfg_t jetty_cfg = context_->CreateJettyCfg(0);
    ret = context_->CreateJetty(jetty_cfg, GetUrmaTpType(), jfs_, jfr_, jetty_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJetty failed, fd=" << raw_socket_);
        return ret;
    }

    UBS_SLOG_INFO("UrmaSocket: resources created");
    return UBS_OK;
}

void UrmaSocket::DestroyUrmaResources()
{
    if (jetty_ != nullptr) {
        jetty_->Destroy();
        jetty_ = nullptr;
    }
    if (jfr_ != nullptr) {
        jfr_->Destroy();
        jfr_ = nullptr;
    }
    if (jfs_ != nullptr) {
        jfs_->Destroy();
        jfs_ = nullptr;
    }

    jfc_ = nullptr;
    remote_target_segs_ = nullptr;
    context_ = nullptr;
}

bool UrmaSocket::IsBindRemote()
{
    return (socket_flags & URMA_SOCKET_MASK_BIND_REMOTE) != 0;
}

int UrmaSocket::NotifySocketReadable()
{
    NotifyBlockingReadable(); /* wake blocking readv waiters (data visible before this call) */
    PROF_START(___URMA_EVENT_WRITE);
    auto current_events = events_.load(std::memory_order_acquire);
    if ((current_events & EPOLLIN) == 0) { // 未关注 IN 事件，仅记录可读不通知eventfd
        socket_readable_ready_.store(true, std::memory_order_release);
        if ((events_.load(std::memory_order_acquire) & EPOLLIN) != 0) { // double check
            if (socket_readable_ready_.exchange(false, std::memory_order_acq_rel)) {
                FetchOrReachEpollEvents(EPOLLIN);
                auto ret = DeliverReadable();
                PROF_END(___URMA_EVENT_WRITE, true);
                return ret;
            }
        }
        PROF_END(___URMA_EVENT_WRITE, true);
        return 0;
    }

    FetchOrReachEpollEvents(EPOLLIN);
    auto ret = DeliverReadable();
    PROF_END(___URMA_EVENT_WRITE, true);
    return ret;
}

int UrmaSocket::DeliverReadable()
{
    /*
     * The poll this socket is registered on may deliver the readable event inline (dispatcher
     * mode, its own epoll_wait thread); otherwise keep the eventfd notification semantics.
     */
    auto notifier = readable_notifier_.load(std::memory_order_acquire);
    if (notifier != nullptr && notifier->NotifyReadable(*this)) {
        return 0;
    }
    return eventfd_write(event_fd_, 1);
}

void UrmaSocket::HandleRecvCqe(const WriteImmData &imm)
{
    switch (imm.ubsocket.opcode) {
        case PRE_WRITE:
            HandlePreWrite(imm);
            break;
        case POST_WRITE:
            HandlePostWrite(imm);
            break;
        case WRITE_IN_BAND:
            HandleWriteInBand(imm);
            break;
        case WRITE_IN_BAND_ACK:
            HandleWriteInBandAck(imm);
            break;
        case CLOSE:
            OnPeerClose();
            break;
        case SHUTDOWN:
            OnPeerShutdownWrite();
            break;
        default:
            UBS_SLOG_ERR("UrmaSocket: poll unknown imm ubsocket opcode: " << imm.ubsocket.opcode);
            break;
    }
}

void UrmaSocket::HandlePreWrite(const WriteImmData &imm)
{
    PROF_START(_URMA_IMM_REACH_PROC);
    auto rdma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto recv_ctl_off = static_cast<uint32_t>(imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT);
    if (UNLIKELY(recv_ctl_off >= recv_buf_size_)) {
        UBS_VLOG_ERR("socket fd(%d) receive invalid imm(opcode: %u, off:%u) off_by_bytes: %u", raw_socket_,
                     static_cast<unsigned>(imm.ubsocket.opcode), static_cast<unsigned>(imm.ubsocket.buffer_offset),
                     recv_ctl_off);
        return;
    }
    auto req = reinterpret_cast<UrmaCtrlMessage *>(recv_buf_addr_ + recv_ctl_off);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) recv req write_imm(%u:%u) req_id(%lu) seq(%lu)", raw_socket_,
                   static_cast<unsigned>(imm.ubsocket.opcode), static_cast<unsigned>(imm.ubsocket.buffer_offset),
                   req->head.request_id, req->head.sequence_no);
    auto res = rdma_rx_ops->DataReached(this, req->head.sequence_no, imm.data);
    /*
     * Only genuine failures are logged. UBS_LATER_SEQ (stored out of order) and UBS_DEFERRED
     * (enqueued, handled by the ReadV drain in Dispatcher mode) are non-fatal although their
     * codes are negative.
     */
    if (UNLIKELY(res != UBS_OK && res != UBS_LATER_SEQ && res != UBS_DEFERRED)) {
        UBS_VLOG_ERR("socket fd(%d) process large io ctrl message failed: %d", raw_socket_, res);
    }
    PROF_END(_URMA_IMM_REACH_PROC, true);
}

void UrmaSocket::HandlePostWrite(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(send_buf_addr_ + send_offset);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) recv resp write_imm(%u:%u) req_id(%lu) seq(%lu)", raw_socket_,
                   static_cast<unsigned>(imm.ubsocket.opcode), static_cast<unsigned>(imm.ubsocket.buffer_offset),
                   req->head.request_id, req->head.sequence_no);
    if (ubsocket_prof_enabled != 0) {
        auto used = ubsocket_get_timeNs() - req->head.trace_time;
        PROF_RECORD(URMA_FULL_IO_LARGE, used, true);
    }
    auto block_refs_ptr = reinterpret_cast<std::vector<BlockRef> *>(req->head.user_ctx);
    if (send_buf_ != nullptr && req->head.message_size > 0) {
        send_buf_->Release(send_offset, req->head.message_size);
        NotifyBlockingWritable(); /* wake blocking writev waiters */
    }

    if (block_refs_ptr != nullptr) {
        req->head.user_ctx = 0;
        for (auto &block_ref : *block_refs_ptr) {
            block_ref.block->DecRef();
        }

        delete block_refs_ptr;
    }
}

void UrmaSocket::HandleWriteInBand(const WriteImmData &imm)
{
    PROF_START(_URMA_IMM_REACH_PROC);
    auto urma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto recv_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(recv_buf_addr_ + recv_offset);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) recv req write_imm(%u:%u) req_id(%lu) seq(%lu)", raw_socket_,
                   static_cast<unsigned>(imm.ubsocket.opcode), static_cast<unsigned>(imm.ubsocket.buffer_offset),
                   req->head.request_id, req->head.sequence_no);
    auto res = urma_rx_ops->DataReached(this, req->head.sequence_no, imm.data);
    if (LIKELY(res == UBS_OK)) {
        NotifySocketReadable();
    } else if (UNLIKELY(res != UBS_LATER_SEQ && res != UBS_DEFERRED)) {
        UBS_VLOG_ERR("socket fd(%d) process inline io ctrl message failed: %d", raw_socket_, res);
    }
    PROF_END(_URMA_IMM_REACH_PROC, true);
}

void UrmaSocket::DataReadFinished(uint32_t seq_no)
{
    PROF_START(_URMA_PULL_FINISH_PROC);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) recv data read finished cr seq(%u)", raw_socket_, seq_no);
    auto rdma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto res = rdma_rx_ops->DataReadFinished(this, seq_no);
    if (LIKELY(res == UBS_OK)) {
        NotifySocketReadable();
    } else if (UNLIKELY(res != UBS_LATER_SEQ && res != UBS_DEFERRED)) {
        UBS_VLOG_ERR("socket fd(%d) process data read finished failed: %d", raw_socket_, res);
    }
    PROF_END(_URMA_PULL_FINISH_PROC, true);
}

void UrmaSocket::HandleWriteInBandAck(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(send_buf_addr_ + send_offset);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) recv resp write_imm(%u:%u) req_id(%lu) seq(%lu)", raw_socket_,
                   static_cast<unsigned>(imm.ubsocket.opcode), static_cast<unsigned>(imm.ubsocket.buffer_offset),
                   req->head.request_id, req->head.sequence_no);
    if (ubsocket_prof_enabled != 0) {
        auto used = ubsocket_get_timeNs() - req->head.trace_time;
        PROF_RECORD(URMA_FULL_IO_INLINE, used, true);
    }
    if (send_buf_ != nullptr && req->head.message_size > 0) {
        send_buf_->Release(send_offset, req->head.message_size);
        NotifyWritable();
        NotifyBlockingWritable(); /* wake blocking writev waiters */
    }
}

Result UrmaSocket::ExchangeSocketResources(bool send_first)
{
    Result ret = DoExchangeSocketResources(send_first);
    if (ret == UBS_OK) {
        return ret;
    }

    if (DegradeEnable()) {
        ret |= UBS_DEGRADABLE_MASK;
    }
    return ret;
}

Result UrmaSocket::DoExchangeSocketResources(bool send_first)
{
    Result ack_ret = UBS_OK;
    Result peer_ret = UBS_OK;
    RemoteSegmentsPtr remote_segments;
    urma_target_seg_t *local_seg;
    auto allocator = UrmaBackend::Instance().GetAllocator();
    struct UrmaSockSegExInfo {
        UrmaSockExInfo ex{};
        IdentifyNode node;
        uint32_t uasid;
        uintptr_t pool_start_addr;
        urma_seg_t seg{};
    } local, peer;

    ack_ret = CreateUrmaResources();
    if (ack_ret != UBS_OK) {
        goto CLEAN_URMA_RESOURCE;
    }

    local.ex.my_send_buf.buf = send_buf_addr_;
    local.ex.my_send_buf.size = send_buf_size_;
    local.ex.my_recv_buf.buf = recv_buf_addr_;
    local.ex.my_recv_buf.size = recv_buf_size_;
    local.ex.raw_jetty_id = jetty_->RawJettyId();
    // token交换需要安全通道，当前需约束在安全网络下
    local.ex.token = jetty_->Token();
    local.node = context_->GetLocalIdentifyNode();
    local.uasid = context_->Handle()->uasid;
    local.pool_start_addr = allocator->get_local_start_addr();
    local_seg = allocator->get_local_target_seg(context_, recv_buf_addr_);
    if (local.pool_start_addr == 0 || local_seg == nullptr) {
        UBS_SLOG_ERR("UrmaSocket: Failed local pool address: " << std::hex << local.pool_start_addr
                     << " get local target_seg @0x" << std::hex << recv_buf_addr_);
        ack_ret = UBS_TCP_EXCHANGE;
        goto CLEAN_URMA_RESOURCE;
    }
    UBS_VLOG_TRACE(
        "send buf(0x%lx, %u) recv buf(0x%lx, %u) jetty-id(eid=(%lx:%lx), uasid=%u, id=%u) "
        "seg(start_ptr=0x%lx, seg(va=0x%lx, len=%lu, attr=%x, token_id=%u), user_ctx=0x%lx, mva=0x%lx, token_id=%p",
        send_buf_addr_, send_buf_size_, recv_buf_addr_, recv_buf_size_,
        local.ex.raw_jetty_id.eid.in6.subnet_prefix, local.ex.raw_jetty_id.eid.in6.interface_id,
        local.ex.raw_jetty_id.uasid, local.ex.raw_jetty_id.id, local.pool_start_addr, local_seg->seg.ubva.va,
        local_seg->seg.len, local_seg->seg.attr, local_seg->seg.token_id, local_seg->user_ctx, local_seg->mva,
        local_seg->token_id);
    local.seg = local_seg->seg;
    if (send_first) {
        if (SocketConnHelper::SendSocketData(raw_socket_, &local, sizeof(local), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local)) {
            UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", raw_socket_, sizeof(local));
            ack_ret = UBS_TCP_EXCHANGE;
            goto CLEAN_URMA_RESOURCE;
        }
    }

    if (SocketConnHelper::RecvSocketData(raw_socket_, &peer, sizeof(peer), CONTROL_PLANE_TIMEOUT_MS) != sizeof(peer)) {
        UBS_VLOG_ERR("UrmaSocket recv socket(%d) data size(%zu) failed.", raw_socket_, sizeof(peer));
        ack_ret = UBS_TCP_EXCHANGE;
        goto CLEAN_URMA_RESOURCE;
    }

    if (jetty_->ImportRemoteJetty(peer.ex.raw_jetty_id, peer.ex.token) != UBS_OK) {
        UBS_VLOG_ERR("UrmaSocket import remote jetty failed.", raw_socket_, sizeof(peer));
        ack_ret = UBS_TCP_EXCHANGE;
        goto CLEAN_URMA_RESOURCE;
    }

    remote_segments = allocator->get_remote_segments(context_, peer.node);
    remote_segments->remote_start_addr_ = peer.pool_start_addr;
    ack_ret = allocator->store_remote_target_seg(context_, remote_segments, peer.seg, remote_ctrl_target_seg_);
    if (ack_ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: Failed to store remote segments(0x"
                     << std::hex << peer.seg.ubva.va << ", 0x" << peer.seg.len << ") return: " << std::oct << ack_ret);
        goto CLEAN_REMOTE_SEG;
    }

    local_ctrl_target_seg_ = local_seg;
    remote_target_segs_ = remote_segments;

    if (!send_first) {
        if (SocketConnHelper::SendSocketData(raw_socket_, &local, sizeof(local), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local)) {
            UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", raw_socket_, sizeof(local));
            ack_ret = UBS_TCP_EXCHANGE;
            goto CLEAN_REMOTE_SEG;
        }
    }

    if (SocketConnHelper::SendSocketData(raw_socket_, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", raw_socket_, sizeof(ack_ret));
        ack_ret = UBS_TCP_EXCHANGE;
        goto CLEAN_REMOTE_SEG;
    }
    if (SocketConnHelper::RecvSocketData(raw_socket_, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(peer_ret)) {
        UBS_VLOG_ERR("UrmaSocket recv socket(%d) data size(%zu) failed.", raw_socket_, sizeof(ack_ret));
        ack_ret = UBS_TCP_EXCHANGE;
        goto CLEAN_REMOTE_SEG;
    }

    empty_recv_sge_.tseg = local_ctrl_target_seg_;
    PostEmptyRecvWorkerRecords(128U); // TODO : post recv count
    rmt_send_buf_addr_ = peer.ex.my_send_buf.buf;
    rmt_recv_buf_addr_ = peer.ex.my_recv_buf.buf;
    remote_uasid_ = peer.uasid;
    peer_node_ = peer.node;
    SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    UBS_VLOG_TRACE("urma socket exchange socket result success, peer buf=(0x%lx:0x%lx), local buf=(0x%lx:0x%lx)",
                   peer.ex.my_send_buf.buf, peer.ex.my_recv_buf.buf, local.ex.my_send_buf.buf,
                   local.ex.my_recv_buf.buf);
    return UBS_OK;

CLEAN_REMOTE_SEG:
    allocator->rm_remote_segments(context_, peer.node);

CLEAN_URMA_RESOURCE:
    DestroyUrmaResources();

    return ack_ret;
}

void UrmaSocket::PostEmptyRecvWorkerRecords(uint32_t count)
{
    urma_jfr_wr_t *bad_wr = nullptr;
    for (auto i = 0U; i < count; i++) {
        if (jetty_->PostRecvWr(&empty_recv_wr_, &bad_wr) != URMA_SUCCESS) {
            UBS_SLOG_ERR("Failed to post recv wr for WRITE_IMM failed with index: " << i);
        }
    }
}

/* ==================== 全异步 server 建链专用注入方法（无 friend 通道） ==================== */

Result UrmaSocket::BuildLocalSegExInfo(UrmaSockSegExInfo &local) noexcept
{
    if (UNLIKELY(context_ == nullptr || jetty_ == nullptr || send_buf_ == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: BuildLocalSegExInfo resource not created, fd=" << raw_socket_);
        return UBS_ERROR;
    }

    auto allocator = UrmaBackend::Instance().GetAllocator();
    local.ex.my_send_buf.buf = send_buf_addr_;
    local.ex.my_send_buf.size = send_buf_size_;
    local.ex.my_recv_buf.buf = recv_buf_addr_;
    local.ex.my_recv_buf.size = recv_buf_size_;
    local.ex.raw_jetty_id = jetty_->RawJettyId();
    /* token 交换需要安全通道，当前需约束在安全网络下 */
    local.ex.token = jetty_->Token();
    local.node = context_->GetLocalIdentifyNode();
    local.uasid = context_->Handle()->uasid;
    local.pool_start_addr = allocator->get_local_start_addr();
    urma_target_seg_t *local_seg = allocator->get_local_target_seg(context_, recv_buf_addr_);
    if (UNLIKELY(local.pool_start_addr == 0 || local_seg == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: BuildLocalSegExInfo failed, pool_start=0x" << std::hex << local.pool_start_addr
                                                                             << " fd=" << raw_socket_);
        return UBS_TCP_EXCHANGE;
    }
    local.seg = local_seg->seg;
    local_ctrl_target_seg_ = local_seg; /* 供 ImportPeerSegments 绑定收包 sge 使用 */
    return UBS_OK;
}

Result UrmaSocket::ImportPeerSegments(const UrmaSockSegExInfo &peer) noexcept
{
    if (UNLIKELY(jetty_ == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: ImportPeerSegments jetty not created, fd=" << raw_socket_);
        return UBS_ERROR;
    }

    auto allocator = UrmaBackend::Instance().GetAllocator();
    urma_jetty_id_t peer_jetty_id = peer.ex.raw_jetty_id;
    uint32_t peer_token = peer.ex.token;
    Result jetty_ret = jetty_->ImportRemoteJetty(peer_jetty_id, peer_token);
    if (jetty_ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: ImportRemoteJetty failed, fd=" << raw_socket_);
        return UBS_TCP_EXCHANGE;
    }

    RemoteSegmentsPtr remote_segments = allocator->get_remote_segments(peer.node);
    remote_segments->remote_start_addr_ = peer.pool_start_addr;
    urma_seg_t seg_copy = peer.seg;
    Result ret = allocator->store_remote_target_seg(context_, remote_segments, seg_copy, remote_ctrl_target_seg_);
    if (UNLIKELY(ret != UBS_OK)) {
        UBS_SLOG_ERR("UrmaSocket: store remote segments failed, ret=" << ret << ", fd=" << raw_socket_);
        allocator->rm_remote_segments(peer.node);
        return ret;
    }

    local_ctrl_target_seg_ = (local_ctrl_target_seg_ != nullptr) ? local_ctrl_target_seg_ :
                                                                   allocator->get_local_target_seg(context_, recv_buf_addr_);
    remote_target_segs_ = remote_segments;
    if (local_ctrl_target_seg_ != nullptr) {
        empty_recv_sge_.tseg = local_ctrl_target_seg_;
        PostEmptyRecvWorkerRecords(128U); /* TODO: post recv count */
    }
    return UBS_OK;
}

void UrmaSocket::CommitRemotePeer(const UrmaSockSegExInfo &peer) noexcept
{
    rmt_send_buf_addr_ = peer.ex.my_send_buf.buf;
    rmt_recv_buf_addr_ = peer.ex.my_recv_buf.buf;
    remote_uasid_ = peer.uasid;
    peer_eid_ = peer.node.eid;
    SetBindRemote(true);
}

} // namespace urma
} // namespace ubs
} // namespace ock
