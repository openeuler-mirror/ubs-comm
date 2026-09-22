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

#include "umq_share_jfr_epoll_runner_ops.h"
#include <atomic>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_link_trace.h"
#include "core/ubsocket_bigdata.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "profiling/probe/probe_manager.h"
#include "profiling/trace/ubs_pkt_trace.h"
#include "umq_backend.h"
#include "umq_data_rx_ops.h"
#include "umq_data_tx_ops.h"
#include "umq_errno.h"
#include "umq_errno_converter.h"
#include "umq_pro_types.h"
#include "umq_setting.h"
#include "umq_socket.h"
#include "under_api/dl_libc_api.h"

#include <sched.h>

namespace ock {
namespace ubs {
namespace umq {

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessOneEvent(const struct epoll_event &event)
{
    /* Woken from epoll_wait: we are no longer in DEEP_IDLE. Clear
     * sleeping_ so cross-thread NotifyPosted skips the eventfd_write
     * syscall (no point waking a thread that is already awake). */
    if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
        TxCqePoller::Instance().SetSleeping(false, std::memory_order_release);
    }

    uint64_t main_umq = 0;
    Socket *socket_object = nullptr;
    RunnerEventData event_data{};

    event_data.u64 = event.data.u64;
    if (event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR ||
        event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR_RETRY) {
        Locker slock(mutex_);
        auto pos = jfr_main_umq_.find(static_cast<int>(event_data.event_data.data));
        if (pos != jfr_main_umq_.end()) {
            main_umq = pos->second;
        }
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_SUB_UMQ_RX) {
        socket_object = reinterpret_cast<Socket *>(static_cast<ptrdiff_t>(event_data.event_data.data));
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_TX_CQE_TIMER) {
        /* flag=OFF: PollAllSockets on every 1ms tick (current behavior).
         * flag=ON: RunUnifiedActiveLoop() (Tx-only) — spins with backoff
         *          until idle, then returns to DrainReadyEvents. The 100ms
         *          fallback timer re-fires this if still idle.
         * Both paths must drain the timer_fd first, otherwise epoll_wait
         * immediately refires (level-triggered) and starves RX. */
        if (!GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().PollAllSockets();
        } else {
            TxCqePoller::Instance().DrainTimerFd();
            RunUnifiedActiveLoop();
        }
        return 0;
    } else if (event_data.event_data.type == RUNNER_EVENT_TYPE_TX_WAKE) {
        /* TX wake eventfd fired: drain it, then run the Tx-only active
         * loop to reclaim CQEs with low latency. */
        if (event_data.event_data.data != 0) {
            uint64_t val = 0;
            LibcApi::read(static_cast<int>(event_data.event_data.data), &val, sizeof(val));
        }
        if (!GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().PollAllSockets();
        } else {
            RunUnifiedActiveLoop();
        }
        return 0;
    } else {
        UBS_VLOG_ERR("async_epoll unknown event:(events:%x, data.type:%lu)\n", event.events,
                     event_data.event_data.type);
    }

    if (main_umq != 0) {
        /* flag=OFF: ProcessShareJfrEvent (legacy do-while, retained for
         *          rollback). flag=ON: rearm + RunUnifiedActiveLoop(main_umq)
         *          which interleaves RX quantum + TX sweep. */
        if (!GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            auto ret = ProcessShareJfrEvent(event, main_umq, event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR);
            traceTime_.process_share_jfr_end_timestamp_ = ubsocket_get_timeNs_compile();
            return ret;
        }
        bool shouldRearm = (event_data.event_data.type == RUNNER_EVENT_TYPE_SHARE_JFR);
        /* [H3] Defer rearm to after RunUnifiedActiveLoop: the doorbell writes
         * inside ProcessMainUmqRearm (umq_get_cq_event + umq_rearm_interrupt)
         * only arm the hardware for FUTURE events and are pure overhead on
         * the current response's critical path. At ms-spaced RPCs the window
         * is safe — no new CQE arrives during the ~us processing window. */
        const uint32_t rx_batch_threshold = GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD;
        const uint64_t rx_poll_start_ns = (rx_batch_threshold > 0) ? ubsocket_get_timeNs() : 0;
        int total_polled = 0;
        traced_socket_fds_.clear();
        RunUnifiedActiveLoop(main_umq, &total_polled);
        if (shouldRearm) {
            ProcessMainUmqRearm(main_umq);
            /* Post-rearm poll: catch CQEs that raced the ack→rearm window. */
            if (RxPollQuantum(main_umq, &total_polled)) {
                RunUnifiedActiveLoop(main_umq, &total_polled);
            }
        }
        if (rx_batch_threshold > 0 && total_polled >= static_cast<int>(rx_batch_threshold)) {
            const uint64_t elapsed_us = (ubsocket_get_timeNs() - rx_poll_start_ns) / 1000;
            UBS_VLOG_DEBUG("RNR RX high-water batch: main_umq: %llu, packets: %d, threshold: %u, "
                           "elapsed_us: %llu\n",
                           static_cast<unsigned long long>(main_umq), total_polled, rx_batch_threshold,
                           static_cast<unsigned long long>(elapsed_us));
        }
        traceTime_.process_share_jfr_end_timestamp_ = ubsocket_get_timeNs_compile();
        return 0;
    }

