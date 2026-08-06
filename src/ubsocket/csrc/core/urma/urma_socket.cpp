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

#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/urma/dl_urma_api.h"
#include "urma_data_rx_ops.h"
#include "urma_data_tx_ops.h"
#include "urma_poller.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

UrmaSocket::UrmaSocket(int fd) noexcept: SocketBase(fd, SocketType::SOCK_TYPE_URMA)
{
    empty_recv_sge_.addr = 0;
    empty_recv_sge_.len = 0;
    empty_recv_sge_.tseg = nullptr;
    empty_recv_sge_.user_tseg = nullptr;

    empty_recv_wr_.src.sge = &empty_recv_sge_;
    empty_recv_wr_.src.num_sge = 1;
    empty_recv_wr_.user_ctx = static_cast<uint64_t>(fd);
    empty_recv_wr_.next = nullptr;
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

    send_buf_ = new (std::nothrow) UrmaSockRingBuf(send_buf_size_);
    if (UNLIKELY(send_buf_ == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: ringbuf alloc failed, fd=" << raw_socket_);
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

    urma_jfs_cfg_t jfs_cfg = context_->CreateJfsCfg(kDefaultQueueDepth, URMA_TM_RM, 0);
    ret = context_->CreateJfs(jfs_cfg, jfc_, jfs_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJfs failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jfr_cfg_t jfr_cfg = context_->CreateJfrCfg(kDefaultQueueDepth, URMA_TM_RM, 0, 0);
    ret = context_->CreateJfr(jfr_cfg, jfc_, jfr_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJfr failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jetty_cfg_t jetty_cfg = context_->CreateJettyCfg(0);
    ret = context_->CreateJetty(jetty_cfg, URMA_CTP, jfs_, jfr_, jetty_);
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
    auto current_events = events_.load(std::memory_order_acquire);
    if ((current_events & EPOLLIN) == 0) { // 未关注 IN 事件，仅记录可读不通知eventfd
        socket_readable_ready_.store(true, std::memory_order_release);
        if ((events_.load(std::memory_order_acquire) & EPOLLIN) != 0) { // double check
            if (socket_readable_ready_.exchange(false, std::memory_order_acq_rel)) {
                FetchOrReachEpollEvents(EPOLLIN);
                return eventfd_write(event_fd_, 1);
            }
        }
        return 0;
    }

    FetchOrReachEpollEvents(EPOLLIN);
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
    auto rdma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto recv_ctl_off = static_cast<uint32_t>(imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT);
    if (UNLIKELY(recv_ctl_off >= recv_buf_size_)) {
        UBS_VLOG_ERR("socket fd(%) receive invalid imm(opcode: %lx, off:%lx) off_by_bytes: %lx", raw_socket_,
                     imm.ubsocket.opcode, imm.ubsocket.buffer_offset, recv_ctl_off);
        return;
    }
    auto req = reinterpret_cast<UrmaCtrlMessage *>(recv_buf_addr_ + recv_ctl_off);
    UBS_VLOG_TRACE("socket(%d) HandlePreWrite imm_off:%lu, recv_off:%u, req->seq_no=%lu", raw_socket_,
                   imm.ubsocket.buffer_offset, recv_ctl_off, req->head.sequence_no);
    auto res = rdma_rx_ops->DataReached(this, req->head.sequence_no, imm.data);
    if (UNLIKELY(res != UBS_OK)) {
        UBS_VLOG_ERR("socket fd(%d) process large io ctrl message failed: %d", raw_socket_, res);
    }
}

void UrmaSocket::HandlePostWrite(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(send_buf_addr_ + send_offset);
    auto block_refs_ptr = reinterpret_cast<std::vector<BlockRef> *>(req->head.user_ctx1);
    if (send_buf_ != nullptr && req->head.message_size > 0) {
        send_buf_->Release(send_offset, req->head.message_size);
    }

    if (block_refs_ptr != nullptr) {
        req->head.user_ctx1 = 0;
        for (auto &block_ref : *block_refs_ptr) {
            block_ref.block->DecRef();
        }

        delete block_refs_ptr;
    }
}

void UrmaSocket::HandleWriteInBand(const WriteImmData &imm)
{
    auto urma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto recv_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(recv_buf_addr_ + recv_offset);
    UBS_VLOG_TRACE("HandleWriteInBand imm_off:%lu, recv_off:%lu, req->seq_no=%lu", imm.ubsocket.buffer_offset,
                   recv_offset, req->head.sequence_no);
    auto res = urma_rx_ops->DataReached(this, req->head.sequence_no, imm.data);
    if (LIKELY(res == UBS_OK)) {
        NotifySocketReadable();
    }
}

void UrmaSocket::DataReadFinsihed(uint32_t seq_no)
{
    auto rdma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto res = rdma_rx_ops->DataReadFinished(this, seq_no);
    if (LIKELY(res == UBS_OK)) {
        NotifySocketReadable();
    }
}

void UrmaSocket::HandleWriteInBandAck(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(send_buf_addr_ + send_offset);
    if (send_buf_ != nullptr && req->head.message_size > 0) {
        send_buf_->Release(send_offset, req->head.message_size);
        NotifyWritable();
    }
}

Result UrmaSocket::ExchangeSocketResources(int fd, UrmaSocket &urma_sock, bool send_first)
{
    Result ret = urma_sock.CreateUrmaResources();
    if (ret != UBS_OK) {
        return ret;
    }

    struct UrmaSockSegExInfo {
        UrmaSockExInfo ex{};
        IdentifyNode node;
        uint32_t uasid;
        uintptr_t pool_start_addr;
        urma_seg_t seg{};
    } local, peer;

    local.ex.my_send_buf.buf = urma_sock.send_buf_addr_;
    local.ex.my_send_buf.size = urma_sock.send_buf_size_;
    local.ex.my_recv_buf.buf = urma_sock.recv_buf_addr_;
    local.ex.my_recv_buf.size = urma_sock.recv_buf_size_;
    local.ex.raw_jetty_id = urma_sock.Jetty()->RawJettyId();
    local.ex.token = urma_sock.Jetty()->Token();

    auto allocator = UrmaBackend::Instance().GetAllocator();
    local.node = UrmaBackend::Instance().GetLocalIdentifyNode();
    local.uasid = context_->Handle()->uasid;
    local.pool_start_addr = allocator->get_local_start_addr();
    auto local_seg = allocator->get_local_target_seg(urma_sock.recv_buf_addr_);
    if (local.pool_start_addr == 0 || local_seg == nullptr) {
        UBS_SLOG_ERR("UrmaSocket: Failed local pool address: " << std::hex << local.pool_start_addr
                                                               << " get local target_seg @0x" << std::hex
                                                               << urma_sock.recv_buf_addr_);
        return UBS_TCP_EXCHANGE;
    }
    UBS_VLOG_TRACE(
        "send buf(0x%lx, %u) recv buf(0x%lx, %u) jetty-id(eid=(%lx:%lx), uasid=%u, id=%u) "
        "seg(start_ptr=0x%lx, seg(va=0x%lx, len=%lu, attr=%x, token_id=%u), user_ctx=0x%lx, mva=0x%lx, token_id=%p",
        urma_sock.send_buf_addr_, urma_sock.send_buf_size_, urma_sock.recv_buf_addr_, urma_sock.recv_buf_addr_,
        local.ex.raw_jetty_id.eid.in6.subnet_prefix, local.ex.raw_jetty_id.eid.in6.interface_id,
        local.ex.raw_jetty_id.uasid, local.ex.raw_jetty_id.id, local.pool_start_addr, local_seg->seg.ubva.va,
        local_seg->seg.len, local_seg->seg.attr, local_seg->seg.token_id, local_seg->user_ctx, local_seg->mva,
        local_seg->token_id);
    local.seg = local_seg->seg;
    if (send_first) {
        if (SocketConnHelper::SendSocketData(fd, &local, sizeof(local), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local)) {
            UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", fd, sizeof(local));
            return UBS_TCP_EXCHANGE;
        }
    }

    if (SocketConnHelper::RecvSocketData(fd, &peer, sizeof(peer), CONTROL_PLANE_TIMEOUT_MS) != sizeof(peer)) {
        UBS_VLOG_ERR("UrmaSocket recv socket(%d) data size(%zu) failed.", fd, sizeof(peer));
        return UBS_TCP_EXCHANGE;
    }

    if (urma_sock.Jetty()->ImportRemoteJetty(peer.ex.raw_jetty_id, peer.ex.token) != UBS_OK) {
        UBS_VLOG_ERR("UrmaSocket import remote jetty failed.", fd, sizeof(peer));
        return UBS_ERROR;
    }

    auto remote = allocator->get_remote_segments(peer.node);
    remote->remote_start_addr_ = peer.pool_start_addr;
    auto res = allocator->store_remote_target_seg(remote, peer.seg, remote_ctrl_target_seg_);
    if (res != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: Failed to store remote segments(0x"
                     << std::hex << peer.seg.ubva.va << ", 0x" << peer.seg.len << ") return: " << std::oct << res);
        return res;
    }

    local_ctrl_target_seg_ = local_seg;
    remote_target_segs_ = remote;

    if (!send_first) {
        if (SocketConnHelper::SendSocketData(fd, &local, sizeof(local), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local)) {
            UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", fd, sizeof(local));
            return UBS_TCP_EXCHANGE;
        }
    }

    Result ack_ret = UBS_OK;
    Result peer_ret = UBS_OK;
    if (SocketConnHelper::SendSocketData(fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        UBS_VLOG_ERR("UrmaSocket send socket(%d) data size(%zu) failed.", fd, sizeof(ack_ret));
        return UBS_TCP_EXCHANGE;
    }
    if (SocketConnHelper::RecvSocketData(fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        UBS_VLOG_ERR("UrmaSocket recv socket(%d) data size(%zu) failed.", fd, sizeof(ack_ret));
        return UBS_TCP_EXCHANGE;
    }

    empty_recv_sge_.tseg = local_ctrl_target_seg_;
    PostEmptyRecvWorkerRecords(128U); // TODO : post recv count
    urma_sock.rmt_send_buf_addr_ = peer.ex.my_send_buf.buf;
    urma_sock.rmt_recv_buf_addr_ = peer.ex.my_recv_buf.buf;
    urma_sock.remote_uasid_ = peer.uasid;
    urma_sock.peer_eid_ = peer.node.eid;
    urma_sock.SetBindRemote(true);
    UBS_VLOG_TRACE("urma socket exchange socket result success, peer buf=(0x%lx:0x%lx), local buf=(0x%lx:0x%lx)",
                   peer.ex.my_send_buf.buf, peer.ex.my_recv_buf.buf, local.ex.my_send_buf.buf,
                   local.ex.my_recv_buf.buf);
    return UBS_OK;
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
