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

#include "ubsocket_tx_cqe_poller.h"

#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <algorithm>
#include <iterator>

#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_port_cooldown.h"
#include "common/ubsocket_scope_exit.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket_helper.h"
#include "core/umq/umq_socket.h"
#include "core/umq/umq_tx_helper.h"
#include "ubsocket_bigdata.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_socket.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
TxCqePoller::TxCqePoller()
{
    /* mutex_ must exist before any RegisterOrphanSweep / SweepOrphanPools
     * call. In POOL mode, UmqTransportPool::WarmUp invokes
     * RegisterOrphanSweep during UmqBackend::UmqInit, which runs BEFORE
     * Start() (Start() is called from SocketBase::Create, i.e. when the
     * first UB socket is created). Without this ctor init, RegisterOrphanSweep
     * would lock a null mutex_. Lock ops are already registered by brpc
     * at this point. */
    mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
}

TxCqePoller::~TxCqePoller()
{
    Stop();
}

int TxCqePoller::Start()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (shutdown_.load(std::memory_order_acquire)) {
        /* Process is tearing down; refuse to rebuild. The caller
         * (SocketBase::Create) fails the socket create, which is the
         * correct outcome for a create racing process exit. */
        return -1;
    }
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return 0;
    }
    auto started_restorer = MakeScopeExit([this]() { started_ = false; });

    /* mutex_ is created in the ctor so RegisterOrphanSweep (which runs
     * before Start() in POOL mode) finds it valid. Just defend against
     * a failed create (e.g. OOM) here. */
    if (mutex_ == nullptr) {
        UBS_VLOG_ERR("TxCqePoller mutex create failed.\n");
        return -1;
    }

    /* Timer period: both flag=ON and flag=OFF start at 100us (FAST) to
     * prevent SQ-full under burst. Adaptive SLOW switching (to Fallback_MS)
     * happens when active set becomes empty; MarkActive switches back to
     * FAST immediately on new post. flag=ON also relies on the eventfd
     * (NotifyPosted) for us-level wake from DEEP_IDLE. */
    const bool unifiedEnabled = GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED;
    const auto timerPeriodNs = static_cast<int64_t>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000LL;

    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0) {
        UBS_VLOG_ERR("TxCqePoller timerfd_create failed: %d.\n", errno);
        return -1;
    }
    auto timer_destroyer = MakeScopeExit([this]() {
        LibcApi::close(timer_fd_);
        timer_fd_ = -1;
    });

    struct itimerspec interval;
    interval.it_value.tv_sec = 0;
    interval.it_value.tv_nsec = timerPeriodNs;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = timerPeriodNs;
    if (timerfd_settime(timer_fd_, 0, &interval, nullptr) < 0) {
        UBS_VLOG_ERR("TxCqePoller timerfd_settime failed: %d.\n", errno);
        return -1;
    }

    /* flag=ON: create wake eventfd for NotifyPosted. */
    if (unifiedEnabled) {
        wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wake_fd_ < 0) {
            UBS_VLOG_ERR("TxCqePoller wake eventfd_create failed: %d.\n", errno);
            return -1;
        }
    }
    auto wake_destroyer = MakeScopeExit([this]() {
        if (wake_fd_ >= 0) {
            LibcApi::close(wake_fd_);
            wake_fd_ = -1;
        }
    });

    stopped_ = false;
    sleeping_.store(false, std::memory_order_relaxed);
    any_inflight_.store(false, std::memory_order_relaxed);
    /* Both flag=ON and flag=OFF start in FAST. Resetting the snapshot cache
     * here (instead of Stop) is race-free: the timer is not yet registered
     * on the runner epoll, so no sweep can be iterating active_cache_. */
    timer_fast_.store(true, std::memory_order_relaxed);
    active_dirty_.store(true, std::memory_order_relaxed);
    active_cache_.clear();

    /* Register the timer_fd onto the SHARE_JFR_RX_RUNNER epoll so that TX
     * CQE polling shares the same bthread as RX CQE processing.  This
     * eliminates a separate EventDispatcher consumer and avoids bthread
     * scheduling latency between the two poll paths. */
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    if (runner.Start() != 0) {
        UBS_VLOG_ERR("TxCqePoller SHARE_JFR_RX_RUNNER start failed.\n");
        return -1;
    }

    RunnerEventData event_data{};
    event_data.event_data.type = RUNNER_EVENT_TYPE_TX_CQE_TIMER;
    event_data.event_data.data = static_cast<uint64_t>(timer_fd_);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = event_data.u64}};
    if (runner.AddEpollEvent(timer_fd_, &ev, nullptr) != 0) {
        UBS_VLOG_ERR("TxCqePoller AddEpollEvent to SHARE_JFR_RX_RUNNER failed: %d.\n", errno);
        return -1;
    }

    /* flag=ON: also register wake_fd_ as a TX_WAKE event. */
    if (unifiedEnabled) {
        RunnerEventData wake_data{};
        wake_data.event_data.type = RUNNER_EVENT_TYPE_TX_WAKE;
        wake_data.event_data.data = static_cast<uint64_t>(wake_fd_);
        struct epoll_event wake_ev = {.events = EPOLLIN, .data = {.u64 = wake_data.u64}};
        if (runner.AddEpollEvent(wake_fd_, &wake_ev, nullptr) != 0) {
            UBS_VLOG_ERR("TxCqePoller AddEpollEvent(wake) to SHARE_JFR_RX_RUNNER failed: %d.\n", errno);
            return -1;
        }
    }

    timer_destroyer.Deactivate();
    wake_destroyer.Deactivate();
    started_restorer.Deactivate();

    /* Dedicated teardown thread: retired sockets are drained/destroyed here,
     * off both the brpc workers and the shared-JFR RX runner. */
    StartReaper();
    /* Deferred releases (ArraySet::OverrideItem/RemoveItem) wake the reaper too;
     * without this only a successful link's close ever drained them (issue #49). */
    ArraySet<Socket>::GetInstance().SetDeferredNotifier(&TxCqePoller::DeferredNotifierThunk, this);
    return 0;
}

