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

#include <sys/socket.h>
#include <algorithm>
#include <cstring>

#include "core/ubsocket_socket_helper.h"
#include "iobuf/ubsocket_iobuf.h"
#include "urma_socket.h"
#include "urma_setting.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

namespace {
/* POSIX: once bytes are committed, report them instead of the failure. */
ssize_t Committed(size_t done) noexcept
{
    return done > 0 ? static_cast<ssize_t>(done) : -1;
}
} // namespace

size_t UrmaTxOps::SumValidIov(const struct iovec *iov, int iovcnt) noexcept
{
    if (UNLIKELY(iov == nullptr || iovcnt <= 0)) {
        return 0;
    }

    size_t total = 0;
    for (int i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_base == nullptr || iov[i].iov_len == 0) {
            /* 跳过 null/空 iov: 与 DataTx 上层(如 brpc)传入的稀疏 iov 语义一致 */
            continue;
        }
        total += iov[i].iov_len;
    }
    return total;
}

void UrmaTxOps::MakeIovRange(const struct iovec *iov, int iovcnt, size_t skip, size_t len,
                             std::vector<struct iovec> &out) noexcept
{
    if (UNLIKELY(iov == nullptr || iovcnt <= 0 || len == 0)) {
        return;
    }

    const size_t rangeEnd = skip + len;
    size_t validOffset = 0;
    for (int i = 0; i < iovcnt && validOffset < rangeEnd; ++i) {
        if (iov[i].iov_base == nullptr || iov[i].iov_len == 0) {
            continue;
        }

        const size_t segEnd = validOffset + iov[i].iov_len;
        const size_t begin = std::max(validOffset, skip);
        const size_t end = std::min(segEnd, rangeEnd);
        if (begin < end) {
            struct iovec seg;
            seg.iov_base = static_cast<char *>(iov[i].iov_base) + (begin - validOffset);
            seg.iov_len = end - begin;
            out.push_back(seg);
        }
        validOffset = segEnd;
    }
}

ssize_t UrmaTxOps::SubmitOnce(const SocketPtr &sock, WriteRequest &req) noexcept
{
    const struct iovec *view = req.iov;
    int viewCnt = req.iovcnt;
    if (req.done > 0) { /* the caller's list is only used for the first attempt */
        req.remainder.clear();
        MakeIovRange(req.iov, req.iovcnt, req.done, req.total - req.done, req.remainder);
        view = req.remainder.data();
        viewCnt = static_cast<int>(req.remainder.size());
    }
    return WriteVOnce(sock, view, viewCnt, req.flags);
}

ssize_t UrmaTxOps::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept
{
    auto urma_sock = dynamic_cast<UrmaSocket *>(sock.Get());
    if (UNLIKELY(urma_sock == nullptr)) {
        errno = ENOTCONN;
        return -1;
    }

    WriteRequest req(iov, iovcnt, SumValidIov(iov, iovcnt), flags);
    if (UNLIKELY(req.total == 0)) {
        return 0; /* POSIX: writev() without data returns 0 */
    }

    /* A blocking fd finishes a short write here; a non-blocking fd (or MSG_DONTWAIT) hands the
     * partial count back to the caller (POSIX). */
    const bool shortWriteAllowed = (flags & UBS_IO_FLAG_DONT_WAIT) != 0 || !SocketConnHelper::IsBlocking(fd_);

    BlockingWaiter waiter(urma_sock, BLOCKING_FLAG_WRITABLE, SO_SNDTIMEO);

    for (;;) {
        const ssize_t ret = SubmitOnce(sock, req);
        if (shortWriteAllowed) {
            return ret;
        }
        if (ret > 0) {
            req.done += static_cast<size_t>(ret);
            if (req.done < req.total) {
                continue; /* blocking fd: submit the remainder */
            }
            return Committed(req.done);
        }
        if (errno != EAGAIN || ret == 0) { /* empty view: unreachable while total > 0, fail instead of spinning */
            return Committed(req.done);
        }

        switch (waiter.Wait()) {
            case WaitOutcome::RETRY:
                continue;
            case WaitOutcome::LOCAL_CLOSED:
            case WaitOutcome::PEER_CLOSED:
                errno = EPIPE;
                return Committed(req.done);
            case WaitOutcome::TIMEOUT:
                break;
        }

        /* SO_SNDTIMEO expiry: submit once more to close the race, then report what was sent */
        const ssize_t last = SubmitOnce(sock, req);
        if (last > 0) {
            req.done += static_cast<size_t>(last);
        } else if (req.done == 0 && errno == EAGAIN) {
            UBS_VLOG_INFO("UrmaTxOps::WriteV fd=%d: SO_SNDTIMEO expired, final retry still EAGAIN", sock->Fd());
        }
        return Committed(req.done);
    }
}

