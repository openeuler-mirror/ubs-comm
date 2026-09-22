/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UBSOCKET_TX_CQE_POLLER_H
#define UBS_COMM_UBSOCKET_TX_CQE_POLLER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
class SocketBase;

class TxCqePoller : public LeakySingleton<TxCqePoller> {
    friend LeakySingleton<TxCqePoller>;

public:
    TxCqePoller(const TxCqePoller &) = delete;
    TxCqePoller &operator=(const TxCqePoller &) = delete;

    ~TxCqePoller();

    /// @brief Start the poller: registers a 1 ms timer_fd onto the
    ///        SHARE_JFR_RX_RUNNER epoll so TX CQE polling shares the same
    ///        bthread as RX CQE processing.
    /// @return 0 if successful, -1 otherwise.
    int Start();

    /// @brief Stop the poller and unregister the timer_fd.
    void Stop();

    /// @brief Add a socket to the detection queue.
    /// @param sock The socket pointer.
    void AddSocket(const SocketPtr &sock);

    /// @brief Remove a socket from the detection queue (unchanged: plain
    ///        removal; runs from EpollCtlDel, i.e. brpc's RemoveConsumer,
    ///        which precedes brpc's close — the socket is still live here).
    /// @param sock The socket pointer.
    void DelSocket(const SocketPtr &sock);

    /// @brief Hand a fully-closed socket to the dedicated reaper thread,
    ///        which drives its UB teardown (unbind + bounded TX/RX drain +
    ///        umq_destroy) in paced batches instead of letting the caller's
    ///        ref drop run the drain inline. Called from ubsocket close() with
    ///        the last ref that ArraySet::OverrideItem hands back: at that
    ///        point brpc has deregistered and closed the fd and the ArraySet
    ///        slot is cleared, so no new refs can be minted — the retire entry
    ///        is the sole owner (this is why the handoff is NOT in DelSocket,
    ///        where brpc may still touch the socket).
    ///        Why: the drain (up to UMQ_DESTROY_FLUSH_TIMEOUT_MS of busy CQE
    ///        polling per link when the peer left WRs in flight) used to run
    ///        inside ~UmqSocket on the brpc worker doing the close; under mass
    ///        teardown a few dozen such closes pinned the whole worker pool and
    ///        starved link establishment.
    ///        NOTE: teardown must NOT run on the poller/RX-runner thread
    ///        either — that thread is the shared-JFR RX data plane for every
    ///        link, and establishment completions flow through it (an earlier
    ///        revision hosted the sweep there and made the contention worse).
    ///        The raw fd travels WITH the socket and is closed by the reaper
    ///        only AFTER the UMQ is torn down (release point). Closing the fd
    ///        up front let the kernel recycle that number for a new link while
    ///        the old socket's UMQ (whose umq_ctx IS the fd, used by the
    ///        share-JFR runner to route completions) was still alive in the
    ///        reaper — the new link's completions were misrouted and its
    ///        handshake hung until brpc's timeout: exactly one such collision
    ///        per run at the warmup→establish boundary ("1 of N fails").
    ///        develop's inline path never had this window because
    ///        umq_destroy ran before close(fd) returned; this restores that
    ///        ordering guarantee while keeping the drain off the workers.
    /// @param fd   The raw fd (already shutdown()'d by the caller); -1 = none.
    /// @param sock The last reference (moved in).
    /// @return true if the socket was parked (the reaper now owns fd + sock);
    ///         false if the reaper is not running — the caller must close the
    ///         fd itself and let its ref drop (inline teardown as before).
    bool RetireSocket(int fd, SocketPtr &&sock);
    /// A reference was parked in ArraySet's deferred-release queue: wake the
    /// reaper so it is dropped promptly even if nothing is ever retired.
    void NotifyDeferred() noexcept;

