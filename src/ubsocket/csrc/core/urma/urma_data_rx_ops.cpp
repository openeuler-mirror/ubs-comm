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

#include <sys/socket.h>

#include "core/ubsocket_socket_helper.h"
#include "urma_backend.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

Result UrmaRxOps::ProcessDataReached(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm) noexcept
{
    size_t idx = seq_no % RING_SIZE;
    auto &slot = data_slots_[idx];

    uint64_t consumed = consume_seq_no_.load(std::memory_order_acquire);
    if (UNLIKELY(slot.rx_state.load(std::memory_order_acquire) != URMA_RX_STS_IDLE)) {
        uint64_t occupied_seq = slot.seq_no;
        if (UNLIKELY(occupied_seq == INVALID_SEQ || consumed <= occupied_seq)) {
            UBS_VLOG_ERR("cannot fill for fd(%d) data reached seq(%lu) slot(%zu) slot_seq(%lu)", sock->Fd(), seq_no,
                         idx, slot.seq_no);
            return UBS_PREFILL_RX;
        }
    }
    slot.write_imm = write_imm;
    slot.seq_no = seq_no;

    WriteImmData imm_data{slot.write_imm};
    if (imm_data.ubsocket.opcode == PRE_WRITE) {
        return IssueReadRequest(sock, seq_no, write_imm, slot);
    }

    if (ubsocket_prof_enabled != 0) {
        slot.start_read_time = ubsocket_get_timeNs();
    }
    slot.rx_state.store(URMA_RX_STS_DATA_READY, std::memory_order_release);
    auto res = seq_no == consumed ? UBS_OK : UBS_LATER_SEQ;
    UBS_VLOG_TRACE("socket(%d) data reached seq_no(%lu) consumed(%lu) res(%d)", sock->Fd(), seq_no, consumed, res);
    return res;
}

Result UrmaRxOps::ProcessDataReadFinished(const SocketPtr &sock, uint64_t seq_no) noexcept
{
    size_t idx = seq_no % RING_SIZE;
    auto &slot = data_slots_[idx];
    if (ubsocket_prof_enabled != 0) {
        auto used = ubsocket_get_timeNs() - slot.start_read_time;
        PROF_RECORD(_URMA_LARGE_DATA_PULL, used, true);
    }

    uint64_t consumed = consume_seq_no_.load(std::memory_order_acquire);
    auto curr_state = slot.rx_state.load(std::memory_order_acquire);
    if (UNLIKELY(curr_state != URMA_RX_STS_READING)) {
        UBS_VLOG_ERR("urma socket fd(%d) data state seq(%lu) slot(%zu) should be(%hhu) but(%hhu)", sock->Fd(), seq_no,
                     idx, URMA_RX_STS_READING, curr_state);
        return UBS_ERROR;
    }

    WriteImmData imm_data{slot.write_imm};
    ResponseCtrlMessage(sock, POST_WRITE, imm_data.ubsocket.buffer_offset, 0);
    if (ubsocket_prof_enabled != 0) {
        slot.start_read_time = ubsocket_get_timeNs();
    }
    slot.rx_state.store(URMA_RX_STS_DATA_READY, std::memory_order_release);
    auto res = seq_no == consumed ||
                       (data_slots_[consume_seq_no_.load(std::memory_order_acquire) % RING_SIZE].rx_state ==
                        URMA_RX_STS_DATA_READY) ?
                   UBS_OK :
                   UBS_LATER_SEQ;
    UBS_VLOG_TRACE("socket(%d) data reached seq_no(%lu) consumed(%lu) res(%d)", sock->Fd(), seq_no, consumed, res);
    return res;
}