void TxCqePoller::DeferredNotifierThunk(void *ctx) noexcept
{
    auto *self = static_cast<TxCqePoller *>(ctx);
    if (self != nullptr) {
        self->NotifyDeferred();
    }
}

void TxCqePoller::NotifyDeferred() noexcept
{
    {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        deferred_signal_ = true;
    }
    retire_cv_.notify_one();
}

void TxCqePoller::DrainDeferred() noexcept
{
    /* Only reached after an explicit wake-up (a parked release or a sweep), never
     * from a timer, so touching the table's mutex here is always safe. */
    std::lock_guard<std::mutex> pool_lk(tx_pool_touch_mutex_);
    const size_t released = ArraySet<Socket>::GetInstance().DrainDeferredRelease();
    if (released >= kDeferredDrainReportThreshold) {
        UBS_VLOG_DEBUG("deferred release drained %zu sockets in one round\n", released);
    }
}

void TxCqePoller::Stop()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    shutdown_.store(true, std::memory_order_release);
    bool expected = true;
    if (!started_.compare_exchange_strong(expected, false)) {
        return;
    }

    stopped_ = true;

    /* Deregister wake_fd_ first (before timer_fd_) so that any in-flight
     * NotifyPosted call observes stopped_=true and returns without writing
     * the now-closed fd. Order matters for the close-race window. */
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    if (wake_fd_ >= 0) {
        runner.DelEpollEvent(wake_fd_);
        LibcApi::close(wake_fd_);
        wake_fd_ = -1;
    }

    if (timer_fd_ >= 0) {
        runner.DelEpollEvent(timer_fd_);
        LibcApi::close(timer_fd_);
        timer_fd_ = -1;
    }

    {
        Locker lock(mutex_);
        sockets_.clear();
        orphan_main_umqs_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(active_mutex_);
        active_.clear();
    }
    /* active_cache_ / active_dirty_ are intentionally NOT touched here: the
     * runner thread may still be mid-sweep iterating active_cache_ (it is
     * stopped only later in the uninit sequence), so clearing it would race.
     * Start() resets both before re-registering the timer. */
    /* Stop being notified before the reaper goes away; ReleaseAll drains the rest at exit. */
    ArraySet<Socket>::GetInstance().SetDeferredNotifier(nullptr, nullptr);
    /* Join the reaper; it drains the retire list before exiting (see StopReaper). */
    StopReaper();

    if (mutex_ != nullptr) {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
    }
}

void TxCqePoller::NotifyInflight() noexcept
{
    any_inflight_.store(true, std::memory_order_release);
}

void TxCqePoller::MarkActive(Socket *sock) noexcept
{
    if (sock == nullptr) {
        return;
    }
    /* Dekker with RetainOrDrop: the caller has just made this socket need a
     * sweep (posted a WR / queued a READ / armed a deadline); the fence
     * orders that store before the flag load below, and RetainOrDrop fences
     * between its flag clear and its re-check — so either we see the flag
     * cleared (and enqueue) or the sweep sees our work (and retains). */
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (sock->tx_poller_active_.load(std::memory_order_relaxed)) {
        return; /* already in the active set (common case: 2nd+ WR on a busy link) */
    }
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }
    if (sock->tx_poller_active_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    SocketPtr ref(sock);
    std::lock_guard<std::mutex> lk(active_mutex_);
    active_.push_back(std::move(ref));
    active_dirty_.store(true, std::memory_order_release);
    MaybeSwitchToFast();
}

size_t TxCqePoller::ActiveCount() const noexcept
{
    std::lock_guard<std::mutex> lk(active_mutex_);
    return active_.size();
}

void TxCqePoller::SnapshotActive(std::vector<SocketPtr> &out) noexcept
{
    std::lock_guard<std::mutex> lk(active_mutex_);
    out = active_;
}

