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
#ifndef UBS_COMM_UMQ_SHARE_JFR_EPOLL_RUNNER_OPS_H
#define UBS_COMM_UMQ_SHARE_JFR_EPOLL_RUNNER_OPS_H

#include "common/ubsocket_flash_dynamic_bitset.h"
#include "common/ubsocket_spsc_ring_queue.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "umq_types.h"

#include <map>
#include <memory>
#include <unordered_set>

namespace ock {
namespace ubs {
namespace umq {

constexpr uint32_t TRACED_SOCKET_FDS_RESERVE_SIZE = 1024;
constexpr uint32_t SOCKED_BIT_SET_INIT_SIZE = 4;
struct UmqPollTraceTime {
    uint64_t umq_poll_start_timestamp_;
    uint64_t umq_poll_end_timestamp_;
    uint64_t umq_rearm_start_timestamp_;
    uint64_t umq_rearm_end_timestamp_;
    uint64_t umq_alloc_start_timestamp_;
    uint64_t umq_alloc_end_timestamp_;
    uint64_t umq_post_start_timestamp_;
    uint64_t umq_post_end_timestamp_;
    uint64_t process_share_jfr_end_timestamp_;
};

class UmqShareJfrEpollRunnerOps : public EpollRunnerOps {
public:
    struct ShareJfrExtContext : public ExtContext {
        bool should_rearm_interrupt = true;
    };

    UmqShareJfrEpollRunnerOps()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        // for avoid expand memory
        traced_socket_fds_.reserve(TRACED_SOCKET_FDS_RESERVE_SIZE);
        for (int i = 0; i < SOCKED_BIT_SET_INIT_SIZE; ++i) {
            socket_fd_bit_set.Push(new (std::nothrow) FlashDynamicBitSet(ArraySet<Socket>::GetInstance().Capacity()));
        }
    }
    ~UmqShareJfrEpollRunnerOps()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        FlashDynamicBitSet *bitset = nullptr;
        while (socket_fd_bit_set.Pop(bitset)) {
            delete bitset;
        }
    }

    /**
     * @brief process epoll_wait event
     * @param event event to process
     */
    int ProcessOneEvent(const struct epoll_event &event) override;

    int ProcessShareJfrEvent(const struct epoll_event &event, uint64_t main_umq, bool should_rearm_interrupt);

    /* Single bounded round of share-JFR RX polling. Extracted from the
     * do-while body of ProcessShareJfrEvent: polls main_umq once (up to
     * MAX_EPOLL_WAIT_COUNT bufs), refills, sifts events, and traces.
     * Returns true if pollNum > 0 (progress), false if pollNum <= 0 (idle
     * or error). The old `return -1` on pollNum<=0 was dead code because
     * DrainReadyEvents ignores ProcessOneEvent's return value.
     * Per-round state: traced_socket_fd_trace_map_ cleared each call;
     * event_reach_sockets/epoll_fds ClearAll before Sift. Cross-loop
     * state: traced_socket_fds_ accumulates across the whole ACTIVE loop
     * (caller clears it once before the loop).
     * total_polled: optional accumulator for the number of data bufs
     * actually polled this round (ioPollNum, flow-control bufs excluded).
     * When non-null, *total_polled is incremented by ioPollNum. */
    bool RxPollQuantum(uint64_t main_umq, int *total_polled = nullptr);

    /* Unified active loop: interleaves RxPollQuantum and TxSweepOnce with
     * a two-state (ACTIVE + DEEP_IDLE) backoff gradient. Used by the
     * SHARE_JFR event branch when UBS_TX_UNIFIED_POLL_ENABLED=ON.
     * When UBS_SHARE_JFR_LOOP_POLL_ENABLED=false, degenerates to a single
     * round (no spin) and returns immediately.
     * total_polled (optional) accumulates ioPollNum across all RxPollQuantum
     * calls for the high-water print. */
    void RunUnifiedActiveLoop(uint64_t main_umq, int *total_polled = nullptr);

    /* Tx-only overload: used by TX_WAKE and TX_CQE_TIMER branches. Runs
     * only TxSweepOnce (no RX poll) to avoid the multi-main-umq parameter
     * issue — those branches do not have a main_umq to pass. */
    void RunUnifiedActiveLoop();

    /* Poll RX (RxPollQuantum) for all registered main umqs. Used by the
     * tx-only RunUnifiedActiveLoop to drain CQEs that miss the SHARE_JFR
     * interrupt (ack-rearm race in POOL share_transport). */
    bool PollMainUmqRxAll();

    int ProcessMainUmqRearm(uint64_t main_umq);

    void SiftSocketEventsWithUmqBuffers(umq_buf_t **buf, int count, FlashDynamicBitSet &socket_fds,
                                        std::vector<SocketPtr> &socket_ptrs);

    int InsertJfrMainUmq(int share_jfr_fd, uint64_t main_umq, int epoll_fd, struct epoll_event *shared_jfr_event)
    {
        Locker sLock(mutex_);
        if (UNLIKELY(jfr_main_umq_.count(share_jfr_fd) == 0)) {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, share_jfr_fd, shared_jfr_event) < 0) {
                return -1;
            }
            jfr_main_umq_.emplace(share_jfr_fd, main_umq);
            jfr_main_umq_list_.push_back(main_umq);
        }

        return 0;
    }

    int AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx) override;

    int DelEpollEvent(int epoll_fd, int fd) override;

private:
    void HandleSubUmqPollBuffers(Socket *socketObject, umq_buf_t **buf, int pollNum);
    uint32_t event_num_{0};
    std::unordered_map<int, uint64_t> jfr_main_umq_{};
    std::vector<uint64_t> jfr_main_umq_list_{};
    u_mutex_t *mutex_{nullptr};
    UmqPollTraceTime traceTime_{};
    std::unordered_set<int> traced_socket_fds_{};
    SPSCRingQueue<FlashDynamicBitSet *> socket_fd_bit_set{SOCKED_BIT_SET_INIT_SIZE};
    // Members instead of thread_local: the processing context may migrate
    // between worker threads under direct dispatch. Lazily initialized from
    // the socket_fd_bit_set pool.
    std::unique_ptr<FlashDynamicBitSet> event_reach_sockets_{};
    std::unique_ptr<FlashDynamicBitSet> event_reach_epoll_fds_{};
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_EPOLL_RUNNER_H