ssize_t UrmaRxOps::ZeroCopyRead(const SocketPtr &sock, DataSlot &slot, uint64_t seq, IovWriter &w) noexcept
{
    size_t iov_total = 0;
    if (GlobalSetting::UBS_READV_UNLIMITED) {
        iov_total = UINT32_MAX;
    } else {
        for (auto i = 0; i < w.iovcnt; i++) {
            iov_total += w.iov[i].iov_len;
        }
    }

    UBS_VLOG_TRACE("socket(%d)zcopy_read seq(%lu) read total(%zu)", sock->Fd(), seq, iov_total);
    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto block = allocator->get_mini_block_head(reinterpret_cast<uintptr_t>(w.iov[0].iov_base));
    if (block == 0) {
        UBS_VLOG_ERR("zcopy read fd(%d) first addr: %p invalid", fd_, w.iov[0].iov_base);
        errno = EINVAL;
        return -1;
    }

    auto out_first_block = reinterpret_cast<Block *>(block);
    auto curr_block = slot.data_block;
    while (curr_block != nullptr) {
        auto next_block = curr_block->GetNext();
        block_cache_.Insert(curr_block->data, curr_block->cap);
        curr_block = next_block;
    }
    slot.data_block = nullptr;

    ssize_t rx_total_len = block_cache_.CutAndInsertAfter(iov_total, out_first_block);
    if (rx_total_len == 0) {
        UBS_VLOG_ERR("zcopy read fd(%d) first addr: %p invalid, return 0", fd_, w.iov[0].iov_base);
        errno = EAGAIN;
        return -1;
    }

    out_first_block->size = out_first_block->cap;
    if (block_cache_.GetCacheLen() == 0) {
        consume_seq_no_.store(seq + 1, std::memory_order_release);
        slot.rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);
        ReleaseBlockInSlot(slot);
    }
    UBS_VLOG_TRACE("socket(%d)zcopy_read seq(%lu) read total(%zu) real(%zd)", sock->Fd(), seq, iov_total, rx_total_len);
    return rx_total_len;
}

ssize_t UrmaRxOps::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags)
{
    auto urma_sock = dynamic_cast<UrmaSocket *>(sock.Get());
    if (UNLIKELY(urma_sock == nullptr)) {
        errno = EBADF;
        return -1;
    }

    bool registered = false;
    auto unregister_waiter = MakeScopeExit([&registered, urma_sock]() {
        if (registered) {
            urma_sock->UnregisterBlockingWaiter();
        }
    });

    bool has_deadline = false;
    struct timespec deadline = {0, 0};
    for (;;) {
        if (UNLIKELY(urma_sock->State() == SOCK_STAT_CLOSE)) {
            errno = EBADF;
            return -1;
        }

        ssize_t ret = ReadVOnce(sock, iov, iovcnt, flags);
        if (ret >= 0 || errno != EAGAIN) {
            return ret;
        }
        if (!SocketConnHelper::IsBlocking(fd_)) {
            return ret;
        }
        if (!has_deadline) {
            has_deadline = urma_sock->GetBlockTimeoutDeadline(SO_RCVTIMEO, deadline);
        }
        if (!registered) {
            urma_sock->RegisterBlockingWaiter();
            registered = true;
            continue; // retry once after registering to eat state changes that raced registration
        }

        const uint32_t signalledFlags =
            urma_sock->WaitBlockingSignal(UrmaSocket::BLOCKING_FLAG_READABLE, has_deadline ? &deadline : nullptr);
        if ((signalledFlags & UrmaSocket::BLOCKING_FLAG_CLOSED) != 0) {
            errno = EBADF;
            return -1;
        }
        if ((signalledFlags & UrmaSocket::BLOCKING_FLAG_PEER_CLOSED) != 0) {
            /* TODO: EOF(返回0) 与异常(透传 errno) 的区分依赖接入事件携带的语义 */
            errno = ECONNRESET;
            return -1;
        }
        if (signalledFlags == 0) {
            /* explicit SO_RCVTIMEO expiry: retry once to close the race, then report EAGAIN */
            ssize_t last = ReadVOnce(sock, iov, iovcnt, flags);
            if (last >= 0 || errno != EAGAIN) {
                return last;
            }
            errno = EAGAIN;
            return -1;
        }
        // readable or spurious wakeup: loop and let the real read decide
    }
}