    if (socket_object == nullptr) {
        return 0;
    }

    umq_buf_t *buf[POLL_BATCH_MAX];
    auto umqSock = dynamic_cast<UmqSocket *>(socket_object);
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int pollNum = UmqApi::umq_poll(umqSock->UmqHandle(), &poll_option, buf, POLL_BATCH_MAX);
    if (UNLIKELY(pollNum <= 0)) {
        if (UNLIKELY(pollNum < 0)) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for sub umq RX, local umq: %llu, "
                         "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(umqSock->UmqHandle()), pollNum, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
        }
        if (umqSock->GetRxOps()->RearmRxInterrupt() < 0) {
            UBS_VLOG_ERR("Rearm sub umq failed, socket fd:%d\n", socket_object->raw_socket_);
        }
        return -1;
    }
    HandleSubUmqPollBuffers(socket_object, buf, pollNum);

    /* flag=ON: interleave a TX sweep after sub-umq RX so TX CQEs posted
     * by DoReadOffer/RetryPendingReads on this thread get reaped promptly. */
    if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
        TxCqePoller::Instance().TxSweepOnce(true, true);
    }
    return 0;
}

void UmqShareJfrEpollRunnerOps::HandleSubUmqPollBuffers(Socket *socketObject, umq_buf_t **buf, int pollNum)
{
    auto umqSock = dynamic_cast<UmqSocket *>(socketObject);
    for (int i = 0; i < pollNum; ++i) {
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE) {
                auto rxOps = umqSock->GetRxOps(); /* 去虚化后 UmqRxOps == DataRxOps，无需 dynamic_cast */
                rxOps->HandleErrorRxCqe(buf[i]);
            } else {
                auto txOps = dynamic_cast<UmqTxOps *>(umqSock->GetTxOps());
                txOps->WakeUpTx(socketObject);
            }
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
        }
    }
}

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq,
                                                                  bool should_rearm_interrupt)
{
    int ret = 0;
    const uint32_t rx_batch_threshold = GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD;
    const uint64_t rx_poll_start_ns = (rx_batch_threshold > 0) ? ubsocket_get_timeNs() : 0;
    int total_polled = 0;

    traced_socket_fds_.clear();
    do {
        if (!RxPollQuantum(main_umq, &total_polled)) {
            ret = -1;
            goto rx_poll_done;
        }
    } while (GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED);

    // Rearm the interrupt after poll processing
do_rearm:
    traceTime_.umq_rearm_start_timestamp_ = ubsocket_get_timeNs_compile();
    if (should_rearm_interrupt && UNLIKELY(ProcessMainUmqRearm(main_umq) < 0)) {
        ret = -1;
    }
    traceTime_.umq_rearm_end_timestamp_ = ubsocket_get_timeNs_compile();
    /* Post-rearm poll: catch CQEs that raced the ack→rearm window. */
    if (should_rearm_interrupt && ret == 0) {
        if (!RxPollQuantum(main_umq, &total_polled)) {
            ret = -1;
            goto rx_poll_done;
        }
    }
    for (int trace_socket_fd : traced_socket_fds_) {
        auto socket_ptr = ArraySet<Socket>::GetInstance().GetItem(trace_socket_fd);
        if (socket_ptr.Get() == nullptr) {
            continue;
        }
    }

rx_poll_done:
    if (rx_batch_threshold > 0 && total_polled >= static_cast<int>(rx_batch_threshold)) {
        const uint64_t elapsed_us = (ubsocket_get_timeNs() - rx_poll_start_ns) / 1000;
        UBS_VLOG_DEBUG("RNR RX high-water batch: main_umq: %llu, packets: %d, threshold: %u, elapsed_us: %llu\n",
                       static_cast<unsigned long long>(main_umq), total_polled, rx_batch_threshold,
                       static_cast<unsigned long long>(elapsed_us));
    }
    return ret;
}