    /// @brief One reaper round: step EVERY retired socket
    ///        (UmqSocket::RetireStep — unbind on first call; in POOL mode
    ///        that is the whole job, in SINGLE mode also ForceDrainTx of the
    ///        socket's private CQ), and release (~UmqSocket → umq_destroy,
    ///        then close the fd) at most kRetireBatch of those that are done
    ///        or past their deadline (UMQ_DESTROY_FLUSH_TIMEOUT_MS); the rest
    ///        go back on the list.
    ///        POOL mode never polls from the reaper: a logic queue's TX CQ is
    ///        the shared jetty-pool node's CQ, polled by the RX runner for
    ///        live sockets and re-borrowable by new links after unbind —
    ///        polling it from a second thread races that runner and
    ///        misdispatches other sockets' completions (field: server
    ///        segfault). umq_destroy returns the borrowed node itself and the
    ///        runner's orphan sweep reaps any residual CQEs single-threaded;
    ///        the node goes back to the (small, shared) pool at ioctl speed.
    ///        Only releases are capped: they are the driver ioctls that
    ///        serialize against create/bind. Runs on the reaper thread;
    ///        exposed for tests.
    /// @return true if any retired socket was released this round.
    bool RetireSweep() noexcept;

    /// @brief Number of sockets currently awaiting deferred teardown.
    size_t RetiredCount() const noexcept;

    /// @brief Max releases (umq_destroy + fd close) per reaper round. Bounds
    ///        how long the reaper holds the per-device verbs path (destroy
    ///        serializes against umq_create / bind of links being
    ///        established), so a mass-delete burst is interleaved with
    ///        establishment instead of monopolizing the driver. Does NOT cap
    ///        draining — see RetireSweep.
    static constexpr size_t kRetireBatch = 64;
    /// @brief Sleep between reaper rounds while work remains (pacing).
    static constexpr uint32_t kRetireRoundIntervalUs = 500;

    /// @brief Poll TX CQEs for all registered sockets.  Called by the
    ///        SHARE_JFR_RX_RUNNER when the timer_fd fires.
    void PollAllSockets() noexcept;

    /// @brief Drain the timer_fd so epoll_wait does not immediately refire.
    ///        Must be called from the TX_CQE_TIMER branch of ProcessOneEvent
    ///        before entering RunUnifiedActiveLoop (flag=ON path), because
    ///        unlike PollAllSockets, RunUnifiedActiveLoop does not drain
    ///        the timer internally. No-op when timer_fd is invalid.
    void DrainTimerFd() noexcept;

    /// @brief Single bounded TX sweep: iterate sockets_, call ForceDrainTx
    ///        (first-empty-exit) + RetryPendingReadsForSocket on each.
    ///        Returns true if any CQE was reclaimed (progress), false if
    ///        all sockets were empty. When all sockets are empty, clears
    ///        any_inflight_ (release) so the active loop can enter idle.
    ///        Guarded by stopped_ — returns false immediately after Stop().
    ///        Used by RunUnifiedActiveLoop to interleave TX draining with
    ///        RX polling on the same thread.
    /// @param sweep_orphan When true, also poll registered orphan main UMQs
    ///        (POOL mode). Should be true only when called from a context
    ///        that also polls RX (RunUnifiedActiveLoop(main_umq)), to avoid
    ///        starving RX in TX-only contexts (RunUnifiedActiveLoop() from
    ///        TX_CQE_TIMER/TX_WAKE).
    bool TxSweepOnce(bool sweep_orphan = true, bool quick_poll = false) noexcept;

