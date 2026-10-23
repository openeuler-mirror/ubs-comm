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
#include "urma_event_dispatch.h"

#include <cstdint>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_errno.h"
#include "common/ubsocket_mpsc_ring_queue.h"
#include "urma_data_rx_ops.h"
#include "urma_setting.h"
#include "urma_socket.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

namespace {
/*
 * Map an EpollHandleMode onto its process-wide characteristics. This is the ONLY switch on
 * the mode enum in production code: adding a mode means adding one subclass below and one
 * case here.
 */
UrmaHandleModeCaps CapsOf(EpollHandleMode mode) noexcept
{
    switch (mode) {
        case DISPATCHER_THREAD_POOL:
            return {false, true};
        case THREAD_PER_CONNECTION:
        default:
            return {true, false};
    }
}
} // namespace

UrmaHandleModeCaps UrmaHandleModeCapsNow() noexcept
{
    return CapsOf(UrmaSetting::UBS_EPOLL_MODE);
}

/*
 * THREAD_PER_CONNECTION: the poller thread (CQE side) processes each event immediately and the
 * connection's own epoll thread wakes on the eventfd. No deferred queue exists.
 */
class InlineRxDispatcher final : public UrmaRxDispatcher {
public:
    Result HandleDataReached(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no,
                             uint64_t write_imm) noexcept override
    {
        return ops.ProcessDataReached(sock, seq_no, write_imm);
    }

    Result HandleDataReadFinished(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no) noexcept override
    {
        return ops.ProcessDataReadFinished(sock, seq_no);
    }

    void DrainEvents(UrmaRxOps &ops, const SocketPtr &sock) noexcept override
    {
        (void)ops;
        (void)sock;
    }
};

/*
 * DISPATCHER_THREAD_POOL: the poller (epoll_wait thread) only pushes the event and wakes the
 * read side; the real slot/QP progression happens when ReadV drains the queue. This keeps a
 * single executor per connection (see UrmaRxDispatcher).
 *
 * Invariant: the queue depth (QUEUE_CAPACITY = 1024) must exceed the max in-flight messages per
 * connection (~ send_buf capacity / URMA_SND_BUFF_ALLOC_UNIT; default 128KB -> <=128; the sender
 * does not keep allocating send_buf before it receives an ACK), so a Push failure is unreachable
 * under the default configuration. If it ever happens it is treated as a connection-level
 * internal error: log and drop, returning UBS_ERROR — never fall back to inline processing on
 * the poller thread.
 *
 * DATA_REACHED carries no seq_no: it is re-read from recv_buf during the drain because
 * the sender does not reuse a buffer_offset before it receives the ACK.
 */
class QueueRxDispatcher final : public UrmaRxDispatcher {
public:
    Result HandleDataReached(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no,
                             uint64_t write_imm) noexcept override
    {
        (void)ops;
        (void)seq_no;
        EnsureQueue();
        auto command = EpollDispatchCommandUnion::MakeDataReached(static_cast<uint32_t>(write_imm));
        if (UNLIKELY(!queue_->Push(command.value))) {
            LogQueueFull(sock, command.Type());
            return UBS_ERROR;
        }
        static_cast<UrmaSocket *>(sock.Get())->NotifySocketReadable();
        return UBS_DEFERRED;
    }

    Result HandleDataReadFinished(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no) noexcept override
    {
        (void)ops;
        EnsureQueue();
        auto command = EpollDispatchCommandUnion::MakeDataReadFinished(static_cast<uint32_t>(seq_no));
        if (UNLIKELY(!queue_->Push(command.value))) {
            LogQueueFull(sock, command.Type());
            return UBS_ERROR;
        }
        static_cast<UrmaSocket *>(sock.Get())->NotifySocketReadable();
        return UBS_DEFERRED;
    }

    void DrainEvents(UrmaRxOps &ops, const SocketPtr &sock) noexcept override
    {
        constexpr uint64_t BATCH_SIZE = 64;
        uint64_t events[BATCH_SIZE];
        if (UNLIKELY(queue_ == nullptr)) {
            return;
        }

        auto urma_sock = static_cast<UrmaSocket *>(sock.Get());
        uint64_t count;
        while ((count = queue_->MultiPop(events, BATCH_SIZE)) > 0) {
            for (auto i = 0U; i < count; i++) {
                EpollDispatchCommandUnion command{events[i]};
                if (command.Type() == EpollDispatchCommandType::DATA_REACHED) {
                    WriteImmData imm_data{command.ImmData()};
                    auto recv_offset = imm_data.ubsocket.buffer_offset * UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT;
                    auto req = reinterpret_cast<UrmaCtrlMessage *>(urma_sock->recv_buf_addr_ + recv_offset);
                    ops.ProcessDataReached(sock, req->head.sequence_no, imm_data.data);
                } else if (command.Type() == EpollDispatchCommandType::DATA_READ_FINISHED) {
                    ops.ProcessDataReadFinished(sock, command.SeqNo());
                }
            }
        }
    }

private:
    ALWAYS_INLINE void EnsureQueue()
    {
        if (UNLIKELY(queue_ == nullptr)) {
            queue_ = std::make_unique<MPSCRingQueue<uint64_t>>(QUEUE_CAPACITY);
        }
    }

    ALWAYS_INLINE void LogQueueFull(const SocketPtr &sock, EpollDispatchCommandType type) const noexcept
    {
        UBS_VLOG_ERR("fd(%d) dispatch queue full(type=%d), drop. queue depth(%llu) must exceed max in-flight "
                     "(~ send_buf / URMA_SND_BUFF_ALLOC_UNIT), treat as internal error",
                     sock->Fd(), static_cast<int>(type), static_cast<unsigned long long>(QUEUE_CAPACITY));
    }

    static constexpr uint64_t QUEUE_CAPACITY = 1024; /* must exceed max in-flight, see class comment */

    /*
     * MPSC deferred-event queue: the poller thread enqueues, the ReadV (consumer) thread drains —
     * cross-thread, unlike ReadableEventQueue whose producer and consumer are the same epoll_wait
     * thread, hence the shared MPSC ring is the right primitive here. Lazily created on the first
     * enqueue (poller-side only).
     */
    std::unique_ptr<MPSCRingQueue<uint64_t>> queue_;
};

std::unique_ptr<UrmaRxDispatcher> CreateUrmaRxDispatcher()
{
    if (CapsOf(UrmaSetting::UBS_EPOLL_MODE).epoll_wait_driven) {
        return std::unique_ptr<UrmaRxDispatcher>(new QueueRxDispatcher());
    }
    return std::unique_ptr<UrmaRxDispatcher>(new InlineRxDispatcher());
}
} // namespace urma
} // namespace ubs
} // namespace ock