ssize_t UrmaRxOps::ReadVOnce(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags)
{
    /* Drain deferred events first (dispatcher strategy); a no-op in inline mode. */
    rx_dispatcher_->DrainEvents(*this, sock);

    if (UNLIKELY(!iov || iovcnt <= 0)) {
        errno = EINVAL;
        UBS_VLOG_TRACE("readv fd=%d invalid args iov=%p iovcnt=%d, return EINVAL", sock->Fd(), iov, iovcnt);
        return -1;
    }
    PROF_START(CORE_READ);
    (void)flags;
    UBS_VLOG_TRACE("readv fd=%d iovcnt=%d enter", sock->Fd(), iovcnt);

    IovWriter w{};
    w.iov = iov;
    w.iovcnt = iovcnt;

    /* PRE_WRITE 大 IO: 从 block_cache_ 直接取数据 */
    auto seq = consume_seq_no_.load(std::memory_order_relaxed);
    auto slot = &data_slots_[seq % RING_SIZE];
    auto rx_state = slot->rx_state.load(std::memory_order_acquire);
    if (rx_state != URMA_RX_STS_DATA_READY || slot->seq_no != seq) {
        errno = EAGAIN;
        return -1;
    }

    if (ubsocket_prof_enabled != 0) {
        auto used = ubsocket_get_timeNs() - slot->start_read_time;
        PROF_RECORD(_URMA_DEAL_SLOT_DATA, used, true);
    }

    WriteImmData imm_data{slot->write_imm};
    if (imm_data.ubsocket.opcode == PRE_WRITE && (flags & UBS_IO_FLAG_USE_BRPC_BLOCK) == UBS_IO_FLAG_USE_BRPC_BLOCK) {
        ssize_t size = ZeroCopyRead(sock, *slot, seq, w);
        PROF_END(CORE_READ, size >= 0);
        return size;
    }

    /* WRITE_IN_BAND 小 IO: 按 slot 顺序消费 */
    bool has_data = false;
    w.idx = 0;
    w.offset = 0;
    w.written = 0;
    while (w.idx < w.iovcnt) {
        has_data = true;
        bool fully_consumed;
        if (imm_data.ubsocket.opcode == PRE_WRITE) {
            fully_consumed = ConsumeBlockData(sock, *slot, w, flags);
        } else {
            fully_consumed = ConsumeInlineData(sock, *slot, w, flags);
        }
        if (!fully_consumed) {
            break;
        }

        seq = consume_seq_no_.load(std::memory_order_relaxed);
        /* 上一消息已整体消费并推进 consume_seq_no_, 需按新 seq 重新定位槽位 */
        slot = &data_slots_[seq % RING_SIZE];
        rx_state = slot->rx_state.load(std::memory_order_acquire);
        imm_data.data = slot->write_imm;
        if (rx_state != URMA_RX_STS_DATA_READY || slot->seq_no != seq ||
            (imm_data.ubsocket.opcode == PRE_WRITE &&
             (flags & UBS_IO_FLAG_USE_BRPC_BLOCK) == UBS_IO_FLAG_USE_BRPC_BLOCK)) {
            break;
        }
    }

    if (!has_data) {
        UBS_VLOG_TRACE("readv fd=%d no data iov fill=%zu return EAGAIN", sock->Fd(), w.written);
        errno = EAGAIN;
        return -1;
    }

    PROF_END(CORE_READ, true);
    UBS_VLOG_TRACE("readv fd=%d done total=%zu return", sock->Fd(), w.written);
    return static_cast<ssize_t>(w.written);
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
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
    auto recv_offset = imm_data.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto resp = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + recv_offset);

    urma_sge_t src_sge;
    urma_sge_t dst_sge;
    uint32_t len = static_cast<uint32_t>(sizeof(UrmaMessageHead));
    len = ((len + URMA_SND_BUFF_ALLOC_UNIT - 1) / URMA_SND_BUFF_ALLOC_UNIT) * URMA_SND_BUFF_ALLOC_UNIT;
    src_sge.addr = urma_sock->recv_buf_addr_ + recv_offset;
    src_sge.len = len;
    src_sge.tseg = urma_sock->local_ctrl_target_seg_;
    dst_sge.addr = urma_sock->rmt_send_buf_addr_ + recv_offset;
    dst_sge.len = len;
    dst_sge.tseg = urma_sock->remote_ctrl_target_seg_;

    urma_sg_t src_sg = {.sge = &src_sge, .num_sge = 1};
    urma_sg_t dst_sg = {.sge = &dst_sge, .num_sge = 1};

    urma_rw_wr_t rw = {.src = src_sg, .dst = dst_sg, .target_hint = 0, .notify_data = imm_data.data};
    UBS_VLOG_TRACE("urma sock(%d) resp(opcode:%hhu, offset:%lu, src_addr=%p, dst_addr=%p, len=%u)", urma_sock->Fd(),
                   opcode, recv_offset, src_sge.addr, dst_sge.addr, len);
    auto urma_ret = urma_sock->jetty_->PostWriteImm(rw, 0);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) send resp write_imm(%u:%u) req_id(%lu) seq(%lu) return(%d)",
                   sock->Fd(), static_cast<unsigned>(imm_data.ubsocket.opcode),
                   static_cast<unsigned>(imm_data.ubsocket.buffer_offset), resp->head.request_id,
                   resp->head.sequence_no, urma_ret);
    if (urma_ret != URMA_SUCCESS) {
        UBS_SLOG_ERR("Urma post write_imm for response: " << resp->head.request_id << " return " << urma_ret);
        return UBS_ERROR;
    }

    return UBS_OK;
}