bool UmqShareJfrEpollRunnerOps::RxPollQuantum(uint64_t main_umq, int *total_polled)
{
    if (UNLIKELY(event_reach_sockets_.get() == nullptr)) {
        event_reach_sockets_.reset(new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        if (UNLIKELY(event_reach_sockets_.get() == nullptr)) {
            UBS_VLOG_ERR("allocate memory for FlashDynamicBitSet failed.\n");
            return false;
        }
    }
    if (UNLIKELY(event_reach_epoll_fds_.get() == nullptr)) {
        event_reach_epoll_fds_.reset(new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        if (UNLIKELY(event_reach_epoll_fds_.get() == nullptr)) {
            UBS_VLOG_ERR("allocate memory for FlashDynamicBitSet failed.\n");
            return false;
        }
    }

    traced_socket_fds_.clear();
    umq_buf_t *buf[MAX_EPOLL_WAIT_COUNT];
    traceTime_.umq_poll_start_timestamp_ = ubsocket_get_timeNs_compile();
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                   traceTime_.umq_poll_start_timestamp_};
    auto pollNum = UmqApi::umq_poll(main_umq, &poll_option, buf, MAX_EPOLL_WAIT_COUNT);
    traceTime_.umq_poll_end_timestamp_ = ubsocket_get_timeNs_compile();
    if (UNLIKELY(pollNum < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, pollNum, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for share jfr RX, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), pollNum, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, pollNum), savedErrno);
        return false;
    }
    if (UNLIKELY(pollNum == 0)) {
        return false;
    }
    // 计算时，排除流控的buffer
    int fcBufCnt = 0;
    for (int i = 0; i < pollNum; ++i) {
        if (buf[i]->status >= UMQ_FAKE_BUF_FC_UPDATE) {
            ++fcBufCnt;
        }
    }

    int ioPollNum = pollNum - fcBufCnt;
    if (total_polled != nullptr) {
        *total_polled += ioPollNum;
    }

    // [H1] Dispatch received data to the host (brpc) BEFORE refilling the RX
    // pool. The refill (umq_buf_alloc + umq_post doorbell) only arms the queue
    // for FUTURE receives; at ms-spaced RPCs the RX queue (depth 1024) never
    // drains, so deferring it by microseconds is safe and shaves the refill
    // latency off the per-message critical path.
    event_reach_sockets_->ClearAll();
    event_reach_epoll_fds_->ClearAll();

    epoll_data_t event_data{};
    std::vector<SocketPtr> socket_ptrs;
    std::vector<AsyncEventPoll *> readable_epoll_fds;
    bool direct_dispatched = false;
    SiftSocketEventsWithUmqBuffers(buf, pollNum, *event_reach_sockets_, socket_ptrs);
    /* All of this batch's RX enqueues are done; order them before the
     * added_epoll_fd_ loads below (pairs with the fence in
     * UmqSocket::SetAddedEpollFd — see the handoff comment there). */
    std::atomic_thread_fence(std::memory_order_seq_cst);
    for (auto &obj : socket_ptrs) {
        auto socket_obj = obj.Get();
        auto *umq_obj = (UmqSocket *)socket_obj;
        umq_obj->NewRxEpollIn();
        auto epoll_fd_obj = (AsyncEventPoll *)(((SocketBase *)socket_obj)->GetAddedEpollFd(event_data));
        if (UNLIKELY(epoll_fd_obj == nullptr)) {
            /* data delivered before the host registered the fd: the ADD
             * path's rescue (SetAddedEpollFd) must pick it up */
            UBS_LINK_TRACE(socket_obj->raw_socket_, "RX_NOTIFY_SKIP", "");
        }
        if (LIKELY(epoll_fd_obj != nullptr)) {
            // 直接投递模式: 只入队不唤醒(NOSIGNAL), 整批投递完统一 flush,
            // 保证统一轮询循环(rearm/poll/post 数据面)不被逐 socket 抢占
            if (TryDirectDispatchEvent(EPOLLIN, event_data, false)) {
                direct_dispatched = true;
                continue;
            }
            if (UNLIKELY(epoll_fd_obj->AddReadableEvent(EPOLLIN, event_data) != 0)) {
                /* 注入环满：这条可读通知丢了，该 socket 要等下一次事件才被拉起。环容量已随 fd 表取，
                 * 走到这里说明同时可读的 socket 超过了环容量——首次与每 4096 次记一条 ERROR（issue #44）。 */
                static std::atomic<uint64_t> ring_full_count{0};
                const uint64_t n = ring_full_count.fetch_add(1, std::memory_order_relaxed);
                if ((n & 0xFFFU) == 0) {
                    UBS_VLOG_ERR("readable event ring full on epoll fd %d, dropped readable notify (data: %lu, "
                                 "total drops: %lu)\n",
                                 epoll_fd_obj->GetEpollFd(), static_cast<unsigned long>(event_data.u64),
                                 static_cast<unsigned long>(n + 1));
                }
            }
            if (!event_reach_epoll_fds_->Test(epoll_fd_obj->GetEpollFd())) {
                readable_epoll_fds.emplace_back(epoll_fd_obj);
                event_reach_epoll_fds_->Set(epoll_fd_obj->GetEpollFd());
            }
        }
    }
    if (direct_dispatched) {
        FlushDirectDispatch();
    }

    for (auto epoll_fd : readable_epoll_fds) {
        epoll_fd->SetReadableEventFd();
    }

    if (ioPollNum != 0) {
        umq_alloc_option_t alloc_option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE | UMQ_ALLOC_FLAG_POOL_TYPE,
                                           sizeof(ock::ubs::Block), UMQ_ALLOC_POOL_RX};
        uint32_t sc_counts[UMQ_SIZE_CLASS_MAX] = {0};
        /* Classify by raw block size (not IOBUF_DIFF-adjusted): UMQ-internal
         * prefill bufs carry data_size=4096 (no headroom), which would be
         * misclassified as SC[1] by the IOBUF_DIFF-adjusted boundary (4064).
         * RX pool only has 4KB blocks (SC[0]); SC[1]+ counts are dropped. */
        UmqSetting::CountRXBufByClass(buf, ioPollNum, sc_counts, UMQ_SIZE_CLASS_MAX);
        for (uint32_t sc = UMQ_RX_POOL_SIZE_CLASS_COUNT; sc < UMQ_SIZE_CLASS_MAX; ++sc) {
            sc_counts[sc] = 0;
        }
        umq_buf_t *sc_lists[UMQ_SIZE_CLASS_MAX] = {nullptr};
        for (uint32_t sc = 0; sc < UmqSetting::GetSizeClassCount(); sc++) {
            if (sc_counts[sc] > 0) {
                sc_lists[sc] = UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSizeByClass(sc), sc_counts[sc],
                                                     UMQ_INVALID_HANDLE, &alloc_option);
            }
        }
        umq_buf_t *rx_buf_list = UmqSetting::MergeBufLists(sc_lists, sc_counts, UMQ_SIZE_CLASS_MAX);
        if (LIKELY(rx_buf_list != nullptr)) {
            umq_buf_t *bad_qbuf = nullptr;
            traceTime_.umq_post_start_timestamp_ = ubsocket_get_timeNs_compile();
            umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION | UMQ_IO_OPTION_FLAG_TAG_TIMESTAMP, UMQ_IO_RX,
                                            UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX,
                                            traceTime_.umq_post_start_timestamp_};
            if (UmqApi::umq_post(main_umq, rx_buf_list, &io_rx_option, &bad_qbuf) != UMQ_SUCCESS) {
                int savedErrno = errno;
                errno = UmqErrnoConverter::Convert(UmqOperation::READV, UMQ_FAIL, savedErrno);
                UBS_VLOG_ERR("[UMQ_API] umq_post() failed for share jfr RX refill, main umq: %llu, "
                             "mapped errno: %d(%s), original errno: %d\n",
                             static_cast<unsigned long long>(main_umq), errno,
                             UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, UMQ_FAIL), savedErrno);
                UmqApi::umq_buf_free(bad_qbuf);
            }
            traceTime_.umq_post_end_timestamp_ = ubsocket_get_timeNs_compile();
        }
    }

    traceTime_.umq_post_end_timestamp_ = ubsocket_get_timeNs_compile();
    return true;
}