    /// @brief Mark that at least one socket has in-flight TX WRs. Called
    ///        from umq_post(TX) paths so the poller knows there is work
    ///        to drain. The flag is cleared by an all-empty sweep.
    void NotifyInflight() noexcept;
    /// @brief Put a socket into the poller's ACTIVE set. Call it whenever
    ///        this socket starts to need per-round attention: a TX WR was
    ///        posted (SEND / ctrl / READ), a READ was queued to
    ///        pending_reads, or a pinned entry / receiver ctx got a
    ///        deadline. Idempotent and cheap (one atomic on the socket;
    ///        the mutex is taken only on the false->true transition), safe
    ///        from any thread. The sweeps (PollAllSockets / TxSweepOnce)
    ///        visit ONLY the active set and drop a socket from it once it
    ///        has no in-flight TX WRs and no bigdata work left — so 40k
    ///        idle links cost the poller nothing per round (previously
    ///        every round copied and walked the whole registry: O(N) on the
    ///        RX runner thread, which is what made first-RPC latency grow
    ///        with the number of open links).
    /// @param sock the socket (raw pointer: the caller must hold a ref, as
    ///        every post path does; a SocketPtr is taken internally when
    ///        the socket is enqueued).
    void MarkActive(Socket *sock) noexcept;
    void MarkActive(const SocketPtr &sock) noexcept
    {
        MarkActive(sock.Get());
    }
    /// @brief Number of sockets currently in the active set (tests / stats).
    size_t ActiveCount() const noexcept;

    /// @brief Query whether any socket has in-flight TX WRs.
    /// @return true if any in-flight WRs exist.
    bool AnyTxInflight() const noexcept;

    /// @brief Set the sleeping flag. Called by RunUnifiedActiveLoop before
    ///        entering DEEP_IDLE (store true, release) and by ProcessOneEvent
    ///        on wakeup (store false, release). Exposed as a public method
    ///        because the state machine lives in UmqShareJfrEpollRunnerOps.
    /// @param val new sleeping state.
    /// @param order memory ordering for the store.
    void SetSleeping(bool val, std::memory_order order) noexcept
    {
        sleeping_.store(val, order);
    }

    /// @brief Query the sleeping flag. Used by tests and the lost-wakeup
    ///        recheck logic.
    bool IsSleeping() const noexcept
    {
        return sleeping_.load(std::memory_order_acquire);
    }

    /// @brief Wake the unified poller from deep sleep when a TX WR is
    /// posted from an application thread. No-op when not sleeping
    /// or after Stop(). EBADF (close race) is downgraded to DEBUG.
    void NotifyPosted() noexcept;

    /// @brief Register a main umq for orphan CQE sweep (POOL mode only,
    /// flag=ON path). Replaces the 1ms TP_TX_TIMER round-robin poll of
    /// the main umq: when a socket closes with in-flight WRs, its logic
    /// umq is destroyed and nobody polls its borrowed jetty node; the
    /// main-umq round-robin poll (with UMQ_IO_OPTION_FLAG_TP_HANDLE_IDX,
    /// tp_idx=0) acts as the scavenger. Called by UmqTransportPool::WarmUp.
    /// @param main_umqh The main umq handle to register.
    void RegisterOrphanSweep(uint64_t main_umqh);

private:
    TxCqePoller();

    /// @brief Sweep all registered main umqs for orphan CQEs (POOL mode,
    /// flag=ON). For each main umq, performs a round-robin TX poll with
    /// FLAG_TP_HANDLE_IDX (tp_idx=0) so umq dispatches to all jetty
    /// nodes. CQEs whose socket (resolved via buf_pro->umq_ctx) is gone
    /// are skipped; the freed_jettys counter from PollUmqTxInternal
    /// drives UmqTpWaitQueue::WakeUp to release EMLINK-blocked sockets.
    /// Called at the end of TxSweepOnce when the registry is non-empty.
    /// @return true if any CQE was reclaimed (progress), false otherwise.
    bool SweepOrphanPools() noexcept;

    /// @brief Switch timerfd to SLOW (Fallback_MS) when active set is empty.
    /// Both flag=ON and flag=OFF: MarkActive switches back to FAST immediately.
    void MaybeSwitchToSlow() noexcept;
    /// @brief Switch timerfd to FAST (100us) when a new socket is enqueued.
    /// Both flag=ON and flag=OFF: prevents SQ-full under burst.
    void MaybeSwitchToFast() noexcept;