Result UrmaRxOps::IssueReadRequest(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm, DataSlot &slot) noexcept
{
    WriteImmData imm_data{write_imm};
    auto urma_sock = static_cast<UrmaSocket *>(sock.Get());
    auto offset = imm_data.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + offset);
    auto data_count = req->head.data_count;

    auto need_size = sizeof(UrmaMessageHead) + data_count * sizeof(PageBufferInMessage);
    need_size = ((need_size + URMA_SND_BUFF_ALLOC_UNIT - 1) / URMA_SND_BUFF_ALLOC_UNIT) * URMA_SND_BUFF_ALLOC_UNIT;

    uint32_t total_bytes = 0;
    for (auto i = 0U; i < data_count; i++) {
        uint64_t entry_offset = offset + sizeof(UrmaMessageHead) + i * sizeof(PageBufferInMessage);
        auto *buf = reinterpret_cast<const PageBufferInMessage *>(urma_sock->recv_buf_addr_ + entry_offset);
        total_bytes += buf->size;
    }

    UBS_VLOG_TRACE("read data to block for socket(%d) seq(%lu) imm(%hhu, %lu) total size=%u", sock->Fd(), seq_no,
                   imm_data.ubsocket.opcode, imm_data.ubsocket.buffer_offset, total_bytes);
    ImportRemoteSegments(sock, data_count, offset);
    auto *head_block = AllocBlocksForReq(total_bytes);
    if (UNLIKELY(head_block == nullptr)) {
        UBS_VLOG_ERR("socket(fd) allocate for seq(%lu) bytes(%u) allocate failed.", sock->Fd(), seq_no, total_bytes);
        return UBS_ERROR;
    }
    slot.data_block = head_block;

    const auto user_ctx = SenderCRContext{READ_DATA_REQUEST, seq_no, urma_sock->Fd()}.value;
    auto allocator = UrmaBackend::Instance().GetAllocator();
    BuildReadWrCtx build_ctx{allocator, urma_sock, user_ctx};
    BuildReadWrs(data_count, offset, head_block, build_ctx);

    if (UNLIKELY(build_ctx.wrs.empty())) {
        UBS_VLOG_ERR("send request READ for socket(%d) seq_no(%lu) wrs is empty", sock->Fd(), seq_no);
        return UBS_ERROR;
    }

    urma_jfs_wr_t *bad_wr = nullptr;
    slot.start_read_time = ubsocket_get_timeNs();
    auto ret = urma_sock->jetty_->PostSendWr(&build_ctx.wrs[0], &bad_wr);
    UBS_VLOG_TRACE("urma_socket_message: socket(%d) one-side-re write_imm(%u:%u) req_id(%lu) seq(%lu) return(%d)",
                   sock->Fd(), static_cast<unsigned>(imm_data.ubsocket.opcode),
                   static_cast<unsigned>(imm_data.ubsocket.buffer_offset), req->head.request_id, req->head.sequence_no,
                   ret);
    if (ret != URMA_SUCCESS) {
        UBS_VLOG_ERR("send request READ for socket(%d) seq_no(%lu) failed: %d", sock->Fd(), seq_no, ret);
        ReleaseBlockInSlot(slot);
        return UBS_ERROR;
    }

    slot.rx_state.store(URMA_RX_STS_READING, std::memory_order_release);
    UBS_VLOG_TRACE("socket(%d) send read data to blocks request for seq(%lu) success.", sock->Fd(), seq_no);
    return UBS_OK;
}

Block *UrmaRxOps::AllocBlocksForReq(uint32_t total_bytes) noexcept
{
    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto block_size = allocator->get_mini_block_size();
    auto block_capacity = static_cast<uint32_t>(block_size - sizeof(Block));
    auto block_count = static_cast<uint16_t>((total_bytes + block_capacity - 1) / block_capacity);

    allocator::FixedLinkedBlock *mini_block_head = nullptr;
    if (UNLIKELY(!allocator->multi_allocate(block_count, mini_block_head) || mini_block_head == nullptr)) {
        UBS_VLOG_ERR("allocate for bytes(%u) block_count(%hu) failed.", total_bytes, block_count);
        return nullptr;
    }

    Block *head_block = nullptr;
    Block *tail_block = nullptr;
    auto data_left = total_bytes;
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
        mini_block_head = mini_block_next;
        data_left -= curr_capacity;
    }
    return head_block;
}