void TxCqePoller::RebuildActive(std::vector<SocketPtr> &survivors, size_t snapshot_size) noexcept
{
    std::lock_guard<std::mutex> lk(active_mutex_);
    /* Producers only append, so active_[0, snapshot_size) is exactly what we
     * snapshotted; everything after it arrived during the sweep and is kept. */
    const bool droppedAny = survivors.size() < snapshot_size;
    if (active_.size() > snapshot_size) {
        survivors.insert(survivors.end(), std::make_move_iterator(active_.begin() + snapshot_size),
                         std::make_move_iterator(active_.end()));
    }
    active_.swap(survivors);
    /* Re-sync the snapshot cache when the sweep shrank the active set, or
     * when a producer appended mid-sweep (active_dirty_ set: the arrival was
     * merged above but is not in the cache). Without this, dropped sockets
     * would linger in active_cache_ until the next MarkActive: every tick
     * would re-walk them, and their stale SocketPtr refs would delay
     * RetireSocket's SoleRef() close up to the retire deadline. Running
     * under active_mutex_ makes this a full snapshot, so clearing
     * active_dirty_ here is race-free against MarkActive's set. */
    if (droppedAny || active_dirty_.load(std::memory_order_relaxed)) {
        active_cache_ = active_;
        active_dirty_.store(false, std::memory_order_release);
    }
}

static bool SocketNeedsSweep(const SocketPtr &sock, SocketBase *sockBase) noexcept
{
    if (sockBase != nullptr) {
        DataTxOps *txOps = sockBase->GetTxOps();
        if (txOps != nullptr &&
            txOps->tx_queue_avail_num_.load(std::memory_order_acquire) != GlobalSetting::UBS_TX_DEPTH) {
            return true; /* in-flight TX WRs: CQEs to reap */
        }
    }
    return UbsBigdata::NeedsPollerAttention(sock); /* pending/posted READs, deadline pins/ctxs */
}

void TxCqePoller::RetainOrDrop(const SocketPtr &sock, SocketBase *sockBase, std::vector<SocketPtr> &survivors) noexcept
{
    if (sock->tx_poller_slot_.load(std::memory_order_acquire) < 0) {
        /* Deregistered (DelSocket ran): nobody polls it any more, drop. */
        sock->tx_poller_active_.store(false, std::memory_order_release);
        return;
    }
    if (SocketNeedsSweep(sock, sockBase)) {
        survivors.push_back(sock);
        return;
    }
    /* Looks idle: clear the flag, fence, re-check (see MarkActive). */
    sock->tx_poller_active_.store(false, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (SocketNeedsSweep(sock, sockBase)) {
        if (!sock->tx_poller_active_.exchange(true, std::memory_order_acq_rel)) {
            survivors.push_back(sock); /* we re-own the entry */
        }
        /* else a producer already re-marked it and appended a fresh entry
         * after our snapshot — RebuildActive keeps that one; drop ours. */
    }
}

bool TxCqePoller::AnyTxInflight() const noexcept
{
    return any_inflight_.load(std::memory_order_acquire);
}

void TxCqePoller::NotifyPosted() noexcept
{
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }
    if (!sleeping_.load(std::memory_order_acquire)) {
        return;
    }
    if (wake_fd_ < 0) {
        return;
    }
    /* Only pay the syscall when the poller is in deep sleep. EBADF is
     * expected during the close race window after Stop() flips stopped_
     * but before NotifyPosted observes it; downgrade to DEBUG. */
    if (eventfd_write(wake_fd_, 1) < 0 && errno != EBADF) {
        UBS_VLOG_ERR("TxCqePoller NotifyPosted eventfd_write failed: %d.\n", errno);
    }
}

void TxCqePoller::AddSocket(const SocketPtr &sock)
{
    if (sock == nullptr) {
        return;
    }

    Locker sLock(mutex_);
    /* O(1) membership via the socket's slot: a valid slot that still points
     * at this very socket means "already registered". (A stale slot — e.g.
     * after Stop() cleared the registry — is treated as not registered.) */
    const int32_t slot = sock->tx_poller_slot_.load(std::memory_order_relaxed);
    if (slot >= 0 && static_cast<size_t>(slot) < sockets_.size() && sockets_[slot].Get() == sock.Get()) {
        return;
    }
    sock->tx_poller_slot_.store(static_cast<int32_t>(sockets_.size()), std::memory_order_release);
    sockets_.push_back(sock);
    sLock.Unlock();
    /* If the socket already has work (a WR posted before registration, e.g.
     * a handshake-time send), put it into the active set now: a sweep that
     * ran in between dropped it as "not registered". */
    auto sockBase = RefDynamicCast<SocketBase>(sock);
    if (SocketNeedsSweep(sock, sockBase.Get())) {
        MarkActive(sock);
    }
}

