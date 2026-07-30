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
#include "urma_data_rx_ops.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaRxOps::DataReached(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm) noexcept
{
    size_t idx = seq_no % RING_SIZE;
    auto &slot = data_slots_[idx];

    uint64_t consumed = consume_seq_no_.load(std::memory_order_acquire);
    if (UNLIKELY(slot.valid.load(std::memory_order_acquire))) {
        uint64_t occupied_seq = slot.seq_no;
        if (UNLIKELY(occupied_seq == INVALID_SEQ || consumed <= occupied_seq)) {
            UBS_VLOG_ERR("cannot fill for fd(%d) data reached seq(%lu) slot(%zu) slot_seq(%lu)", sock->Fd(), seq_no,
                         idx, slot.seq_no);
            return UBS_PREFILL_RX;
        }
    }

    slot.write_imm = write_imm;
    slot.seq_no = seq_no;
    slot.valid.store(true, std::memory_order_release);
    auto res = seq_no == consumed ? UBS_OK : UBS_LATER_SEQ;
    UBS_VLOG_TRACE("data reached seq_no(%lu) consumed(%lu) res(%d)", seq_no, consumed, res);
    return res;
}

ssize_t UrmaRxOps::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags)
{
    if (!iov || iovcnt <= 0) {
        errno = EINVAL;
        UBS_VLOG_TRACE("readv fd=%d invalid args iov=%p iovcnt=%d, return EINVAL", sock->Fd(), iov, iovcnt);
        return -1;
    }

    (void)flags;
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    UBS_VLOG_TRACE("readv fd=%d iovcnt=%d enter", sock->Fd(), iovcnt);

    size_t total = 0;
    bool has_data = false;
    int iov_idx = 0;
    size_t iov_offset = 0;

    // 按序号顺序消费 data_slots_ 中的消息
    while (iov_idx < iovcnt) {
        auto seq = consume_seq_no_.load(std::memory_order_relaxed);
        auto &slot = data_slots_[seq % RING_SIZE];
        if (!slot.valid.load(std::memory_order_acquire)) {
            UBS_VLOG_TRACE("readv fd=%d slot(%zu) not valid, break", sock->Fd(), seq % RING_SIZE);
            break;
        }
        if (slot.seq_no != seq) {
            UBS_VLOG_TRACE("readv fd=%d slot(%zu) seq_mismatch slot_seq=%lu expect=%lu, break", sock->Fd(),
                           seq % RING_SIZE, slot.seq_no, seq);
            break;
        }
        has_data = true;

        // 从 slot 解码 recv_offset，定位环形缓冲区中的消息
        WriteImmData imm_data{slot.write_imm};
        auto recv_offset = imm_data.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
        auto req = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + recv_offset);
        auto body_start = recv_offset + URMA_INLINE_MESSAGE_CTRL_COST;
        auto available = static_cast<size_t>(req->small_io.body_size) - consume_offset_;
        UBS_VLOG_TRACE("recv_offset=%lu, req=%p, msg_size=%hu, body_size=%u, head_cost=%u", recv_offset, req,
                       req->head.message_size, req->small_io.body_size, URMA_INLINE_MESSAGE_CTRL_COST);
        UBS_VLOG_TRACE("readv fd=%d slot(%zu) seq=%lu body_size=%u avail=%zu offset_in_msg=%zu", sock->Fd(),
                       seq % RING_SIZE, seq, req->small_io.body_size, available, consume_offset_);

        size_t copied = 0;
        // 将消息体分散写入 iov 数组，一条消息跨多个 iov 条目时顺序填充
        while (iov_idx < iovcnt && copied < available) {
            auto iov_remain = static_cast<size_t>(iov[iov_idx].iov_len) - iov_offset;
            auto copy_len = std::min(iov_remain, available - copied);
            auto *iov_dst = static_cast<uint8_t *>(iov[iov_idx].iov_base) + iov_offset;
            auto read_offset = body_start + consume_offset_ + copied;
            auto *src_base = reinterpret_cast<uint8_t *>(urma_sock->recv_buf_addr_);

            if (read_offset + copy_len <= urma_sock->recv_buf_size_) {
                std::memcpy(iov_dst, src_base + read_offset, copy_len);
            } else {
                // 环形缓冲区回绕，分两段拷贝
                auto part0 = urma_sock->recv_buf_size_ - read_offset;
                UBS_VLOG_TRACE("readv fd=%d ringbuf wrap read_offset=%zu part0=%zu part1=%zu", sock->Fd(), read_offset,
                               part0, copy_len - part0);
                std::memcpy(iov_dst, src_base + read_offset, part0);
                std::memcpy(iov_dst + part0, src_base, copy_len - part0);
            }

            UBS_VLOG_TRACE("readv fd=%d copy iov[%d]+%zu -> buf_off=%zu copy_len=%zu", sock->Fd(), iov_idx, iov_offset,
                           read_offset, copy_len);
            copied += copy_len;
            total += copy_len;
            iov_offset += copy_len;
            if (iov_offset >= static_cast<size_t>(iov[iov_idx].iov_len)) {
                iov_idx++;
                iov_offset = 0;
            }
        }

        if (copied == available) {
            // 消息完整消费，释放 slot 供生产者复用
            UBS_VLOG_TRACE("readv fd=%d slot(%zu) seq=%lu fully consumed, send ACK", sock->Fd(), seq % RING_SIZE, seq);
            ResponseCtrlMessage(sock, WRITE_IN_BAND_ACK, imm_data.ubsocket.buffer_offset, flags);
            slot.valid.store(false, std::memory_order_release);
            consume_seq_no_.store(seq + 1, std::memory_order_release);
            consume_offset_ = 0;
        } else {
            // 部分消费，记录偏移，下次 ReadV 从此处继续
            consume_offset_ += copied;
            UBS_VLOG_TRACE("readv fd=%d slot(%zu) seq=%lu partial consume_offset=%zu copied=%zu, break", sock->Fd(),
                           seq % RING_SIZE, seq, consume_offset_, copied);
            break;
        }
    }

    if (!has_data) {
        UBS_VLOG_TRACE("readv fd=%d no data iov fill=%zu return EAGAIN", sock->Fd(), total);
        errno = EAGAIN;
        return -1;
    }

    UBS_VLOG_TRACE("readv fd=%d done total=%zu return", sock->Fd(), total);
    return static_cast<ssize_t>(total);
}

Result UrmaRxOps::TriggerPollIn(const SocketPtr &sock, uint32_t flags)
{
    (void)sock;
    (void)flags;
    return UBS_OK;
}

Result UrmaRxOps::ResponseCtrlMessage(const SocketPtr &sock, IOOpcode opcode, uint64_t offset, uint32_t flags) noexcept
{
    (void)flags;
    WriteImmData imm_data{opcode, offset};
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    auto recv_offset = imm_data.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto resp = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + recv_offset);

    urma_sge_t src_sge;
    urma_sge_t dst_sge;
    uint32_t len = static_cast<uint32_t>(sizeof(UrmaMessageHead));
    len = ((len + URMA_SND_BUFF_ALLOC_UNIT - 1) / URMA_SND_BUFF_ALLOC_UNIT) * URMA_SND_BUFF_ALLOC_UNIT;
    src_sge.addr = urma_sock->recv_buf_addr_ + recv_offset;
    src_sge.len = len;
    src_sge.tseg = urma_sock->local_ctrl_target_seg_;
    dst_sge.addr = urma_sock->rmt_recv_buf_addr_ + recv_offset;
    dst_sge.len = len;
    dst_sge.tseg = urma_sock->remote_ctrl_target_seg_;

    urma_sg_t src_sg = {.sge = &src_sge, .num_sge = 1};
    urma_sg_t dst_sg = {.sge = &dst_sge, .num_sge = 1};

    urma_rw_wr_t rw = {.src = src_sg, .dst = dst_sg, .target_hint = 0, .notify_data = imm_data.data};
    UBS_VLOG_TRACE("urma sock(%d) resp(opcode:%hhu, offset:%lu, src_addr=%p, dst_addr=%p, len=%u)", urma_sock->Fd(),
                   opcode, recv_offset, src_sge.addr, dst_sge.addr, len);
    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, resp->head.request_id);
    if (urma_ret != URMA_SUCCESS) {
        UBS_SLOG_ERR("Urma post write_imm for response: " << resp->head.request_id << " return " << urma_ret);
        return UBS_ERROR;
    }

    return UBS_OK;
}

} // namespace urma
} // namespace ubs
} // namespace ock