void UrmaRxOps::BuildReadWrs(uint32_t data_count, uint64_t msg_offset, Block *head_block, BuildReadWrCtx &ctx) noexcept
{
    size_t remote_idx = 0;
    uint32_t remote_offset = 0;
    Block *local_block = head_block;
    uint32_t local_offset = 0;

    uint32_t common_complete_enable = 0;
    if (UrmaSetting::UB_USING_BOUNDING_DEV) {
        common_complete_enable = 1;
    }
    while (remote_idx < data_count && local_block != nullptr) {
        uint64_t entry_offset = msg_offset + sizeof(UrmaMessageHead) + remote_idx * sizeof(PageBufferInMessage);
        const auto *buf = reinterpret_cast<const PageBufferInMessage *>(ctx.urma_sock->recv_buf_addr_ + entry_offset);
        uint32_t remote_remain = buf->size - remote_offset;
        uint32_t local_remain = local_block->cap - local_offset;
        uint32_t seg_size = std::min(remote_remain, local_remain);

        ctx.src_sges.emplace_back();
        ctx.dst_sges.emplace_back();
        auto &src_sge = ctx.src_sges.back();
        auto &dst_sge = ctx.dst_sges.back();

        src_sge.addr = buf->addr + remote_offset;
        src_sge.len = seg_size;
        src_sge.tseg = ctx.allocator->get_remote_target_seg(ctx.urma_sock->remote_target_segs_, buf->addr);

        dst_sge.addr = reinterpret_cast<uintptr_t>(local_block->data) + local_offset;
        dst_sge.len = seg_size;
        dst_sge.tseg = ctx.allocator->get_local_target_seg(reinterpret_cast<uintptr_t>(local_block->data));

        urma_jfs_wr_t wr = {};
        wr.opcode = URMA_OPC_READ;
        wr.user_ctx = 0;
        wr.flag.bs.complete_enable = common_complete_enable;
        ctx.wrs.push_back(wr);

        remote_offset += seg_size;
        if (remote_offset >= buf->size) {
            remote_idx++;
            remote_offset = 0;
        }

        local_offset += seg_size;
        if (local_offset >= local_block->cap) {
            local_block = local_block->GetNext();
            local_offset = 0;
        }
    }

    // During the loop, src_sges and dst_sges may be realloc. As a result, the sge pointer embedded in the WR becomes invalid.
    for (size_t i = 0; i < ctx.wrs.size(); i++) {
        ctx.wrs[i].rw.src = {.sge = &ctx.src_sges[i], .num_sge = 1};
        ctx.wrs[i].rw.dst = {.sge = &ctx.dst_sges[i], .num_sge = 1};
        if (i + 1 < ctx.wrs.size()) {
            ctx.wrs[i].next = &ctx.wrs[i + 1];
        } else {
            ctx.wrs[i].user_ctx = ctx.user_ctx;
            ctx.wrs[i].flag.bs.complete_enable = 1;
        }
    }
}

void UrmaRxOps::ImportRemoteSegments(const SocketPtr &sock, uint32_t data_count, uint64_t offset) noexcept
{
    auto urma_socket = static_cast<UrmaSocket *>(sock.Get());
    auto allocator = UrmaBackend::Instance().GetAllocator();
    auto remote_segs = urma_socket->remote_target_segs_;
    for (auto i = 0U; i < data_count; i++) {
        uint64_t entry_offset = offset + sizeof(UrmaMessageHead) + i * sizeof(PageBufferInMessage);
        const auto *buf = reinterpret_cast<const PageBufferInMessage *>(urma_socket->recv_buf_addr_ + entry_offset);
        allocator->store_remote_target_seg(remote_segs, urma_socket->peer_eid_, urma_socket->remote_uasid_, buf->addr,
                                           buf->flags);
    }
}

