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
#include "urma_poller.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaSocket::Initialize() noexcept
{
    event_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ < 0) {
        UBS_SLOG_ERR("UrmaSocket: eventfd failed, fd=" << raw_socket_ << " errno=" << errno);
        return UBS_ERROR;
    }

    uint32_t send_buf_size = URMA_SEND_BUF_DEFAULT_KB * 1024;
    uint32_t recv_buf_size = URMA_RECV_BUF_DEFAULT_KB * 1024;

    auto buffer = UrmaBackend::GetAllocator()->direct_allocate(recv_buf_size + send_buf_size);
    if (UNLIKELY(buffer == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: malloc buf failed, fd=" << raw_socket_);
        send_buf_addr_ = 0;
        recv_buf_addr_ = 0;
        close(event_fd_);
        event_fd_ = -1;
        return UBS_MALLOC_FAILED;
    }
    recv_buf_addr_ = reinterpret_cast<uintptr_t>(buffer);
    send_buf_addr_ = recv_buf_addr_ + recv_buf_size;

    send_buf_ = new (std::nothrow) UrmaSockRingBuf(send_buf_size);
    if (UNLIKELY(send_buf_ == nullptr)) {
        UBS_SLOG_ERR("UrmaSocket: ringbuf alloc failed, fd=" << raw_socket_);
        UrmaBackend::GetAllocator()->direct_release(buffer);
        send_buf_addr_ = 0;
        recv_buf_addr_ = 0;
        close(event_fd_);
        event_fd_ = -1;
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
        UrmaBackend::GetAllocator()->direct_release(buffer);
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
    context_ = UrmaBackend::GetUrmaContext();
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

    urma_jfs_cfg_t jfs_cfg = context_->CreateJfsCfg(kDefaultQueueDepth, URMA_TM_RC, 0);
    ret = context_->CreateJfs(jfs_cfg, jfc_, jfs_);
    if (ret != UBS_OK) {
        UBS_SLOG_ERR("UrmaSocket: CreateJfs failed, fd=" << raw_socket_);
        return ret;
    }

    urma_jfr_cfg_t jfr_cfg = context_->CreateJfrCfg(kDefaultQueueDepth, URMA_TM_RC, 0, 0);
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
    if (jfc_ != nullptr) {
        jfc_->Destroy();
        jfc_ = nullptr;
    }
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
                return eventfd_write(event_fd_, 1);
            }
        }
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
    // TODO: implement pre-write CQE handling
    //auto recv_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    //auto req = reinterpret_cast<UramWriteMessage *>(recv_buf_addr_ + recv_offset);
    recv_data_ready_ = true;
    NotifySocketReadable();
}

void UrmaSocket::HandlePostWrite(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UramWriteMessage *>(send_buf_addr_ + send_offset);

    /*
     * TODO:
     *  checker req->flags, all page read finished -> release req buf, release finished pages
     */

    if (send_buf_ != nullptr && req->message_size > 0) {
        send_buf_->Release(send_offset, req->message_size);
    }
}

void UrmaSocket::HandleWriteInBand(const WriteImmData &imm)
{
    auto rdma_rx_ops = reinterpret_cast<UrmaRxOps *>(rx_.GetRxOps());
    auto recv_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UramWriteMessage *>(recv_buf_addr_ + recv_offset);
    auto res = rdma_rx_ops->DataReached(this, req->sequence_no, imm.data);
    if (LIKELY(res == UBS_OK)) {
        NotifySocketReadable();
    }
}

void UrmaSocket::HandleWriteInBandAck(const WriteImmData &imm)
{
    auto send_offset = imm.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UramWriteMessage *>(send_buf_addr_ + send_offset);
    if (send_buf_ != nullptr && req->message_size > 0) {
        send_buf_->Release(send_offset, req->message_size);
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
        uintptr_t pool_start_addr;
        urma_seg_t seg{};
    } local, peer;

    local.ex.my_send_buf.buf = urma_sock.send_buf_addr_;
    local.ex.my_send_buf.size = urma_sock.send_buf_size_;
    local.ex.my_recv_buf.buf = urma_sock.recv_buf_addr_;
    local.ex.my_recv_buf.size = urma_sock.recv_buf_size_;
    local.ex.raw_jetty_id = urma_sock.Jetty()->RawJettyId();
    local.ex.token = urma_sock.Jetty()->Token();

    auto allocator = UrmaBackend::GetAllocator();
    local.node = UrmaBackend::GetLocalIdentifyNode();
    local.pool_start_addr = allocator->get_local_start_addr();
    auto local_seg = allocator->get_local_target_seg(urma_sock.recv_buf_addr_);
    if (local.pool_start_addr == 0 || local_seg == nullptr) {
        UBS_SLOG_ERR("UrmaSocket: Failed local pool address: " << std::hex << local.pool_start_addr
                                                               << " get local target_seg @0x" << std::hex
                                                               << urma_sock.recv_buf_addr_);
        return UBS_TCP_EXCHANGE;
    }
    local.seg = local_seg->seg;
    if (send_first) {
        if (SocketConnHelper::SendSocketData(fd, &local, sizeof(local), CONTROL_PLANE_TIMEOUT_MS) != sizeof(local)) {
            return UBS_TCP_EXCHANGE;
        }
    }

    if (SocketConnHelper::RecvSocketData(fd, &peer, sizeof(peer), CONTROL_PLANE_TIMEOUT_MS) != sizeof(peer)) {
        return UBS_TCP_EXCHANGE;
    }

    if (urma_sock.Jetty()->ImportRemoteJetty(peer.ex.raw_jetty_id, peer.ex.token) != UBS_OK) {
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
            return UBS_TCP_EXCHANGE;
        }
    }

    Result ack_ret = UBS_OK;
    Result peer_ret = UBS_OK;
    if (SocketConnHelper::SendSocketData(fd, &ack_ret, sizeof(ack_ret), CONTROL_PLANE_TIMEOUT_MS) != sizeof(ack_ret)) {
        return UBS_TCP_EXCHANGE;
    }
    if (SocketConnHelper::RecvSocketData(fd, &peer_ret, sizeof(peer_ret), CONTROL_PLANE_TIMEOUT_MS) !=
        sizeof(peer_ret)) {
        return UBS_TCP_EXCHANGE;
    }

    urma_jfr_wr_t *bad_wr = nullptr;
    urma_sge_t recv_sge = {.addr = 0, .len = 0, .tseg = local_ctrl_target_seg_, .user_tseg = nullptr};
    urma_sg_t recv_sg = {.sge = &recv_sge, .num_sge = 1};
    urma_jfr_wr_t recv_wr = {.src = recv_sg, .user_ctx = static_cast<uint64_t>(fd), .next = nullptr};
    for (auto i = 0U; i < 128U; i++) { // TODO : post recv count
        if (jetty_->PostRecvWr(&recv_wr, &bad_wr) != URMA_SUCCESS) {
            UBS_SLOG_ERR("Failed to post recv wr for WRITE_IMM failed with index: " << i);
            return UBS_TCP_EXCHANGE;
        }
    }

    urma_sock.rmt_send_buf_addr_ = peer.ex.my_send_buf.buf;
    urma_sock.rmt_recv_buf_addr_ = peer.ex.my_recv_buf.buf;
    urma_sock.SetBindRemote(true);
    return UBS_OK;
}

} // namespace urma
} // namespace ubs
} // namespace ock