void UmqShareJfrEpollRunnerOps::RunUnifiedActiveLoop(uint64_t main_umq, int *total_polled)
{
    if (!GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED) {
        /* Degenerate single round: no spin, return immediately. This lets
         * the runner fall through to DrainReadyEvents and the 100ms
         * fallback timer handles idle. */
        RxPollQuantum(main_umq, total_polled);
        TxCqePoller::Instance().TxSweepOnce(true, true);
        return;
    }

    /* Three-state active loop (design §3.3):
     *   ACTIVE  : bounded spin (64 rounds, 2 idle exit) — same as original
     *   BACKOFF : exponential usleep (10us→200us) when idle but inflight
     *   DEEP_IDLE: exit to epoll_wait with sleeping_=true + recheck
     * The ACTIVE phase is identical to the original code, preserving
     * high-QPS behavior (spin 64 rounds, yield to other events). The
     * BACKOFF phase only triggers on idle exit when inflight WRs are
     * pending, catching CQEs that arrive ~10-50us after post without
     * waiting for the 0~1ms timer. The sleeping_ protocol (design §4.3)
     * lets NotifyPosted's eventfd wake us from epoll_wait with us-level
     * latency when a post happens during DEEP_IDLE. */
    constexpr uint32_t MAX_ACTIVE_ROUNDS = 64;
    constexpr uint32_t MAX_IDLE_ROUNDS = 2;
    constexpr uint32_t BACKOFF_MIN_US = 10;
    const uint32_t BACKOFF_MAX_US = GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US;
    uint32_t backoffUs = BACKOFF_MIN_US;
    const bool activeYield = GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD;

    for (;;) {
        /* ACTIVE: bounded spin — same structure as original. Exits on
         * 2 idle rounds or MAX_ACTIVE_ROUNDS cap. */
        uint32_t idleRounds = 0;
        bool idleExit = false;
        for (uint32_t round = 0; round < MAX_ACTIVE_ROUNDS; ++round) {
            bool progress = RxPollQuantum(main_umq, total_polled);
            progress = TxCqePoller::Instance().TxSweepOnce(true, true) || progress;
            if (progress) {
                idleRounds = 0;
                backoffUs = BACKOFF_MIN_US;
            } else if (++idleRounds >= MAX_IDLE_ROUNDS) {
                idleExit = true;
                break;
            }
            if (activeYield) {
                PollerYield();
            }
        }

        /* If we hit the cap (not idle exit), return to let other events
         * run. The next event re-enters this loop. */
        if (!idleExit) {
            return;
        }

        /* Idle exit: if no inflight WRs, enter DEEP_IDLE. */
        if (!TxCqePoller::Instance().AnyTxInflight()) {
            break;
        }

        /* Inflight WRs pending but no CQE yet: BACKOFF to wait for
         * hardware completion (~10-50us). Catches CQEs without the
         * 0~1ms timer phase latency. */
        if (backoffUs >= BACKOFF_MAX_US) {
            break; /* backoff exhausted → DEEP_IDLE */
        }
        usleep(backoffUs);
        backoffUs <<= 1;
    }

    /* DEEP_IDLE: set sleeping_ + recheck (design §4.3).
     * Recheck AnyTxInflight after store-release to close the lost-wakeup
     * window: if a post raced between our last sweep and sleeping_=true,
     * the recheck sees inflight and clears sleeping_ so NotifyPosted
     * doesn't need to write eventfd. */
    TxCqePoller::Instance().SetSleeping(true, std::memory_order_release);
    if (TxCqePoller::Instance().AnyTxInflight()) {
        TxCqePoller::Instance().SetSleeping(false, std::memory_order_release);
    }
}