void TxCqePoller::DelSocket(const SocketPtr &sock)
{
    if (sock == nullptr) {
        return;
    }

    Locker sLock(mutex_);
    /* O(1) swap-remove via the slot; the moved-in socket gets the vacated
     * slot. Deregistration is what makes a later sweep drop this socket
     * from the active set (see RetainOrDrop). */
    const int32_t slot = sock->tx_poller_slot_.load(std::memory_order_relaxed);
    if (slot >= 0 && static_cast<size_t>(slot) < sockets_.size() && sockets_[slot].Get() == sock.Get()) {
        const size_t last = sockets_.size() - 1;
        if (static_cast<size_t>(slot) != last) {
            sockets_[slot] = std::move(sockets_[last]);
            sockets_[slot]->tx_poller_slot_.store(slot, std::memory_order_release);
        }
        sockets_.pop_back();
    }
    sock->tx_poller_slot_.store(-1, std::memory_order_release);
    /* NOTE: no deferral here. EpollCtlDel (our caller) runs from brpc's
     * RemoveConsumer, which happens BEFORE brpc closes the fd — the socket
     * is still live in the ArraySet and brpc may still touch it. Retiring
     * at this point would race the reaper's umq_unbind against brpc's
     * final ubs_poll/close (observed as a client-side segfault). The safe
     * handoff is RetireSocket(), called from ubsocket close() with the ref
     * that OverrideItem hands back — by then brpc is done with the socket
     * and no new refs can be minted. */
}

bool TxCqePoller::RetireSocket(int fd, SocketPtr &&sock)
{
    if (sock == nullptr) {
        return false;
    }
    /* If the reaper is not running (poller stopped/never started), tell the
     * caller to close the fd and drop the ref itself → inline teardown as
     * before; nothing dangles. */
    if (!started_.load(std::memory_order_acquire) || stopped_.load(std::memory_order_acquire)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        retired_.push_back(
            RetiredSocket{std::move(sock), fd, SocketConnHelper::GetTimeMs() + UMQ_DESTROY_FLUSH_TIMEOUT_MS});
    }
    retire_cv_.notify_one();
    return true;
}

bool TxCqePoller::RetireSweep() noexcept
{
    /* Take the WHOLE list for this round. Every retired socket gets a drain
     * step each round — draining is a cheap, non-contending CQE poll, and it
     * is what returns the socket's borrowed jetty-pool node (POOL mode: the
     * node is released by the poll path once tx_outstanding hits 0). The
     * pool is small (default 800, field 100) and establishment borrows from
     * the same pool, so retired sockets must give their nodes back as fast
     * as the hardware flushes — pacing the DRAIN starves establishment
     * (EMLINK → wait queue → brpc timeout). Only the RELEASES (~UmqSocket →
     * umq_destroy, driver ioctls that serialize against create/bind) are
     * capped per round by kRetireBatch to interleave with establishment. */
    std::vector<RetiredSocket> batch;
    {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        if (retired_.empty()) {
            return false;
        }
        batch.swap(retired_);
    }
    const uint64_t now_ms = SocketConnHelper::GetTimeMs();
    /* 退出期（fork#28）：umq 的 poll 入口恒返回 0，drain 不可能推进——继续等
     * 只是把每条链的 deadline 全额烧完，StopReaper 的 join 因此卡住十几秒，
     * 而这个窗口里仍活着的 UbsEventDispatcher 正是三份 core 的现场。退出期
     * 直接视 drain 为已完成（对端要么已死要么将收到 RST，内核会回收一切），
     * 并撤掉每轮释放上限（kRetireBatch 是为了给建链让路——退出期没有建链）。 */
    const bool exiting = GlobalSetting::IsExiting();
    std::vector<RetiredSocket> keep;
    keep.reserve(batch.size());
    size_t released = 0;
    bool progress = false;
    for (auto &entry : batch) {
        /* Pool-touch serialization vs TxSweepOnce (see tx_pool_touch_mutex_). */
        std::lock_guard<std::mutex> pool_lk(tx_pool_touch_mutex_);
        bool done = true;
        auto *umqSock = dynamic_cast<umq::UmqSocket *>(entry.sock.Get());
        if (umqSock != nullptr) {
            done = exiting || umqSock->RetireStep() || now_ms >= entry.deadline_ms;
        }
        /* fd 号与对象同生共死：外部（如 brpc 健康检查）仍持引用时，此处 close(fd)
         * 会让内核立即复用号码，而对象与其数据面槽位未亡——新 accept 在同一槽位上
         * 撞 stale owner 被连环拒绝（issue#32 fd 605/704 现场，同一 owner 钉死槽位
         * 13s+）。FIN 已在 wrapper close 的 shutdown() 发出，押后 close 无对端可见
         * 影响；socket 已从 ArraySet 摘除，引用只减不增，等到独占即可。deadline
         * 兜底：drained 后先空槽再关号（访问器 owner 判空安全），号码复用无害。 */
        bool close_safe = exiting || umqSock == nullptr || entry.sock->SoleRef();
        if (done && !close_safe && now_ms >= entry.deadline_ms && umqSock->RetireDrained()) {
            umqSock->ReleaseDataPlane();
            close_safe = true;
        }
        if (done && close_safe && (released < kRetireBatch || exiting)) {
            ++released;
            progress = true;
            const int fd = entry.fd;
            entry.sock = nullptr; /* release: ~UmqSocket (umq_destroy) runs on this (reaper) thread */
            /* Only now may the kernel recycle the fd number: the UMQ whose
             * umq_ctx was this fd is gone, so no completion can be misrouted
             * to a new link that inherits the number. */
            if (fd >= 0) {
                LibcApi::close(fd);
            }
        } else {
            /* not drained yet, drained but ref-pinned (close deferred), or over cap */
            keep.push_back(std::move(entry));
        }
    }
    if (!keep.empty()) {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        retired_.insert(retired_.begin(), std::make_move_iterator(keep.begin()), std::make_move_iterator(keep.end()));
    }
    /* Second door of the pool race (issue #30 core 8a, found by the
     * fix-branch soak at exactly this line): deferred releases run
     * ~UmqSocket -> UnInitialize -> umq_destroy on this thread, and
     * were the one destroy path left outside tx_pool_touch_mutex_.
     * Same rule as the batch releases above. The other drain sites are
     * DrainDeferred() from ReaperLoop (same thread, same mutex) and
     * ArraySet::ReleaseAll — exit-path only, after Stop() has already
     * refused further sweeps, race-free by ordering. */
    DrainDeferred();
    return progress;
}