    u_mutex_t *mutex_ = nullptr;
    /* sockets_: registry of every socket registered via AddSocket (used by
     * Stop and as the ground truth of membership). Guarded by mutex_. Each
     * socket's tx_poller_slot_ is its index here (-1 = not registered) so
     * AddSocket/DelSocket are O(1) — with 40k links the former linear
     * scans cost ~1 ms per call under mutex_, contending with the sweeps. */
    std::vector<SocketPtr> sockets_;
    /* active_: the sockets a sweep has to look at (see MarkActive). Producers
     * append under active_mutex_ (tiny critical section); the sweep — which
     * only ever runs on the SHARE_JFR_RX_RUNNER thread — snapshots it, works
     * off the snapshot, then rebuilds it from the survivors plus whatever was
     * appended meanwhile. Membership is mirrored in Socket::tx_poller_active_
     * so a socket has at most one entry. */
    std::vector<SocketPtr> active_;
    mutable std::mutex active_mutex_;
    /// @brief Snapshot the active set (append-only from producers, so the
    ///        first @p n entries of active_ stay stable until RebuildActive).
    void SnapshotActive(std::vector<SocketPtr> &out) noexcept;
    /// @brief Replace active_ by @p survivors plus the entries appended after
    ///        the snapshot of size @p snapshot_size.
    void RebuildActive(std::vector<SocketPtr> &survivors, size_t snapshot_size) noexcept;
    /// @brief Decide whether a swept socket stays in the active set. Drops
    ///        deregistered sockets; for the rest clears tx_poller_active_ and
    ///        re-checks (Dekker with MarkActive's fence) so a post that races
    ///        with the drop can never leave a needy socket outside the set.
    void RetainOrDrop(const SocketPtr &sock, SocketBase *sockBase, std::vector<SocketPtr> &survivors) noexcept;
    /* retired_: sockets handed over by DelSocket, awaiting incremental
     * teardown on the reaper thread (see RetireSweep / ReaperLoop). Guarded
     * by retire_mutex_ (a plain std::mutex + condvar: this list is touched by
     * brpc workers on close and by the reaper — never by the RX runner — so
     * it must not share mutex_ with the poller's hot sweep). Each entry
     * keeps the socket alive (SocketPtr) until its drain is complete or its
     * deadline passes; deadline is absolute ms. */
    struct RetiredSocket {
        SocketPtr sock;
        int fd;               /* closed by the reaper after UMQ teardown; -1 = none */
        uint64_t deadline_ms;
    };
    std::vector<RetiredSocket> retired_;
    mutable std::mutex retire_mutex_;
    std::condition_variable retire_cv_;
    std::thread reaper_thread_;
    std::atomic<bool> reaper_stop_{false};
    /* Serializes Start()/Stop(). started_ alone cannot: Stop() flips it at
     * entry and then tears down for milliseconds (reaper join), during which
     * a concurrent Start() from SocketBase::Create sees started_==false and
     * rebuilds the poller — including `reaper_thread_ = std::thread(...)`
     * over a still-joinable thread → std::terminate (issue #30, core 1). */
    std::mutex lifecycle_mutex_;
    /* Latched by the first Stop(). Stop() only runs at process teardown
     * (ubsocket_uninit / dtor), so once set, Start() must refuse forever:
     * a poller half-rebuilt behind the exit path dangles timer/wake fds on
     * a runner that is being dismantled. */
    std::atomic<bool> shutdown_{false};
    /* Serializes every TX-side touch of the shared transport pool between
     * the unified-loop sweeps (TxSweepOnce / SweepOrphanPools — the runner
     * event can be picked up by any dispatcher, nothing stops two entering
     * at once) and the reaper's per-socket drain/destroy (RetireStep /
     * ~UmqSocket -> umq_destroy). umq's TX poll round-robins shared
     * jetty-pool state that destroy tears down (issue #30 runtime cores:
     * SIGSEGV inside umq_ub_poll_tx on the shared umqh_tp). Sweeps
     * try-lock and skip a round on contention (the 100us timer retries);
     * the reaper locks per retired entry, never for the whole batch, so
     * active-TX drains are not starved during mass teardown. */
    std::mutex tx_pool_touch_mutex_;