bool UmqShareJfrEpollRunnerOps::PollMainUmqRxAll()
{
    /* Read jfr_main_umq_list_ without locking: it is only appended to in
     * InsertJfrMainUmq (under mutex_) and never removed/shrunk during the
     * process lifetime. A stale snapshot (missing a just-added umq) is safe
     * — the next insertion or the next SHARE_JFR event will pick it up. */
    bool any = false;
    for (uint64_t main_umq : jfr_main_umq_list_) {
        if (RxPollQuantum(main_umq)) {
            any = true;
        }
    }
    return any;
}

void UmqShareJfrEpollRunnerOps::RunUnifiedActiveLoop()
{
    /* Tx-only overload: no main_umq available (TX_WAKE / TX_CQE_TIMER
     * branches), so skip RxPollQuantum and only sweep TX.
     * sweep_orphan=true: also poll main UMQ for orphan CQEs. Without this,
     * during warmup RebuildTp, CQEs on stale jetty nodes are never reclaimed
     * (TX-only path is the only poller running when no RX traffic), causing
     * SQ-full (status:12) and packet loss. SweepOrphanPools is bounded
     * (poll-to-empty), so the added latency is minimal. */
    if (!GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED) {
        TxCqePoller::Instance().TxSweepOnce(true);
        /* Poll main umq RX: the 1ms timer fires this path; without RX poll,
         * CQEs that miss the SHARE_JFR interrupt cause multi-second stalls. */
        PollMainUmqRxAll();
        return;
    }

    /* Three-state active loop (design §3.3), tx-only variant. Same
     * ACTIVE→BACKOFF→DEEP_IDLE structure as RunUnifiedActiveLoop(main_umq)
     * but without RxPollQuantum. PollMainUmqRxAll is called during
     * BACKOFF to catch CQEs that missed the SHARE_JFR interrupt. */
    constexpr uint32_t MAX_ACTIVE_ROUNDS = 64;
    constexpr uint32_t MAX_IDLE_ROUNDS = 2;
    constexpr uint32_t BACKOFF_MIN_US = 10;
    const uint32_t BACKOFF_MAX_US = GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US;
    uint32_t backoffUs = BACKOFF_MIN_US;
    const bool activeYield = GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD;

    for (;;) {
        uint32_t idleRounds = 0;
        bool idleExit = false;
        for (uint32_t round = 0; round < MAX_ACTIVE_ROUNDS; ++round) {
            bool progress = TxCqePoller::Instance().TxSweepOnce(true);
            if (progress) {
                idleRounds = 0;
                backoffUs = BACKOFF_MIN_US;
            } else if (++idleRounds >= MAX_IDLE_ROUNDS) {
                idleExit = true;
                break;
            }
            if (activeYield) {
                PollerYield();
            }
        }

        if (!idleExit) {
            return;
        }

        if (!TxCqePoller::Instance().AnyTxInflight()) {
            break;
        }

        if (backoffUs >= BACKOFF_MAX_US) {
            break;
        }
        usleep(backoffUs);
        backoffUs <<= 1;
        /* BACKOFF does NOT poll RX per iteration. Safety chain for CQEs
         * that miss the SHARE_JFR interrupt (ack-rearm race in POOL mode):
         * (1) the RX path's post-rearm poll (ProcessShareJfrEvent, [H3])
         *     covers CQEs racing the ack→rearm window;
         * (2) backoff exhaustion is bounded (~BACKOFF_MAX_US total), after
         *     which the final PollMainUmqRxAll at DEEP_IDLE entry below
         *     catches stragglers;
         * (3) when fully idle, the TX timer re-fires this loop — its period
         *     is 100us while any socket is active, UBS_TX_POLLER_FALLBACK_MS
         *     once the TX active set drains (adaptive SLOW). A lost interrupt
         *     on an otherwise idle link is therefore caught within one SLOW
         *     period worst case. Per-BACKOFF: 5-6 RxPollQuantum → 0. */
    }

    /* DEEP_IDLE: set sleeping_ + recheck (design §4.3). */
    TxCqePoller::Instance().SetSleeping(true, std::memory_order_release);
    if (TxCqePoller::Instance().AnyTxInflight()) {
        TxCqePoller::Instance().SetSleeping(false, std::memory_order_release);
    }
    /* Final RX poll before sleeping to catch stragglers. */
    PollMainUmqRxAll();
}