size_t TxCqePoller::RetiredCount() const noexcept
{
    std::lock_guard<std::mutex> lk(retire_mutex_);
    return retired_.size();
}

void TxCqePoller::ReaperLoop() noexcept
{
    pthread_setname_np(pthread_self(), "ubs_reaper");
    while (true) {
        bool run_sweep = false;
        {
            std::unique_lock<std::mutex> lk(retire_mutex_);
            /* Wake on: stop, a retired socket, or a parked deferred release. Before
             * issue #49 the wait had no deferred term: in a process where every
             * handshake failed (each failure removes its own socket from the table,
             * so brpc's close never retires anything) the reaper slept forever and
             * no failed socket — nor the umq/id it had already created — was ever
             * released. Explicit wake-ups only: see kDeferredDrainReportThreshold's
             * neighbour in the header for why there is no periodic tick. */
            retire_cv_.wait(lk, [this] {
                return reaper_stop_.load(std::memory_order_acquire) || !retired_.empty() || deferred_signal_;
            });
            if (reaper_stop_.load(std::memory_order_acquire) && retired_.empty()) {
                return;
            }
            deferred_signal_ = false;
            run_sweep = !retired_.empty();
        }
        if (run_sweep) {
            (void)RetireSweep(); /* drains the deferred queue at its end */
        } else {
            DrainDeferred();
        }
        /* Pace: give the per-device verbs path (and everything else) room
         * between rounds while work remains. */
        if (RetiredCount() > 0) {
            usleep(kRetireRoundIntervalUs);
        }
    }
}

void TxCqePoller::StartReaper()
{
    /* Belt and braces: with Start/Stop serialized by lifecycle_mutex_ an old
     * thread here can only be one that already returned; reap it before the
     * assignment so the move-assign never sees a joinable target. */
    if (reaper_thread_.joinable()) {
        reaper_thread_.join();
    }
    reaper_stop_.store(false, std::memory_order_release);
    reaper_thread_ = std::thread([this]() { ReaperLoop(); });
}

void TxCqePoller::StopReaper()
{
    /* Set the flag UNDER retire_mutex_: ReaperLoop reads it inside its wait
     * predicate with the mutex held, so a store made outside the mutex could
     * land between that check and the atomic release-and-block — the notify
     * then finds no waiter and the join below never returns (the lost wake-up
     * behind the reaper UTs deleted in cb5cef8d). RetireSocket and
     * NotifyDeferred already publish under the same mutex. */
    {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        reaper_stop_.store(true, std::memory_order_release);
    }
    retire_cv_.notify_all();
    if (reaper_thread_.joinable()) {
        reaper_thread_.join(); /* ReaperLoop drains retired_ before returning */
    }
    /* Belt and braces: anything still parked (reaper never started) is
     * released here on the caller's thread — the pre-existing inline path. */
    std::vector<RetiredSocket> leftovers;
    {
        std::lock_guard<std::mutex> lk(retire_mutex_);
        leftovers.swap(retired_);
    }
    for (auto &entry : leftovers) {
        std::lock_guard<std::mutex> pool_lk(tx_pool_touch_mutex_); /* vs an in-flight sweep */
        const int fd = entry.fd;
        entry.sock = nullptr; /* inline teardown on the caller's thread */
        if (fd >= 0) {
            LibcApi::close(fd);
        }
    }
    leftovers.clear();
}

void TxCqePoller::DrainTimerFd() noexcept
{
    /* Drain the timer fd so the runner does not re-fire immediately. */
    if (timer_fd_ >= 0) {
        uint64_t expirations = 0;
        ssize_t s = LibcApi::read(timer_fd_, &expirations, sizeof(expirations));
        (void)s;
    }
}

