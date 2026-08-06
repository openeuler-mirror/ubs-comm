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
#include "iobuf/ubsocket_iobuf.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

ssize_t UrmaTxOps::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept
{
    (void)flags;

    size_t total_size = 0;
    for (auto i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == nullptr) {
            continue;
        }
        total_size += iov[i].iov_len;
    }

    UBS_VLOG_TRACE("writev on urma socket(%d) iovcnt=%d, bytes=%zu, flag=%x", sock->Fd(), iovcnt, total_size, flags);
    if (UNLIKELY(iovcnt <= 0 || total_size == 0)) {
        return 0;
    }

    if (total_size <= URMA_INLINE_DATA_THRESHOLD) {
        return WriteInline(sock, iov, iovcnt, total_size, flags);
    }

    if ((flags & UBS_IO_FLAG_USE_BRPC_BLOCK) == UBS_IO_FLAG_USE_BRPC_BLOCK) {
        return WriteZeroCopy(sock, iov, iovcnt, total_size, flags);
    }

    return WriteLargeIO(sock, iov, iovcnt, total_size, flags);
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
    /* here we already hold the reference by input param, use raw pointer directly,
     * to avoid increase and def ref */
    auto urma_sock = dynamic_cast<UrmaSocket *>(sock.Get());
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

void UrmaTxOps::SetupSgeContext(const UrmaSocketPtr &sock, uint64_t buf_offset, uint64_t alloc_size,
                                SgeContext &ctx) noexcept
{
    ctx.src_sges.resize(2);
    ctx.dst_sges.resize(2);
    ctx.src_sges[0].addr = sock->send_buf_addr_ + buf_offset;
    ctx.src_sges[0].len = static_cast<uint32_t>(alloc_size);
    ctx.src_sges[0].tseg = sock->local_ctrl_target_seg_;
    ctx.dst_sges[0].addr = sock->rmt_recv_buf_addr_ + buf_offset;
    ctx.dst_sges[0].len = static_cast<uint32_t>(alloc_size);
    ctx.dst_sges[0].tseg = sock->remote_ctrl_target_seg_;

    ctx.src_sg = {.sge = ctx.src_sges.data(), .num_sge = 1};
    ctx.dst_sg = {.sge = ctx.dst_sges.data(), .num_sge = 1};

    auto capacity = sock->send_buf_->Capacity();
    if (UNLIKELY(buf_offset + alloc_size > capacity)) {
        uint32_t part0 = static_cast<uint32_t>(capacity - buf_offset);
        uint32_t part1 = static_cast<uint32_t>(alloc_size - part0);
        ctx.src_sges[1] = ctx.src_sges[0];
        ctx.dst_sges[1] = ctx.dst_sges[0];
        ctx.src_sges[0].len = ctx.dst_sges[0].len = part0;
        ctx.src_sges[1].len = ctx.dst_sges[1].len = part1;
        ctx.src_sges[1].addr = sock->send_buf_addr_;
        ctx.dst_sges[1].addr = sock->rmt_recv_buf_addr_;
        ctx.src_sg.num_sge++;
        ctx.dst_sg.num_sge++;
    }
}

ssize_t UrmaTxOps::WriteInline(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total,
                               uint32_t flags) noexcept
{
    /* here we already hold the reference by input param, use raw pointer directly,
     * to avoid increase and def ref */
    auto urma_sock = dynamic_cast<UrmaSocket *>(sock.Get());
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
    ctrl_message.head.data_count = static_cast<uint32_t>(total);
    UBS_VLOG_TRACE("ctl_msg(req_id=%lx, seq_no=%lx, msg_size=%hu, body_size=%u", ctrl_message.head.request_id,
                   ctrl_message.head.sequence_no, ctrl_message.head.message_size, ctrl_message.head.data_count);

    std::vector<iovec> data_iov(iovcnt + 1);
    data_iov[0].iov_base = reinterpret_cast<char *>(&ctrl_message);
    data_iov[0].iov_len = URMA_INLINE_MESSAGE_CTRL_COST;
    size_t data_idx = 1;
    for (auto i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == nullptr || iov[i].iov_len == 0) {
            continue;
        }
        data_iov[data_idx].iov_base = reinterpret_cast<char *>(iov[i].iov_base);
        data_iov[data_idx].iov_len = iov[i].iov_len;
        data_idx++;
    }
    data_iov.resize(data_idx);

    SgeContext ctx;
    SetupSgeContext(urma_sock, buf_offset, real_size, ctx);
    CopyFromIovToSendBuf(data_iov, ctx.src_sges);
#ifdef UBS_LOG_TRACE_ENABLED
    auto real_req = (UrmaCtrlMessage *)(ctx.src_sges[0].addr);
    UBS_VLOG_TRACE("real_req(req_id=%lx, seq_no=%lx, msg_size=%hu, body_size=%u", real_req->head.request_id,
                   real_req->head.sequence_no, real_req->head.message_size, real_req->head.data_count);