ssize_t UrmaTxOps::WriteVOnce(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept
{
    const size_t total_size = SumValidIov(iov, iovcnt);

    UBS_VLOG_TRACE("writev on urma socket(%d) iovcnt=%d, bytes=%zu, flag=%x", sock->Fd(), iovcnt, total_size, flags);
    if (UNLIKELY(total_size == 0)) {
        return 0;
    }

    /* 对端已终结 (收到 CLOSE / 连接级错误) — 对端传输资源随后销毁, 写不可达, 立即 EPIPE。
     * 不单独依赖 IsClosed()(HUP 位) 间接判断, 避免 HUP 注入源变化时此处静默失效; IsClosed() 作为兜底保留。 */
    auto *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
    if (UNLIKELY(urma_sock->IsPeerClosed() || urma_sock->IsPeerReset() || urma_sock->IsClosed())) {
        UBS_VLOG_ERR("writev on closed urma socket(%d), return EPIPE", sock->Fd());
        errno = EPIPE;
        return -1;
    }

    /* 本端 SHUT_WR — 后续 writev 返回 -1/EPIPE */
    if (UNLIKELY(urma_sock->IsWriteShutdown())) {
        UBS_VLOG_ERR("writev on write-shutdown urma socket(%d), return EPIPE", sock->Fd());
        errno = EPIPE;
        return -1;
    }

    ssize_t ret;
    if (total_size <= UrmaInlineDataThreshold(UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT)) {
        ret = WriteInline(sock, iov, iovcnt, total_size, flags);
    } else if ((flags & UBS_IO_FLAG_USE_BRPC_BLOCK) == UBS_IO_FLAG_USE_BRPC_BLOCK) {
        ret = WriteZeroCopy(sock, iov, iovcnt, total_size, flags);
    } else {
        ret = WriteLargeIO(sock, iov, iovcnt, total_size, flags);
    }

    if (ret > 0 && GlobalSetting::UBS_TRACE_ENABLED) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
        sockptr->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::TX_PACKET_COUNT, 1);
        sockptr->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::TX_BYTE_COUNT, ret);
    }
    return ret;
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
    return urma_sock->send_buf_->Available() >= UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT;
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
    ctx.src_sges.resize(1);
    ctx.dst_sges.resize(1);
    ctx.src_sges[0].addr = sock->send_buf_addr_ + buf_offset;
    ctx.src_sges[0].len = static_cast<uint32_t>(alloc_size);
    ctx.src_sges[0].tseg = sock->local_ctrl_target_seg_;
    ctx.dst_sges[0].addr = sock->rmt_recv_buf_addr_ + buf_offset;
    ctx.dst_sges[0].len = static_cast<uint32_t>(alloc_size);
    ctx.dst_sges[0].tseg = sock->remote_ctrl_target_seg_;

    ctx.src_sg = {.sge = ctx.src_sges.data(), .num_sge = 1};
    ctx.dst_sg = {.sge = ctx.dst_sges.data(), .num_sge = 1};
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

    const uint32_t unit = UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT;
    auto min_bytes = ((URMA_INLINE_MESSAGE_CTRL_COST + 1UL + unit - 1) / unit) * unit;
    auto max_bytes = ((URMA_INLINE_MESSAGE_CTRL_COST + total + unit - 1) / unit) * unit;
    uint64_t buf_offset = 0;
    auto real_size = urma_sock->send_buf_->Allocate(min_bytes, max_bytes, buf_offset);
    if (real_size < min_bytes) {
        ClearWritableIfBlocked(sock);
        errno = EAGAIN;
        return -1;
    }

    /* The best-effort block may be too small for header + payload: send the prefix that fits and
     * keep head.data_count equal to it, otherwise the peer reads bytes that were never written. */
    const size_t payload_capacity = static_cast<size_t>(real_size) - URMA_INLINE_MESSAGE_CTRL_COST;
    const size_t payload_len = (total < payload_capacity) ? total : payload_capacity;
    if (UNLIKELY(payload_len == 0)) {
        urma_sock->send_buf_->Release(buf_offset, real_size);
        ClearWritableIfBlocked(sock);
        errno = EAGAIN;
        return -1;
    }

    UrmaCtrlMessage ctrl_message;
    auto seq_no = __atomic_fetch_add(&seq_gen_, 1U, __ATOMIC_RELAXED);
    ctrl_message.head.request_id = (static_cast<uint64_t>(fd_) << 32) | (seq_no & 0xFFFFFFFFUL);
    ctrl_message.head.sequence_no = seq_no;
    ctrl_message.head.flags = 0;
    ctrl_message.head.message_size = real_size;
    ctrl_message.head.data_count = static_cast<uint32_t>(payload_len);
    UBS_VLOG_TRACE("ctl_msg(req_id=%lx, seq_no=%lx, msg_size=%hu, body_size=%u", ctrl_message.head.request_id,
                   ctrl_message.head.sequence_no, ctrl_message.head.message_size, ctrl_message.head.data_count);

    std::vector<iovec> data_iov;
    data_iov.reserve(static_cast<size_t>(iovcnt) + 1U);
    struct iovec ctrl_iov;
    ctrl_iov.iov_base = reinterpret_cast<char *>(&ctrl_message);
    ctrl_iov.iov_len = URMA_INLINE_MESSAGE_CTRL_COST;
    data_iov.push_back(ctrl_iov);
    MakeIovRange(iov, iovcnt, 0, payload_len, data_iov);

    SgeContext ctx;
    SetupSgeContext(urma_sock, buf_offset, real_size, ctx);
    PROF_START(URMA_INLINE_MEM_CPY);
    CopyFromIovToSendBuf(data_iov, ctx.src_sges);
    PROF_END(URMA_INLINE_MEM_CPY, true);

    WriteImmData imm_data{WRITE_IN_BAND, buf_offset / unit};
    urma_rw_wr_t rw = {.src = ctx.src_sg, .dst = ctx.dst_sg, .target_hint = 0, .notify_data = imm_data.data};

    auto user_ctx = SenderCRContext{CTRL_DATA_REQUEST, seq_no, fd_}.value;
    auto req = (UrmaCtrlMessage *)(ctx.src_sges[0].addr);
    req->head.trace_time = ubsocket_get_timeNs();
    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, user_ctx);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) send req write_imm(%u:%u) req_id(%lu) seq(%lu) return(%d)",
                   sock->Fd(), static_cast<unsigned>(imm_data.ubsocket.opcode),
                   static_cast<unsigned>(imm_data.ubsocket.buffer_offset), req->head.request_id, req->head.sequence_no,
                   urma_ret);
    if (urma_ret != URMA_SUCCESS) {
        urma_sock->send_buf_->Release(buf_offset, real_size);
        UBS_SLOG_ERR("Urma post write_imm for request: " << ctrl_message.head.request_id << " return " << urma_ret);
        ClearWritableIfBlocked(sock);
        errno = EAGAIN;
        return -1;
    }

    /* POSIX: report the payload bytes committed, not the padded block. */
    return static_cast<ssize_t>(payload_len);
}