void TxCqePoller::PollAllSockets() noexcept
{
    DrainTimerFd();

    if (stopped_.load(std::memory_order_relaxed)) {
        return;
    }

    /* Pool-touch serialization vs the reaper (issue #41 review): this legacy
     * 1ms-tick path (unified poll OFF — the default) drains TX and touches
     * bigdata state exactly like TxSweepOnce, so it must take the same
     * try-lock. Contention means the reaper is running umq_destroy on the
     * shared pool; skip this tick — the 1ms timer retries. DrainTimerFd()
     * already ran above, so the skipped tick does not leave the timerfd
     * level-set and re-firing in a hot loop. */
    std::unique_lock<std::mutex> pool_lk(tx_pool_touch_mutex_, std::try_to_lock);
    if (!pool_lk.owns_lock()) {
        return;
    }

    /* Only the ACTIVE set is visited (see MarkActive): sockets with in-flight
     * TX WRs or bigdata work. Idle links — the vast majority at scale — are
     * not touched at all, so a round costs O(active), not O(all sockets).
     * active_dirty_ optimization: skip mutex+copy when no MarkActive happened
     * since the last snapshot. Dirty clear is inside active_mutex_ to close
     * the race with MarkActive's dirty set. */
    if (active_dirty_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lk(active_mutex_);
        active_cache_ = active_;
        active_dirty_.store(false, std::memory_order_release);
    }
    const size_t snapshot_size = active_cache_.size();
    std::vector<SocketPtr> survivors;
    survivors.reserve(snapshot_size);

    for (const auto &sock : active_cache_) {
        if (stopped_.load(std::memory_order_relaxed)) {
            break;
        }
        if (sock == nullptr) {
            continue;
        }
        if (sock->tx_poller_slot_.load(std::memory_order_acquire) < 0) {
            /* DelSocket ran (brpc RemoveConsumer; the reaper may already be
             * tearing the UMQ down): do not touch it, just forget it. */
            sock->tx_poller_active_.store(false, std::memory_order_release);
            continue;
        }
        auto sockBase = RefDynamicCast<SocketBase>(sock);
        if (sockBase != nullptr) {
            DataTxOps *txOps = sockBase->GetTxOps();
            if (txOps != nullptr &&
                txOps->tx_queue_avail_num_.load(std::memory_order_relaxed) != GlobalSetting::UBS_TX_DEPTH) {
                /* poll_to_empty: reclaim ALL pending TX CQEs, not just
                 * one batch. PollTx's state-gated third branch only polls
                 * once when tx_queue_avail_num_ > 0, which lets CQEs
                 * accumulate under high throughput and stall the SQ
                 * (status:12). ForceDrainTx loops until empty. */
                txOps->ForceDrainTx(sock.Get());
            }
            UbsBigdata::RetryPendingReadsForSocket(sock);
            UbsBigdata::SweepExpiredForSocket(sock);
        }
        RetainOrDrop(sock, sockBase.Get(), survivors);
    }
    RebuildActive(survivors, snapshot_size);
    MaybeSwitchToSlow();
}

