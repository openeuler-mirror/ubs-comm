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
#ifndef UBS_COMM_URMA_EVENT_DISPATCH_H
#define UBS_COMM_URMA_EVENT_DISPATCH_H

#include <cstdint>
#include <memory>

#include "core/ubsocket_core_types.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaRxOps;

/*
 * Per-connection strategy that decides how poller-produced RX events reach the slot
 * state machine:
 *   - InlineRxDispatcher (THREAD_PER_CONNECTION): the poller thread processes every event
 *     immediately (ProcessDataReached / ProcessDataReadFinished); readiness is signalled
 *     through the socket eventfd.
 *   - QueueRxDispatcher (DISPATCHER_THREAD_POOL): ubsocket spawns no worker pool itself — a
 *     single dispatcher (epoll_wait) thread enqueues the event onto an MPSC queue and wakes the
 *     read side (returns UBS_DEFERRED); an EXTERNAL IO thread pool of the application (e.g. brpc
 *     IO threads) drains it at the start of ReadV. The poller never advances slots nor posts on
 *     the QP, so each connection has a single executor and needs no cross-thread lock.
 *
 * One EpollHandleMode maps onto exactly one strategy. To add a third mode, add a subclass and
 * update the single switch in UrmaHandleModeCapsNow() (urma_event_dispatch.cpp); no other code
 * references the mode enum.
 */
class UrmaRxDispatcher {
public:
    virtual ~UrmaRxDispatcher() = default;

    /*
     * Poller-side WRITE_IMM arrival (URMA_WRITE_IMM CQE).
     * Inline: forwards to UrmaRxOps::ProcessDataReached and returns its result.
     * Queue: enqueues a DATA_REACHED event, wakes the read side and returns
     *        UBS_DEFERRED (UBS_ERROR when the queue is full, which violates the capacity
     *        invariant; never falls back to poller-side inline processing).
     */
    virtual Result HandleDataReached(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no,
                                     uint64_t write_imm) noexcept = 0;

    /*
     * Poller-side RDMA READ completion (READ_DATA_REQUEST CQE). See HandleDataReached.
     */
    virtual Result HandleDataReadFinished(UrmaRxOps &ops, const SocketPtr &sock, uint64_t seq_no) noexcept = 0;

    /*
     * Consumer side, invoked at the start of every ReadV. Queue mode drains the deferred
     * events (the only place that advances slots / posts QP work for that connection);
     * Inline mode is a no-op.
     */
    virtual void DrainEvents(UrmaRxOps &ops, const SocketPtr &sock) noexcept = 0;
};

/* Create the per-connection strategy matching the active EpollHandleMode. */
std::unique_ptr<UrmaRxDispatcher> CreateUrmaRxDispatcher();

/*
 * Process-wide characteristics of the active EpollHandleMode. Both UrmaPoller and
 * UrmaEventPoll consult this instead of the raw enum, so adding a mode only changes the
 * switch in UrmaHandleModeCapsNow().
 */
struct UrmaHandleModeCaps {
    bool dedicated_poller_thread; /* per-connection: an independent thread waits/polls the JFC */
    bool epoll_wait_driven;       /* dispatcher: epoll_wait registers JFCE fds and polls the JFC inline */
};

UrmaHandleModeCaps UrmaHandleModeCapsNow() noexcept;

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_EVENT_DISPATCH_H