#endif

    WriteImmData imm_data{WRITE_IN_BAND, buf_offset / unit};
    urma_rw_wr_t rw = {.src = ctx.src_sg, .dst = ctx.dst_sg, .target_hint = 0, .notify_data = imm_data.data};

    auto user_ctx = SenderCRContext{CTRL_DATA_REQUEST, seq_no, fd_}.value;
    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, user_ctx);
    if (urma_ret != URMA_SUCCESS) {
        urma_sock->send_buf_->Release(buf_offset, real_size);
        UBS_SLOG_ERR("Urma post write_imm for request: " << ctrl_message.head.request_id << " return " << urma_ret);
        errno = EAGAIN;
        return -1;
    }

    return static_cast<ssize_t>(real_size - URMA_INLINE_MESSAGE_CTRL_COST);
}


void UrmaTxOps::BuildBlockRefs(const struct iovec *iov, int iovcnt, std::vector<BlockRef> &block_refs) noexcept
{
    auto allocator = UrmaBackend::Instance().GetAllocator();
    uint32_t valid_count = 0;
    for (auto i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == nullptr || iov[i].iov_len == 0) {
            continue;
        }
        auto block_head = allocator->get_mini_block_head(reinterpret_cast<uintptr_t>(iov[i].iov_base));
        auto data = block_head + sizeof(Block);
        block_refs[valid_count].block = reinterpret_cast<Block *>(block_head);
        block_refs[valid_count].offset = reinterpret_cast<uintptr_t>(iov[i].iov_base) - data;
        block_refs[valid_count].length = iov[i].iov_len;
        valid_count++;
    }
    block_refs.resize(valid_count);
}

ssize_t UrmaTxOps::WriteZeroCopy(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total,
                                  uint32_t flags) noexcept
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    if (UNLIKELY(urma_sock->send_buf_ == nullptr)) {
        errno = ENOTCONN;
        return -1;
    }

    auto block_refs_ptr = new(std::nothrow) std::vector<BlockRef>(iovcnt);
    if (UNLIKELY(block_refs_ptr == nullptr)) {
        UBS_VLOG_ERR("socket(%d) write bytes(%zu) allocate BlockRef with count(%d) failed!", sock->Fd(), total, iovcnt);
        errno = ENOMEM;
        return -1;
    }

    auto &block_refs = *block_refs_ptr;
    BuildBlockRefs(iov, iovcnt, block_refs);
    auto valid_count = static_cast<uint32_t>(block_refs.size());

    constexpr size_t unit = URMA_SND_BUFF_ALLOC_UNIT;
    auto need_size =  sizeof(UrmaCtrlMessage) + valid_count * sizeof(PageBufferInMessage);
    need_size = ((need_size + unit - 1) / unit) * unit;

    uint64_t buf_offset = 0;
    if (!urma_sock->send_buf_->Allocate(need_size, buf_offset)) {
        UBS_VLOG_ERR("socket(%d) allocate send buf with size(%lu) failed.", sock->Fd(), need_size);
        errno = EAGAIN;
        return -1;
    }

    SgeContext ctx;
    SetupSgeContext(urma_sock, buf_offset, need_size, ctx);

    auto seq_no = __atomic_fetch_add(&seq_gen_, 1U, __ATOMIC_RELAXED);
    const auto request_id = (static_cast<uint64_t>(fd_) << 32) | (seq_no & 0xFFFFFFFFUL);

    const bool is_wrap = (ctx.src_sg.num_sge > 1);
    char *dst = reinterpret_cast<char *>(ctx.src_sges[0].addr);
    auto *head = reinterpret_cast<UrmaMessageHead *>(dst);
    head->request_id = request_id;
    head->sequence_no = seq_no;
    head->flags = 0;
    head->data_offset = 0;
    head->message_size = static_cast<uint16_t>(need_size);
    head->data_count = valid_count;
    head->user_ctx1 = reinterpret_cast<uintptr_t>(block_refs_ptr);

    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto start_addr = allocator->get_local_start_addr();
    auto *bufs = reinterpret_cast<PageBufferInMessage *>(head + 1);
    if (!is_wrap) {
        for (uint32_t i = 0; i < valid_count; i++) {
            bufs[i].addr = reinterpret_cast<uint64_t>(block_refs[i].block->data + block_refs[i].offset);
            bufs[i].size = block_refs[i].length;
            bufs[i].flags = allocator->get_local_target_seg(bufs[i].addr)->seg.token_id;
        }
    } else {
        uint32_t part0 = ctx.src_sges[0].len;
        uint32_t seg_remain = part0 >= sizeof(UrmaMessageHead) ? part0 - sizeof(UrmaMessageHead) : 0;
        char *cur = dst + sizeof(UrmaMessageHead);
        for (uint32_t i = 0; i < valid_count; i++) {
            PageBufferInMessage entry;
            entry.addr = reinterpret_cast<uint64_t>(block_refs[i].block->data + block_refs[i].offset);
            entry.size = block_refs[i].length;
            entry.flags = allocator->get_local_target_seg(entry.addr)->seg.token_id;
            if (seg_remain >= sizeof(entry)) {
                memcpy(cur, &entry, sizeof(entry));
                cur += sizeof(entry);
                seg_remain -= sizeof(entry);
            } else if (seg_remain > 0) {
                memcpy(cur, &entry, seg_remain);
                memcpy(reinterpret_cast<char *>(ctx.src_sges[1].addr),
                       reinterpret_cast<const char *>(&entry) + seg_remain, sizeof(entry) - seg_remain);
                cur = reinterpret_cast<char *>(ctx.src_sges[1].addr) + (sizeof(entry) - seg_remain);
                seg_remain = ctx.src_sges[1].len - (sizeof(entry) - seg_remain);
            } else {
                memcpy(cur, &entry, sizeof(entry));
                cur += sizeof(entry);
            }
        }
    }

    WriteImmData imm_data{PRE_WRITE, buf_offset / unit};
    urma_rw_wr_t rw = {.src = ctx.src_sg, .dst = ctx.dst_sg, .target_hint = 0, .notify_data = imm_data.data};

    auto user_ctx = SenderCRContext{CTRL_DATA_REQUEST, seq_no, fd_}.value;
    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, user_ctx);
    if (urma_ret != URMA_SUCCESS) {
        urma_sock->send_buf_->Release(buf_offset, need_size);
        UBS_SLOG_ERR("Urma post write_imm for request: " << request_id << " return " << urma_ret);
        errno = EAGAIN;
        head->user_ctx1 = 0;
        delete block_refs_ptr;
        return -1;
    }

    for (uint32_t i = 0; i < valid_count; i++) {
        block_refs[i].block->IncRef();
    }

    UBS_VLOG_TRACE("ctl_msg(req_id=%lx, seq_no=%lx, msg_size=%hu, data_count=%u, buf_count=%u, return %zu", request_id,
                   seq_no, head->message_size, head->data_count, valid_count, total);
    return static_cast<ssize_t>(total);
}