    /// @brief Reaper thread body: wait for retired sockets, then run paced
    ///        RetireSweep rounds until the list is empty or stop is requested.
    void ReaperLoop() noexcept;
    void StartReaper();
    /// Drop the references parked in ArraySet's deferred-release queue (under
    /// tx_pool_touch_mutex_, same rule as the batch releases in RetireSweep).
    void DrainDeferred() noexcept;
    static void DeferredNotifierThunk(void *ctx) noexcept;
    /* No periodic wake: the notify path cannot lose a wake-up (deferred_signal_ is
     * set under retire_mutex_, the same mutex the wait predicate reads), and a
     * self-waking reaper would run into static destruction in processes that never
     * call ubsocket_uninit (unit-test binaries), where the lock registry behind
     * ArraySet's mutex may already be gone. */
    static constexpr size_t kDeferredDrainReportThreshold = 1024;
    bool deferred_signal_ = false; /* guarded by retire_mutex_ */
    void StopReaper();
    /* orphan_main_umqs_: main umq handles registered by UmqTransportPool
     * for round-robin orphan CQE sweep (POOL mode, flag=ON). Replaces the
     * 1ms TP_TX_TIMER. Guarded by mutex_. Empty in SINGLE mode or when
     * UBS_TX_UNIFIED_POLL_ENABLED=OFF. */
    std::vector<uint64_t> orphan_main_umqs_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> started_{false};

    int timer_fd_ = -1;
    /* wake_fd_: eventfd written by NotifyPosted to wake the active loop
     * from deep sleep. Only created when UBS_TX_UNIFIED_POLL_ENABLED=ON. */
    int wake_fd_ = -1;
    /* sleeping_: true when the active loop has entered DEEP_IDLE (no
     * inflight WRs and no CQEs for several rounds). NotifyPosted reads
     * this to decide whether to pay the eventfd_write syscall. */
    std::atomic<bool> sleeping_{false};
    /* any_inflight_: global "any socket has in-flight TX WRs" flag.
     * Set by NotifyInflight on umq_post(TX); cleared by an all-empty
     * sweep. Lost-wakeup correctness does NOT depend on its precision
     * (triple fallback: sleeping_ + eventfd + 100ms timer). */
    std::atomic<bool> any_inflight_{false};
    /* timer_fast_: adaptive timer for both flag=ON and flag=OFF. true =
     * 100us FAST period; false = UBS_TX_POLLER_FALLBACK_MS SLOW period.
     * Switched by PollAllSockets/TxSweepOnce (FAST→SLOW on empty active
     * set) and MarkActive (SLOW→FAST on new enqueue). Atomic: written by
     * the runner thread (sweeps) and by posting threads (MarkActive). */
    std::atomic<bool> timer_fast_{true};
    /* active_dirty_: set by MarkActive under active_mutex_ after push_back,
     * cleared by TxSweepOnce/PollAllSockets under active_mutex_ after copy.
     * When false, the runner thread reuses active_cache_ without locking.
     * The lock-protected clear closes the race with MarkActive's set. */
    std::atomic<bool> active_dirty_{true};
    /* active_cache_: snapshot reused across sweeps while active_dirty_ is
     * false; written only by the sweep thread (snapshot block, and
     * RebuildActive's re-sync under active_mutex_ when sockets are dropped).
     * The re-sync keeps it from accumulating stale SocketPtr refs that would
     * delay RetireSocket's SoleRef() close up to the retire deadline. */
    std::vector<SocketPtr> active_cache_;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_TX_CQE_POLLER_H
