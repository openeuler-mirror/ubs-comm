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
#ifndef UBS_COMM_UBSOCKET_LINK_TRACE_H
#define UBS_COMM_UBSOCKET_LINK_TRACE_H

#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>

#include "ubsocket_logger.h"

/*
 * Per-link lifecycle trace ("链路生命周期追踪").
 *
 * Purpose: when a handful of links out of thousands fail to establish, find
 * WHICH server-side stage they entered and never left. Every stage of a
 * link's life emits one line tagged [LINKTRACE] with the fd, so a stuck
 * link's whole story is one grep:
 *
 *     grep "LINKTRACE.*fd=1234 " server.log
 *
 * Stages (server accept side, in order):
 *   ACCEPTED        kernel accept() returned the fd. qwait_ms = how long the
 *                   connection sat fully-established in the kernel accept
 *                   queue before accept() picked it up (TCP_INFO
 *                   tcpi_last_ack_recv; -1 if unavailable), inq = bytes the
 *                   peer already sent (its negotiation request, if it was
 *                   waiting on us)
 *   NEGO_BEGIN      UB negotiation started (DoAccept)
 *   NEGO_DONE       negotiation ok / failed (rc)
 *   RES_LOCAL       our own umq create/bind/import finished (rc); the rest of
 *                   RES_DONE is waiting for the peer's resource ack
 *   RES_DONE        CreateSocketResources ok / failed (rc) — umq create+bind
 *   ESTABLISHED     handshake complete, OnEstablished
 *   HS_FATAL_CLOSE  handshake failed fatally, fd closed here (never delivered)
 * Client connect side, in order:
 *   C_CONNECT_BEGIN connect() entered
 *   C_TCP_DONE      TCP connect() returned (non-blocking: EINPROGRESS counts)
 *   C_NEGO_SENT     negotiation request written — the TCP handshake is
 *                   complete at this point (SYN/backlog wait is before it)
 *   C_NEGO_DONE     negotiation reply received (rc)
 *   C_RES_LOCAL     our own umq create/bind/import finished (rc)
 *   C_RES_DONE      peer's resource ack received (rc)
 *   C_ESTABLISHED   connect() about to return success
 * Data plane / teardown:
 *   EPOLL_ADD       host (brpc) registered the fd in its epoll; rx_pending=1
 *                   means data had already arrived before that
 *   RX_FIRST        first RX data delivered to this socket's queue by the
 *                   share-JFR runner; epoll=0 means the host had not
 *                   registered the fd yet at that moment
 *   RX_NOTIFY_SKIP  runner had data for the socket but no epoll to notify
 *                   (host not registered yet) — SetAddedEpollFd's rescue
 *                   (RX_RESCUE) must follow, otherwise the data is deaf
 *   RX_RESCUE       EPOLL_ADD found data already queued and notified for it
 *   TX_FIRST        first successful umq_post on this socket
 *   EMLINK_PARK     post got EMLINK (no free jetty node) → parked on wait queue
 *   EMLINK_WAKE     wait queue woke this socket (NotifyWritable)
 *   PEER_CLOSED     ubs_poll saw TCP EOF → return 0 (graceful close)
 *   RETIRED         close(): handed to reaper (fd held), or inline (rc=0)
 *   RELEASED        reaper released socket (umq_destroy) and closed fd
 *
 * Enable with env UBSOCKET_LINK_TRACE=1 (checked once). Off = one relaxed
 * atomic load per call site, no formatting. Lines are INFO level.
 * Time is µs since the first trace call, so deltas read directly.
 */
namespace ock {
namespace ubs {

class LinkTrace {
public:
    static ALWAYS_INLINE bool Enabled() noexcept
    {
        int s = state_.load(std::memory_order_relaxed);
        if (UNLIKELY(s == 0)) {
            const char *env = std::getenv("UBSOCKET_LINK_TRACE");
            s = (env != nullptr && env[0] == '1') ? 2 : 1;
            state_.store(s, std::memory_order_relaxed);
        }
        return s == 2;
    }

    static ALWAYS_INLINE uint64_t NowUs() noexcept
    {
        using namespace std::chrono;
        const uint64_t now = static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
        uint64_t base = base_us_.load(std::memory_order_relaxed);
        if (UNLIKELY(base == 0)) {
            base_us_.compare_exchange_strong(base, now, std::memory_order_relaxed);
            base = base_us_.load(std::memory_order_relaxed);
        }
        return now - base;
    }

    static ALWAYS_INLINE long Tid() noexcept
    {
        return static_cast<long>(syscall(SYS_gettid));
    }

    /* Accept-queue residency of a just-accepted TCP fd, in ms (how long the
     * kernel had it fully established before accept() returned it), or -1.
     * Only meaningful right after accept(); cost = one getsockopt. */
    static long AcceptQueueWaitMs(int fd) noexcept;
    /* Bytes already received on fd and not yet read (FIONREAD), or -1. */
    static long PendingBytes(int fd) noexcept;

private:
    static std::atomic<int> state_;       /* 0 = unchecked, 1 = off, 2 = on */
    static std::atomic<uint64_t> base_us_;
};

} // namespace ubs
} // namespace ock

/* Usage: UBS_LINK_TRACE(fd, "STAGE", "extra %d", v);  — the extra format is optional-ish:
 * always pass at least a literal "" if nothing to add. */
#define UBS_LINK_TRACE(fd, stage, fmt, ...)                                                                    \
    do {                                                                                                     \
        if (UNLIKELY(ock::ubs::LinkTrace::Enabled())) {                                                      \
            UBS_VLOG_INFO("[LINKTRACE] fd=%d stage=%s t_us=%llu tid=%ld " fmt "\n", (int)(fd), (stage),      \
                          (unsigned long long)ock::ubs::LinkTrace::NowUs(), ock::ubs::LinkTrace::Tid(),      \
                          ##__VA_ARGS__);                                                                     \
        }                                                                                                    \
    } while (0)

#endif // UBS_COMM_UBSOCKET_LINK_TRACE_H