void UrmaTxOps::ClearWritableIfBlocked(const SocketPtr &sock) noexcept
{
    auto urma_sock = dynamic_cast<UrmaSocket *>(sock.Get());
    if (urma_sock == nullptr) {
        return;
    }
    /* Clear bit first, then re-check and refill: prevents lost wakeup race with NotifyWritabl */
    urma_sock->FetchAndReachEpollEvents(EPOLLOUT);
    if (Writable(sock)) {
        (void)urma_sock->NotifyWritable();
    }
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
    UrmaSocketPtr urma_sock = RefStaticCast<UrmaSocket>(sock);
    if (UNLIKELY(urma_sock->send_buf_ == nullptr)) {
        errno = ENOTCONN;
        return -1;
    }

    auto block_refs_ptr = new (std::nothrow) std::vector<BlockRef>(iovcnt);
    if (UNLIKELY(block_refs_ptr == nullptr)) {
        UBS_VLOG_ERR("socket(%d) write bytes(%zu) allocate BlockRef with count(%d) failed!", sock->Fd(), total, iovcnt);
        errno = ENOMEM;
        return -1;
    }

    auto &block_refs = *block_refs_ptr;
    BuildBlockRefs(iov, iovcnt, block_refs);
    auto valid_count = static_cast<uint32_t>(block_refs.size());

    const uint32_t unit = UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT;
    auto need_size = sizeof(UrmaCtrlMessage) + valid_count * sizeof(PageBufferInMessage);
    need_size = ((need_size + unit - 1) / unit) * unit;

    uint64_t buf_offset = 0;
    PROF_START(URMA_ALLOC_SEND_BUF);
    if (!urma_sock->send_buf_->Allocate(need_size, buf_offset)) {
        PROF_END(URMA_ALLOC_SEND_BUF, false);
        UBS_VLOG_ERR("socket(%d) allocate send buf with size(%lu) failed, cap(%lu) used(%lu) ava(%lu).", sock->Fd(),
                     need_size, urma_sock->send_buf_->Capacity(), urma_sock->send_buf_->Used(),
                     urma_sock->send_buf_->Available());
        ClearWritableIfBlocked(sock);
        errno = EAGAIN;
        return -1;
    }
    PROF_END(URMA_ALLOC_SEND_BUF, true);

    SgeContext ctx;
    SetupSgeContext(urma_sock, buf_offset, need_size, ctx);

    auto seq_no = __atomic_fetch_add(&seq_gen_, 1U, __ATOMIC_RELAXED);
    const auto request_id = (static_cast<uint64_t>(fd_) << 32) | (seq_no & 0xFFFFFFFFUL);

    char *dst = reinterpret_cast<char *>(ctx.src_sges[0].addr);
    auto *head = reinterpret_cast<UrmaMessageHead *>(dst);
    head->request_id = request_id;
    head->sequence_no = seq_no;
    head->flags = 0;
    head->data_offset = 0;
    head->message_size = static_cast<uint16_t>(need_size);
    head->data_count = valid_count;
    head->user_ctx = reinterpret_cast<uintptr_t>(block_refs_ptr);

    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto *bufs = reinterpret_cast<PageBufferInMessage *>(head + 1);
    auto socket_ctx = urma_sock->GetContext();
    for (uint32_t i = 0; i < valid_count; i++) {
        bufs[i].addr = reinterpret_cast<uint64_t>(block_refs[i].block->data + block_refs[i].offset);
        bufs[i].size = block_refs[i].length;
        bufs[i].flags = allocator->get_local_target_seg(socket_ctx, bufs[i].addr)->seg.token_id;
    }

    WriteImmData imm_data{PRE_WRITE, buf_offset / unit};
    urma_rw_wr_t rw = {.src = ctx.src_sg, .dst = ctx.dst_sg, .target_hint = 0, .notify_data = imm_data.data};

    auto user_ctx = SenderCRContext{CTRL_DATA_REQUEST, seq_no, fd_}.value;
    head->trace_time = ubsocket_get_timeNs();
    for (uint32_t i = 0; i < valid_count; i++) {
        block_refs[i].block->IncRef();
    }

    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, user_ctx);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) send req write_imm(%u:%u) req_id(%lu) seq(%lu) return(%d)",
                   sock->Fd(), static_cast<unsigned>(imm_data.ubsocket.opcode),
                   static_cast<unsigned>(imm_data.ubsocket.buffer_offset), head->request_id, head->sequence_no,
                   urma_ret);
    if (urma_ret != URMA_SUCCESS) {
        urma_sock->send_buf_->Release(buf_offset, need_size);
        UBS_SLOG_ERR("Urma post write_imm for request: " << request_id << " return " << urma_ret);
        errno = EAGAIN;
        head->user_ctx = 0;
        for (uint32_t i = 0; i < valid_count; i++) {
            block_refs[i].block->DecRef();
        }
        delete block_refs_ptr;
        ClearWritableIfBlocked(sock);
        return -1;
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
        auto curr_block = new (mini_block_head)
            Block(reinterpret_cast<char *>(mini_block_head) + sizeof(Block), curr_capacity);
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
        auto next_block = head_block->GetNext();
        head_block->DecRef();
        head_block = next_block;
    }
    return ret;
}

} // namespace urma
} // namespace ubs
} // namespace ock