void UmqShareJfrEpollRunnerOps::SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count,
                                                               FlashDynamicBitSet &socket_fds,
                                                               std::vector<SocketPtr> &socket_ptrs)
{
    SocketPtr socket_ptr{nullptr};
    SocketBasePtr sk_base{nullptr};
    int last_socket_fd = -1;
    // Stage15: per-batch per-socket ForceDrainTx dedup. When a 10ms-style
    // token-bucket burst lands N READ_OFFERs (one per in-flight RPC), all N
    // hit the same socket. The original loop drained TX-to-empty per buf,
    // serializing the RX runner behind synchronous umq_poll cycles. The
    // background TxCqePoller (RunUnifiedActiveLoop) already drains TX
    // asynchronously, so the first drain per (batch, socket) is sufficient
    // to release credits for the entire burst — subsequent calls are no-ops
    // on an already-empty queue. Skipping them keeps the RX runner hot.
    int last_force_drained_fd = -1;
    for (int i = 0; i < count; ++i) {
        // FC fake bufs (UMQ_FAKE_BUF_FC_UPDATE / UMQ_FAKE_BUF_FC_ERR) carry no
        // real data and must not enter the rx queue or any FreeQbufChain path.
        // The credit window was already updated by umq_ub_shared_credit_resp_handle
        // before the fake buf was created. Free them immediately to prevent
        // double-free. For FC_UPDATE, notify the socket to resume writing.
        if (buf[i]->status >= UMQ_FAKE_BUF_FC_UPDATE) {
            if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE && buf[i]->qbuf_ext != nullptr) {
                auto fc_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
                auto fc_fd = static_cast<int>(fc_pro->umq_ctx);
                auto fc_sock = ArraySet<Socket>::GetInstance().GetItem(fc_fd);
                auto fc_base = RefStaticCast<SocketBase>(fc_sock);
                if (fc_base.Get() != nullptr) {
                    fc_base->NotifyWritable();
                }
            }
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }

        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        if (socket_fd != last_socket_fd) {
            socket_ptr = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
            sk_base = RefStaticCast<SocketBase>(socket_ptr);
            last_socket_fd = socket_fd;
        }
        if (UNLIKELY(socket_ptr.Get() == nullptr)) {
            UBS_VLOG_DEBUG("[Debug] async_epoll: socket fd: %d object is null, skipping event processing. \n",
                           socket_fd);
            continue;
        }
        // Probe packets are intercepted before trace/bigdata/AddQbuf — they carry
        // no business data and must not pollute trace state or enter rx queues.
        if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
            Statistics::ProbeManager::GetInstance().HandleReceivedPacket(socket_fd, buf[i]);
            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }
        if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE) {
            // 收到对端流控回复报文，此 socket 对象可写，唤醒写端
            sk_base->NotifyWritable();
        } else if (buf[i]->status == UMQ_FAKE_BUF_FC_ERR || buf[i]->status == UMQ_FAKE_BUF_FC_ERR_FATAL) {
            // 流控报文错误需要透传给具体 socket 对象，主动触发断链
        }

        // bigData control messages (READ_OFFER / READ_DONE / READ_ABORT) carry
        // imm_data bit 20 set (UBS_IMM_BIG_CTRL_BIT). Only packets so marked are
        // dispatched to the bigdata engine; ordinary SMALL_DATA falls through.
        if (buf[i]->status == 0) {
            if (proto::is_big_ctrl(buf_pro->imm_data)) {
                auto *umq_sock_ptr = static_cast<UmqSocket *>(socket_ptr.Get());
                if (socket_ptr->State() == SOCK_STAT_CLOSE ||
                    (umq_sock_ptr != nullptr && umq_sock_ptr->IsRetiring())) {
                    QBUF_LIST_NEXT(buf[i]) = nullptr;
                    UmqApi::umq_buf_free(buf[i]);
                    continue;
                    }
                if (UbsBigdata::HandleRxControl(socket_ptr, buf[i])) {
                    auto *tx_ops = umq_sock_ptr->GetTxOps();
                    if (tx_ops != nullptr) {
                        // Dedup per (batch, socket): only the first big_ctrl on a
                        // given socket in this umq_poll batch triggers a TX poll.
                        // Use QuickPollTx (single-shot) instead of ForceDrainTx
                        // (drain-to-empty) to bound latency in the RX dispatch
                        // path. Background TxCqePoller handles full drain.
                        if (socket_fd != last_force_drained_fd) {
                            tx_ops->QuickPollTx(socket_ptr.Get());
                            last_force_drained_fd = socket_fd;
                        }
                    }
                    continue;
                }
            }
        }

        /* Socket 正在拆链：丢弃普通数据 buffer，不再入 rxQueue。
         * 与 big_ctrl 路径一致，避免 AddQbuf 在 rxQueue 已关闭/已 null 时
         * 产生 EPIPE/-105 错误日志。 */
        {
            auto *umq_sock_ptr = static_cast<UmqSocket *>(socket_ptr.Get());
            if (socket_ptr->State() == SOCK_STAT_CLOSE ||
                (umq_sock_ptr != nullptr && umq_sock_ptr->IsRetiring())) {
                QBUF_LIST_NEXT(buf[i]) = nullptr;
                UmqApi::umq_buf_free(buf[i]);
                continue;
            }
        }

        /* Inline-data CQE dispatch (SMALL_DATA): big_ctrl / FC / probe bufs
         * were all filtered out above, so anything reaching AddQbuf is
         * ordinary received data. SEND_IMM carries the connection-level
         * segment SN in imm.user_data -- stamp it so the offline join can
         * close the RX black box for sub-4KB-segment messages (方案 A):
         * RxCqeData -> delivered(P row) = rxQueue wait + wake + brpc read
         * scheduling. Same clock domain as brpc (CLOCK_MONOTONIC). */
        if (buf_pro->opcode == UMQ_OPC_SEND_IMM) {
            UbsStageTrace(socket_fd, buf_pro->imm.user_data, STAGE_RX_CQE_DATA,
                          UbsPktTraceNowNs());
        }
        if (UNLIKELY((((UmqSocket *)socket_ptr.Get())->AddQbuf(buf[i]) != 0))) {
            UBS_VLOG_DEBUG("async_epoll add qbuf for socket fd: %d failed.\n", socket_fd);
            continue;
        }
        if (!socket_fds.Test(socket_fd)) {
            socket_fds.Set(socket_fd);
            socket_ptrs.emplace_back(socket_ptr);
        }
    }
}