bool UrmaRxOps::ConsumeBlockData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept
{
    WriteImmData imm_data{slot.write_imm};
    auto seq = slot.seq_no;

    Block *block = slot.data_block;
    if (UNLIKELY(block == nullptr)) {
        UBS_VLOG_ERR("readv fd=%d pre_write seq=%lu data_block is null", sock->Fd(), seq);
        return false;
    }

    size_t total_block_bytes = 0;
    for (auto *b = block; b != nullptr; b = b->GetNext()) {
        total_block_bytes += b->cap;
    }
    auto available = total_block_bytes - consume_offset_;

    uint64_t skip = consume_offset_;
    while (block != nullptr && skip >= block->cap) {
        skip -= block->cap;
        block = block->GetNext();
    }

    size_t copied = 0;
    while (w.idx < w.iovcnt && copied < available) {
        auto iov_remain = static_cast<size_t>(w.iov[w.idx].iov_len) - w.offset;
        auto copy_len = std::min(iov_remain, available - copied);
        auto copy_len_from_block = std::min(copy_len, static_cast<size_t>(block->cap - skip));

        auto *iov_dst = static_cast<uint8_t *>(w.iov[w.idx].iov_base) + w.offset;
        std::memcpy(iov_dst, block->data + skip, copy_len_from_block);

        UBS_VLOG_TRACE("readv fd=%d pre_write seq=%lu copy iov[%d]+%zu block=%p+%lu len=%zu", sock->Fd(), seq, w.idx,
                       w.offset, block, skip, copy_len_from_block);

        copied += copy_len_from_block;
        w.written += copy_len_from_block;
        w.offset += copy_len_from_block;
        skip += copy_len_from_block;

        if (w.offset >= static_cast<size_t>(w.iov[w.idx].iov_len)) {
            w.idx++;
            w.offset = 0;
        }
        if (skip >= block->cap) {
            block = block->GetNext();
            skip = 0;
        }
    }

    if (copied == available) {
        UBS_VLOG_TRACE("readv fd=%d pre_write seq=%lu fully consumed, send POST_WRITE", sock->Fd(), seq);
        consume_seq_no_.store(seq + 1, std::memory_order_release);
        slot.rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);
        consume_offset_ = 0;
        return true;
    }

    consume_offset_ += copied;
    UBS_VLOG_TRACE("readv fd=%d pre_write seq=%lu partial consume_offset=%zu copied=%zu", sock->Fd(), seq,
                   consume_offset_, copied);
    return false;
}

bool UrmaRxOps::ConsumeInlineData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept
{
    WriteImmData imm_data{slot.write_imm};
    auto seq = slot.seq_no;
    auto urma_sock = static_cast<UrmaSocket *>(sock.Get());

    auto recv_offset = imm_data.ubsocket.buffer_offset * URMA_SND_BUFF_ALLOC_UNIT;
    auto req = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + recv_offset);
    auto body_start = recv_offset + URMA_INLINE_MESSAGE_CTRL_COST;
    auto available = static_cast<size_t>(req->head.data_count) - consume_offset_;

    size_t copied = 0;
    while (w.idx < w.iovcnt && copied < available) {
        auto iov_remain = static_cast<size_t>(w.iov[w.idx].iov_len) - w.offset;
        auto copy_len = std::min(iov_remain, available - copied);
        auto *iov_dst = static_cast<uint8_t *>(w.iov[w.idx].iov_base) + w.offset;
        auto read_offset = body_start + consume_offset_ + copied;
        auto *src_base = reinterpret_cast<uint8_t *>(urma_sock->recv_buf_addr_);

        std::memcpy(iov_dst, src_base + read_offset, copy_len);

        copied += copy_len;
        w.written += copy_len;
        w.offset += copy_len;
        if (w.offset >= static_cast<size_t>(w.iov[w.idx].iov_len)) {
            w.idx++;
            w.offset = 0;
        }
    }

    if (copied == available) {
        ResponseCtrlMessage(sock, WRITE_IN_BAND_ACK, imm_data.ubsocket.buffer_offset, flags);
        consume_seq_no_.store(seq + 1, std::memory_order_release);
        slot.rx_state.store(URMA_RX_STS_IDLE, std::memory_order_release);
        consume_offset_ = 0;
        return true;
    }

    consume_offset_ += copied;
    return false;
}

void UrmaRxOps::ReleaseBlockInSlot(DataSlot &slot) noexcept
{
    auto block = slot.data_block;
    slot.data_block = nullptr;

    while (block != nullptr) {
        auto next = block->GetNext();
        block->DecRef();
        block = next;
    }
}

uint64_t UrmaRxOps::CurrentTraceNsTime()
{
    if (ubsocket_prof_enabled == 0) {
        return 0;
    }
    uint64_t consumed = consume_seq_no_.load(std::memory_order_acquire);
    size_t idx = consumed % RING_SIZE;
    auto &slot = data_slots_[idx];
    if (slot.rx_state == URMA_RX_STS_DATA_READY) {
        return slot.start_read_time;
    }
    return 0;
}
} // namespace urma
} // namespace ubs
} // namespace ock