ssize_t UrmaTxOps::WriteLargeIO(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total,
                                uint32_t flags) noexcept
{
    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto block_capacity = allocator->get_mini_block_size() - sizeof(Block);
    auto block_count = static_cast<uint16_t>((total + block_capacity - 1) / block_capacity);

    allocator::FixedLinkedBlock *mini_block_head = nullptr;
    if (UNLIKELY(!allocator->multi_allocate(block_count, mini_block_head) || mini_block_head == nullptr)) {
        UBS_VLOG_ERR("socket(%d) write bytes(%zu) allocate(%hu) block failed.", sock->Fd(), total, block_count);
        errno = ENOMEM;
        return -1L;
    }

    Block *head_block = nullptr;
    Block *tail_block = nullptr;
    auto data_left = total;
    uint32_t i = 0;
    std::vector<iovec> zc_iov(block_count);
    while (mini_block_head != nullptr) {
        auto curr_capacity = std::min(block_capacity, data_left);
        auto mini_block_next = mini_block_head->next;
        auto curr_block = new (mini_block_head) Block(reinterpret_cast<char *>(mini_block_head) + sizeof(Block), curr_capacity);
        if (tail_block == nullptr) {
            head_block = tail_block = curr_block;
        } else {
            tail_block = tail_block->SetNext(curr_block);
        }

        zc_iov[i].iov_base = curr_block->data;
        zc_iov[i].iov_len = curr_block->cap;
        i++;

        mini_block_head = mini_block_next;
        data_left -= curr_capacity;
    }

    size_t src_idx = 0;
    size_t src_offset = 0;
    for (uint32_t d = 0; d < block_count && src_idx < static_cast<size_t>(iovcnt); d++) {
        if (iov[src_idx].iov_base == nullptr) {
            src_idx++;
            d--;
            continue;
        }
        char *dst = static_cast<char *>(zc_iov[d].iov_base);
        auto dst_remain = zc_iov[d].iov_len;
        size_t dst_copied = 0;

        while (src_idx < static_cast<size_t>(iovcnt) && dst_copied < dst_remain) {
            if (iov[src_idx].iov_base == nullptr) {
                src_idx++;
                src_offset = 0;
                continue;
            }
            size_t src_remain = iov[src_idx].iov_len - src_offset;
            size_t n = std::min(src_remain, dst_remain - dst_copied);
            memcpy(dst + dst_copied, static_cast<const char *>(iov[src_idx].iov_base) + src_offset, n);
            dst_copied += n;
            src_offset += n;
            if (src_offset >= iov[src_idx].iov_len) {
                src_idx++;
                src_offset = 0;
            }
        }
    }

    auto ret = WriteZeroCopy(sock, zc_iov.data(), block_count, total, flags);
    while (head_block != nullptr) {
        auto next_block  = head_block->GetNext();
        head_block->DecRef();
        head_block = next_block;
    }
    return ret;
}

} // namespace urma
} // namespace ubs
} // namespace ock
