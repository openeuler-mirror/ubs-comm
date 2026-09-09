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
#ifndef UBS_COMM_URMA_READABLE_EVENT_QUEUE_H
#define UBS_COMM_URMA_READABLE_EVENT_QUEUE_H

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

#include <sys/epoll.h>

#include "common/ubsocket_errno.h"

namespace ock {
namespace ubs {
namespace urma {

/*
 * FIFO of readable urma-socket epoll_events that were produced inline (during JFCE dispatch)
 * but could not all be returned by the last epoll_wait. It is drained ahead of every blocking
 * epoll_wait so no event is lost. Behavior:
 *   - per-fd de-dup: while an fd has an unreturned event, further records for the same fd are
 *     ignored (equivalent to the eventfd counter semantics but without a wakeup storm);
 *   - single delivery: when the fd is delivered through the kernel eventfd path on the wait
 *     thread (ClearPending), any buffered entry for it is dropped, so the same readiness is
 *     never surfaced twice;
 *   - stale drop: when drained, entries whose fd is no longer registered on the epoll (already
 *     closed / removed) are dropped instead of being returned to the application — this is the
 *     safety net for unregister (EPOLL_CTL_DEL, possibly issued from another thread) which only
 *     clears the pending mark;
 *   - backpressure: when the ring is full TryRecord returns false and the caller falls back to
 *     the eventfd notification.
 *
 * Invariant for wait-thread operations (TryRecord/Drain/ClearPending): a buffered entry exists
 * for an fd if and only if that fd is in the pending set. ForgetFd (possibly cross-thread) clears
 * the mark while the entry lingers until the next Drain drops it as stale.
 * The queue state is owned by the single epoll_wait thread (producer and consumer are the same
 * thread), so it needs no internal lock. The owner only serializes the "is the fd still
 * registered" check that Drain performs through the is_registered callback.
 *
 * Concurrency contract: a kernel epoll fd is normally driven by one thread (its epoll_wait plus
 * the EPOLL_CTL_ADD/DEL calls for its sockets). If EPOLL_CTL_DEL must run on another thread it is
 * limited to ForgetFd (pending mark only), so a concurrent erase of the same fd's mark between
 * the wait thread and the deleting thread is the only tolerated boundary.
 *
 * Design note (why a dedicated ring instead of reusing the common queues): the producer and the
 * consumer are the SAME thread, so no cross-thread atomics / cache-line padding are needed; the
 * class also requires per-fd de-dup plus a max-limited drain that validates every popped entry
 * and drops stale fds mid-loop. The shared SPSC ring (common/ubsocket_spsc_ring_queue.h) targets
 * concurrent single-producer/single-consumer threads, requires a power-of-two capacity with
 * full-capacity semantics (no reserved slot) and moves items out on pop — none of that matches
 * this use, so a plain head/tail ring over a vector is kept deliberately. The dispatcher's
 * deferred-event queue (MPSCRingQueue in QueueRxDispatcher) is cross-thread (poller enqueues,
 * ReadV drains) and is therefore a different concern.
 */
class ReadableEventQueue {
public:
    ReadableEventQueue() noexcept = default;
    ~ReadableEventQueue() = default;

    /*
     * Allocate the ring (lazy). No-op once initialized. @param capacity ring depth (must be > 0).
     */
    Result Init(uint32_t capacity) noexcept;

    /*
     * Record one readable event for fd. Returns true if recorded or already pending for fd
     * (de-dup); false when the ring is full or not yet initialized (caller should fall back to
     * eventfd).
     */
    bool TryRecord(int fd, const struct epoll_event &evt) noexcept;

    /*
     * Move up to maxevents queued events into out. Every popped entry clears the pending mark;
     * entries whose fd fails is_registered (stale) are dropped and the drain continues.
     * @return number of events actually written to out.
     */
    uint32_t Drain(struct epoll_event *out, uint32_t maxevents, const std::function<bool(int)> &is_registered) noexcept;

    /*
     * The fd was already delivered through the kernel eventfd path: drop any buffered entry and
     * its pending mark so the same readiness is not surfaced again by a later drain.
     */
    void ClearPending(int fd) noexcept;

    /*
     * The fd was removed from this epoll (EPOLL_CTL_DEL, possibly from another thread): only
     * clear its pending mark. The buffered entry is dropped later by Drain's stale check once
     * the fd is unregistered (see class comment).
     */
    void ForgetFd(int fd) noexcept;

    /* Test/observation helpers. */
    bool IsPending(int fd) const noexcept;
    uint32_t PendingCount() const noexcept;
    uint32_t Size() const noexcept;
    uint32_t Capacity() const noexcept;

private:
    struct Entry {
        int fd = -1;
        struct epoll_event event {};
    };

    /* Compact the ring, removing every buffered entry for fd; also clears the pending mark. */
    void RemoveBuffered(int fd) noexcept;

    std::vector<Entry> ring_;
    uint32_t capacity_ = 0; /* 0 = not initialized; one ring slot is kept unused (head==tail = empty) */
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    std::unordered_set<int> pending_;
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_READABLE_EVENT_QUEUE_H