bool TxCqePoller::TxSweepOnce(bool sweep_orphan, bool quick_poll) noexcept
{
    if (stopped_.load(std::memory_order_acquire)) {
        return false;
    }
    std::unique_lock<std::mutex> pool_lk(tx_pool_touch_mutex_, std::try_to_lock);
    if (!pool_lk.owns_lock()) {
        return false; /* another sweeper or the reaper is on the pool; the 100us timer retries */
    }

    /* Only the ACTIVE set is visited (see MarkActive / PollAllSockets).
     * active_dirty_ optimization: skip mutex+copy when no MarkActive happened
     * since the last snapshot. Dirty clear is inside active_mutex_ to close
     * the race with MarkActive's dirty set. */
    if (active_dirty_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lk(active_mutex_);
        active_cache_ = active_;
        active_dirty_.store(false, std::memory_order_release);
    }
    const size_t snapshot_size = active_cache_.size();
    std::vector<SocketPtr> survivors;
    survivors.reserve(snapshot_size);

    bool anyProgress = false;
    for (const auto &sock : active_cache_) {
        if (stopped_.load(std::memory_order_relaxed)) {
            break;
        }
        if (sock == nullptr) {
            continue;
        }
        if (sock->tx_poller_slot_.load(std::memory_order_acquire) < 0) {
            sock->tx_poller_active_.store(false, std::memory_order_release); /* deregistered: forget it */
            continue;
        }
        auto sockBase = RefDynamicCast<SocketBase>(sock);
        if (sockBase == nullptr) {
            RetainOrDrop(sock, nullptr, survivors);
            continue;
        }
        DataTxOps *txOps = sockBase->GetTxOps();
        if (txOps != nullptr) {
            /* Skip sockets with no in-flight WRs — nothing to drain. */
            if (txOps->tx_queue_avail_num_.load(std::memory_order_relaxed) == GlobalSetting::UBS_TX_DEPTH) {
                UbsBigdata::RetryPendingReadsForSocket(sock);
                UbsBigdata::SweepExpiredForSocket(sock);
                RetainOrDrop(sock, sockBase.Get(), survivors);
                continue;
            }
            /* quick_poll=true (RX active loop): QuickPollTx — single-shot
             * CQE reclaim to bound per-socket TX poll latency so RX polling
             * is not blocked by large CQE batches. Remaining CQEs are
             * reclaimed by the TX-only active loop (TX_CQE_TIMER / TX_WAKE)
             * which passes quick_poll=false → ForceDrainTx.
             * quick_poll=false (TX-only active loop): ForceDrainTx — drain
             * all pending CQEs to completion, preventing SQ-full (status:12).
             * Infer progress from tx_queue_avail_num_ delta. */
            uint16_t availBefore = txOps->tx_queue_avail_num_.load(std::memory_order_relaxed);
            if (quick_poll) {
                txOps->QuickPollTx(sock.Get());
            } else {
                txOps->ForceDrainTx(sock.Get());
            }
            uint16_t availAfter = txOps->tx_queue_avail_num_.load(std::memory_order_relaxed);
            if (availAfter > availBefore) {
                anyProgress = true;
            }
        }
        UbsBigdata::RetryPendingReadsForSocket(sock);
        UbsBigdata::SweepExpiredForSocket(sock);
        RetainOrDrop(sock, sockBase.Get(), survivors);
    }
    RebuildActive(survivors, snapshot_size);

    /* POOL mode (flag=ON): sweep registered main umqs for orphan CQEs and
     * to free jetty nodes via UmqTpWaitQueue::WakeUp. This replaces the
     * 1ms TP_TX_TIMER round-robin poll, raising the jetty-release cadence
     * from 1ms to us-level (driven by the ACTIVE loop). Empty registry
     * (SINGLE mode or flag=OFF) is a zero-cost branch.
     * Only called when sweep_orphan=true (from RunUnifiedActiveLoop(main_umq)
     * which also polls RX). The TX-only RunUnifiedActiveLoop() passes
     * sweep_orphan=false to avoid starving RX with orphan polling. */
    if (sweep_orphan && !orphan_main_umqs_.empty()) {
        if (SweepOrphanPools()) {
            anyProgress = true;
        }
    }

    /* When no socket had CQEs to reclaim this round, clear the global
     * in-flight flag so the active loop can enter backoff. Precision is
     * not required for correctness (triple fallback: sleeping_ + eventfd
     * + 100ms timer), only an optimization hint. */
    if (!anyProgress) {
        any_inflight_.store(false, std::memory_order_release);
    }
    MaybeSwitchToSlow();
    return anyProgress;
}

void TxCqePoller::RegisterOrphanSweep(uint64_t main_umqh)
{
    if (main_umqh == UMQ_INVALID_HANDLE) {
        return;
    }
    Locker sLock(mutex_);
    for (const auto &h : orphan_main_umqs_) {
        if (h == main_umqh) {
            return;
        }
    }
    orphan_main_umqs_.push_back(main_umqh);
}