ALWAYS_INLINE int UmqShareJfrEpollRunnerOps::ProcessMainUmqRearm(uint64_t main_umq)
{
    umq_interrupt_option_t option = {
        .flag = UMQ_INTERRUPT_FLAG_IO_DIRECTION | UMQ_INTERRUPT_FLAG_TAG_TIMESTAMP,
        .direction = UMQ_IO_RX,
        .fd_type = UMQ_FD_IO,
        .tag_timestamp = traceTime_.umq_rearm_start_timestamp_,
    };
    auto events_cnt = UmqApi::umq_get_cq_event(main_umq, &option);
    if (UNLIKELY(events_cnt < 0)) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, events_cnt, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed for share jfr RX, main umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(main_umq), events_cnt, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, events_cnt), savedErrno);
        return events_cnt;
    }

    if (LIKELY(events_cnt > 0)) {
        int rearmRet = UmqApi::umq_rearm_interrupt(main_umq, false, &option);
        if (rearmRet < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, rearmRet, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for share jfr RX rearm, "
                         "main umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(main_umq), rearmRet, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, rearmRet), savedErrno);
        }
        event_num_ += events_cnt;
        if (event_num_ >= GET_PER_ACK) {
            UmqApi::umq_ack_interrupt(main_umq, event_num_, &option);
            event_num_ = 0;
        }
    }

    return events_cnt;
}

