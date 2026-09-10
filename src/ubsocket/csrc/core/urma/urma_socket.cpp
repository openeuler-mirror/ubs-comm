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
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"
#include "under_api/urma/dl_urma_api.h"
#include "urma_data_rx_ops.h"
#include "urma_data_tx_ops.h"
#include "urma_poller.h"
#include "urma_socket.h"

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
    empty_recv_wr_.user_ctx = static_cast<uint64_t>(fd);
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
    /* TODO: wire from UrmaBackend::HandleCqe error-CQE branch (status != URMA_CR_SUCCESS). */
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_PEER_CLOSED, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
}

void UrmaSocket::NotifyLocalClose() noexcept
{
    pthread_mutex_lock(&block_mutex_);
    block_flags_.fetch_or(BLOCKING_FLAG_CLOSED, std::memory_order_relaxed);
    pthread_cond_broadcast(&block_cond_);
    pthread_mutex_unlock(&block_mutex_);
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
    context_ = UrmaBackend::Instance().GetUrmaContext();
    if (context_ == nullptr) {
        UBS_SLOG_ERR("UrmaSocket: GetUrmaContext failed, fd=" << raw_socket_);
        return UBS_ERROR;
    }

    auto ret = UrmaPollerSet::Instance().SelectPoller(jfc_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: SelectPoller failed, fd=" << raw_socket_);
        return ret;
    }

    const urma_eid_info_t &eid_info = context_->EidInfo();
    memcpy(&local_eid_, &eid_info.eid, sizeof(urma_eid_t));

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
    context_ = nullptr;
}

bool UrmaSocket::IsBindRemote()
{
    return is_bind_remote_;
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
    Result ret;
    do {
        ret = DoExchangeSocketResources(send_first);
        if (ret == UBS_OK) {
            return ret;
        }
    } while (LinkSwitchEnable());

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
    local.node = UrmaBackend::Instance().GetLocalIdentifyNode();
    local.uasid = context_->Handle()->uasid;
    local.pool_start_addr = allocator->get_local_start_addr();
    local_seg = allocator->get_local_target_seg(recv_buf_addr_);
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

    remote_segments = allocator->get_remote_segments(peer.node);
    remote_segments->remote_start_addr_ = peer.pool_start_addr;
    ack_ret = allocator->store_remote_target_seg(remote_segments, peer.seg, remote_ctrl_target_seg_);
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
    peer_eid_ = peer.node.eid;
    SetBindRemote(true);
    UBS_VLOG_TRACE("urma socket exchange socket result success, peer buf=(0x%lx:0x%lx), local buf=(0x%lx:0x%lx)",
                   peer.ex.my_send_buf.buf, peer.ex.my_recv_buf.buf, local.ex.my_send_buf.buf,
                   local.ex.my_recv_buf.buf);
    return UBS_OK;

CLEAN_REMOTE_SEG:
    allocator->rm_remote_segments(peer.node);

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

} // namespace urma
} // namespace ubs
} // namespace ock