bool TxCqePoller::SweepOrphanPools() noexcept
{
    if (stopped_.load(std::memory_order_acquire)) {
        return false;
    }

    std::vector<uint64_t> mainUmqs;
    {
        Locker sLock(mutex_);
        mainUmqs = orphan_main_umqs_;
    }

    bool anyProgress = false;
    for (const auto &main_umqh : mainUmqs) {
        if (stopped_.load(std::memory_order_relaxed)) {
            break;
        }
        /* Round-robin poll of all jetty nodes via tp_idx=0 (umq dispatches
         * to all nodes when FLAG_TP_HANDLE_IDX is set with tp_idx=0).
         * Mirrors the TP_TX_TIMER branch (umq_tp_tx_epoll_runner_ops.cpp:65-114).
         * PollUmqTxInternal produces freed_jettys -> UmqTpWaitQueue::WakeUp,
         * releasing EMLINK-blocked sockets at us-level cadence. */
        umq_io_option_t poll_option = {
            UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TP_HANDLE_IDX,
            UMQ_IO_TX,
            0, /* tp_idx=0: round-robin all nodes */
        };
        ops_error_code err = ops_error_code::OK;
        umq::UmqTxHelper::PollArgs args(main_umqh, poll_option, err, nullptr);

        int poll_cnt = 0;
        do {
            poll_cnt = umq::UmqTxHelper::PollUmqTx(args, [main_umqh](umq_buf_t *qbuf) {
                /* Error CQE callback: resolve socket via buf_pro->umq_ctx.
                 * Orphan CQEs (socket already destroyed) -> GetItem returns
                 * null -> skip. This is the scavenger path for sockets that
                 * closed with in-flight WRs (FlushTx timeout). */
                auto buf_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
                if (buf_pro == nullptr) {
                    return;
                }
                auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
                auto sock_ref = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
                auto *socket_ptr = sock_ref.Get();
                if (socket_ptr == nullptr) {
                    UBS_VLOG_DEBUG("orphan sweep: socket %d already gone (umq %llu)\n", socket_fd,
                                   static_cast<unsigned long long>(main_umqh));
                    return;
                }
                /* Live socket hit an error CQE: async-close via shutdown(SHUT_RD)
                 * so brpc picks up EOF on the next EPOLLIN. Mirrors TP_TX_TIMER
                 * and TP_TX branches. */
                LibcApi::shutdown(socket_fd, SHUT_RD);
                UBS_VLOG_DEBUG("orphan sweep: closing socket fd=%d in TX CQE error (umq %llu)\n", socket_fd,
                               static_cast<unsigned long long>(main_umqh));
                socket_ptr->State(SOCK_STAT_CLOSE);

                /* CLOS topology: mark all used ports in cooldown on fatal CQE. */
                auto *umq_sock = static_cast<umq::UmqSocket *>(socket_ptr);
                if (umq_sock->GetTopoType() == UMQ_TOPO_TYPE_CLOS) {
                    if (qbuf->status == UMQ_BUF_LOC_LEN_ERR || qbuf->status == UMQ_BUF_LOC_ACCESS_ERR ||
                        qbuf->status == UMQ_BUF_ACK_TIMEOUT_ERR || qbuf->status == UMQ_FAKE_BUF_FC_ERR ||
                        qbuf->status == UMQ_FAKE_BUF_FC_ERR_FATAL) {
                        auto [ports, ports_num] = umq_sock->GetUsedPorts();
                        for (std::size_t i = 0; i < ports_num; ++i) {
                            UBS_VLOG_WARN("orphan sweep: port down (chip=%u,die=%u,port=%u)\n", ports[i].bs.chip_id,
                                          ports[i].bs.die_id, ports[i].bs.port_idx);
                            PortCooldownManager::MarkPortInCooldown(ports[i]);
                        }
                    }
                }
            });
            if (poll_cnt > 0) {
                anyProgress = true;
            }
        } while (poll_cnt > 0 && err == ops_error_code::OK);
    }
    return anyProgress;
}

void TxCqePoller::MaybeSwitchToSlow() noexcept
{
    if (!GlobalSetting::UBS_TX_POLLER_SLOW_SWITCH_ENABLED) {
 	    return;
 	}
    if (!timer_fast_.load(std::memory_order_acquire) || timer_fd_ < 0) {
        return;
    }
    /* Lock-free pre-filters: only the sweep thread writes active_cache_, so
     * reading it here without active_mutex_ is safe, and the ACTIVE spin
     * (up to 64 TxSweepOnce rounds per wake) stays free of lock traffic. */
    if (active_dirty_.load(std::memory_order_acquire) || !active_cache_.empty()) {
        return;
    }
    const uint32_t fallbackMs = GlobalSetting::UBS_TX_POLLER_FALLBACK_MS;
    /* fallbackMs==0 would disarm the timerfd (it_value==0) and kill the
     * fallback cadence entirely — treat it as "adaptive switching off". */
    if (fallbackMs == 0 || ActiveCount() != 0) {
        return;
    }
    struct itimerspec slow{};
    slow.it_value.tv_sec = static_cast<long>(fallbackMs / 1000);
    slow.it_value.tv_nsec = static_cast<long>(fallbackMs % 1000) * 1000L * 1000L;
    slow.it_interval = slow.it_value;
    if (timerfd_settime(timer_fd_, 0, &slow, nullptr) != 0) {
        return; /* keep FAST (and timer_fast_ consistent); the next sweep retries */
    }
    timer_fast_.store(false, std::memory_order_release);
    /* Close the check-then-act race with MarkActive: a post that completed
     * between the ActiveCount() check and the flag store above observed
     * timer_fast_==true in MaybeSwitchToFast and skipped the re-arm. Without
     * this re-check the timer would stay SLOW for that socket's whole active
     * lifetime (MarkActive early-returns while tx_poller_active_ is set),
     * degrading CQE reaping to the SLOW cadence. */
    if (ActiveCount() != 0) {
        MaybeSwitchToFast();
    }
}

void TxCqePoller::MaybeSwitchToFast() noexcept
{
    if (timer_fast_.load(std::memory_order_acquire) || timer_fd_ < 0) {
        return;
    }
    struct itimerspec fast{};
    fast.it_value.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    fast.it_interval.tv_nsec = static_cast<long>(GlobalSetting::UBS_TX_POLLER_FAST_PERIOD_US) * 1000L;
    if (timerfd_settime(timer_fd_, 0, &fast, nullptr) != 0) {
        return; /* stay SLOW (timer_fast_ consistent); the next MarkActive retries */
    }
    timer_fast_.store(true, std::memory_order_release);
}

} // namespace ubs
} // namespace ock