int UmqShareJfrEpollRunnerOps::AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx)
{
    RunnerEventData runner_data;
    runner_data.u64 = event->data.u64;
    /* TX_CQE_TIMER and TX_WAKE are plain fds (timerfd / eventfd) that only
     * need a direct epoll_ctl ADD — they carry no ShareJfrExtContext and
     * must not fall through to the ShareJfr main-umq registration path. */
    if (runner_data.event_data.type == RUNNER_EVENT_TYPE_TX_CQE_TIMER ||
        runner_data.event_data.type == RUNNER_EVENT_TYPE_TX_WAKE) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event) < 0) {
            UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) tx event (type=%lu) failed: %d : %s\n",
                         static_cast<unsigned long>(runner_data.event_data.type), errno, strerror(errno));
            return UBS_ERROR;
        }
        return UBS_OK;
    }

    ShareJfrExtContext *share_jfr_ctx = dynamic_cast<ShareJfrExtContext *>(ctx);
    if (share_jfr_ctx == nullptr) {
        UBS_VLOG_ERR("Unsupported operation. Check context because context is null.\n");
        return UBS_ERROR;
    }
    if (InsertJfrMainUmq(fd, share_jfr_ctx->umq_handle, epoll_fd, event) < 0) {
        UBS_VLOG_ERR("async_epoll epoll_ctl(ADD) share jfr event failed: %d : %s\n", errno, strerror(errno));
        return UBS_ERROR;
    }

    if (share_jfr_ctx->should_rearm_interrupt) {
        umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
        int ret = UmqApi::umq_rearm_interrupt(ctx->umq_handle, false, &rx_option);
        if (ret < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for share jfr RX, "
                         "main umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(ctx->umq_handle), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, ret), savedErrno);
            return UBS_ERROR;
        }
    }
    return UBS_OK;
}

int UmqShareJfrEpollRunnerOps::DelEpollEvent(int epoll_fd, int fd)
{
    auto ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    if (UNLIKELY(ret < 0)) {
        UBS_VLOG_ERR("async_epoll del event for fd: %d failed: %d : %s\n", fd, errno, strerror(errno));
        return UBS_ERROR;
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock