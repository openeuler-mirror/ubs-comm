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
#include "urma_data_tx_ops.h"

#include <algorithm>
#include <cstring>

#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

ssize_t UrmaTxOps::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept
{
    (void)flags;

    size_t total_size = 0;
    for (auto i = 0; i < iovcnt; i++) {
        total_size += iov[i].iov_len;
    }

    if (UNLIKELY(iovcnt <= 0 || total_size == 0)) {
        return 0;
    }

    if (total_size <= URMA_INLINE_DATA_THRESHOLD) {
        return WriteInline(sock, iov, iovcnt, total_size, flags);
    }

    errno = ENOSYS;
    return -1;
}

Result UrmaTxOps::Flush(const SocketPtr &sock, uint32_t flags) noexcept
{
    (void)sock;
    (void)flags;
    return UBS_OK;
}

Result UrmaTxOps::TriggerPollOut(const SocketPtr &sock, uint32_t flags) noexcept
{
    (void)sock;
    (void)flags;
    return UBS_OK;
}

bool UrmaTxOps::Writable(const SocketPtr &sock) noexcept
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    return urma_sock->send_buf_->Available() >= URMA_SND_BUFF_ALLOC_UNIT;
}

void UrmaTxOps::CopyFromIovToSendBuf(const std::vector<iovec> &iov, const std::vector<urma_sge_t> &dest) noexcept
{
    size_t iov_idx = 0;
    size_t iov_offset = 0;

    for (size_t d = 0; d < dest.size() && iov_idx < iov.size(); d++) {
        char *dst = reinterpret_cast<char *>(dest[d].addr);
        auto seg_capacity = dest[d].len;
        size_t seg_copied = 0;

        while (iov_idx < iov.size() && seg_copied < seg_capacity) {
            size_t remaining = iov[iov_idx].iov_len - iov_offset;
            size_t n = std::min(remaining, seg_capacity - seg_copied);
            memcpy(dst + seg_copied, static_cast<const char *>(iov[iov_idx].iov_base) + iov_offset, n);
            seg_copied += n;
            iov_offset += n;
            if (iov_offset >= iov[iov_idx].iov_len) {
                iov_idx++;
                iov_offset = 0;
            }
        }
    }
}

ssize_t UrmaTxOps::WriteInline(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total,
                               uint32_t flags) noexcept
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    if (UNLIKELY(urma_sock->send_buf_ == nullptr)) {
        errno = ENOTCONN;
        return -1;
    }
    constexpr size_t unit = URMA_SND_BUFF_ALLOC_UNIT;
    auto min_bytes = ((URMA_INLINE_MESSAGE_CTRL_COST + 1UL + unit - 1) / unit) * unit;
    auto max_bytes = ((URMA_INLINE_MESSAGE_CTRL_COST + total + unit - 1) / unit) * unit;
    uint64_t buf_offset = 0;
    auto real_size = urma_sock->send_buf_->Allocate(min_bytes, max_bytes, buf_offset);
    if (real_size < min_bytes) {
        errno = EAGAIN;
        return -1;
    }
    UrmaCtrlMessage ctrl_message;
    auto seq_no = __atomic_fetch_add(&seq_gen_, 1U, __ATOMIC_RELAXED);
    ctrl_message.head.request_id = (static_cast<uint64_t>(fd_) << 32) | (seq_no & 0xFFFFFFFFUL);
    ctrl_message.head.sequence_no = seq_no;
    ctrl_message.head.flags = 0;
    ctrl_message.head.message_size = real_size;
    ctrl_message.head.reserved[0] = 0;
    ctrl_message.head.reserved[1] = 0;
    ctrl_message.head.reserved[2] = 0;
    ctrl_message.small_io.body_size = static_cast<uint32_t>(total);
    std::vector<iovec> data_iov(iovcnt + 1);
    data_iov[0].iov_base = reinterpret_cast<char *>(&ctrl_message);
    data_iov[0].iov_len = sizeof(URMA_INLINE_MESSAGE_CTRL_COST);
    for (auto i = 0; i < iovcnt; i++) {
        data_iov[i + 1].iov_base = reinterpret_cast<char *>(iov[i].iov_base);
        data_iov[i + 1].iov_len = iov[i].iov_len;
    }

    std::vector<urma_sge_t> src_sges(2);
    std::vector<urma_sge_t> dst_sges(2);
    src_sges[0].addr = urma_sock->send_buf_addr_ + buf_offset;
    src_sges[0].len = static_cast<uint32_t>(real_size);
    src_sges[0].tseg = urma_sock->local_ctrl_target_seg_;
    dst_sges[0].addr = urma_sock->rmt_recv_buf_addr_ + buf_offset;
    dst_sges[0].len = static_cast<uint32_t>(real_size);
    dst_sges[0].tseg = urma_sock->remote_ctrl_target_seg_;

    urma_sg_t src_sg = {.sge = src_sges.data(), .num_sge = 1};
    urma_sg_t dst_sg = {.sge = dst_sges.data(), .num_sge = 1};

    auto capacity = urma_sock->send_buf_->Capacity();
    if (UNLIKELY(buf_offset + real_size > capacity)) {
        uint32_t part0 = static_cast<uint32_t>(capacity - buf_offset);
        uint32_t part1 = static_cast<uint32_t>(real_size - part0);
        src_sges[1] = src_sges[0];
        dst_sges[1] = dst_sges[0];
        src_sges[0].len = dst_sges[0].len = part0;
        src_sges[1].len = dst_sges[1].len = part1;
        src_sges[1].addr = urma_sock->send_buf_addr_;
        dst_sges[1].addr = urma_sock->rmt_recv_buf_addr_;
        src_sg.num_sge++;
        dst_sg.num_sge++;
    }
    /*
     * For WRITE_IMM, the immediate data is stored in rw.notify_data.
     * The server receives this as cr.imm_data via URMA_CR_OPC_WRITE_WITH_IMM.
     */
    WriteImmData imm_data{0};
    imm_data.ubsocket.opcode = WRITE_IN_BAND;
    imm_data.ubsocket.buffer_offset = buf_offset / unit;
    urma_rw_wr_t rw = {.src = src_sg, .dst = dst_sg, .target_hint = 0, .notify_data = imm_data.data};

    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, ctrl_message.head.request_id);
    if (urma_ret != URMA_SUCCESS) {
        urma_sock->send_buf_->Release(buf_offset, real_size);
        UBS_SLOG_ERR("Urma post write_imm for request: " << ctrl_message.head.request_id << " return " << urma_ret);
        errno = EAGAIN;
        return -1;
    }

    return static_cast<ssize_t>(real_size - URMA_INLINE_MESSAGE_CTRL_COST);
}

} // namespace urma
} // namespace ubs
} // namespace ock
