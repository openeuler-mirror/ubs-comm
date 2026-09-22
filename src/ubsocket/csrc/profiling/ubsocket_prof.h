/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_PROF_H
#define UBS_COMM_UBSOCKET_PROF_H

#include <cstdint>
#include <cstring>
#include <ctime>

#ifdef __cplusplus
extern "C" {
#endif

extern int ubsocket_prof_enabled;
extern uint64_t ubsocket_arm_cpu_freq;

enum ProfilingTPId : uint32_t {
    CORE_CONNECT = 0,
    CORE_ACCEPT,
    CORE_WRITE,
    CORE_READ,
    CORE_READ_EAGAIN,
    CORE_READ_POLL_RX,
    CORE_READ_HANDLE_BUF,
    CORE_READ_RX_DATA_SET,
    CORE_READ_REARM,
    CORE_EPOLL_REARM,
    CORE_EPOLL_POLL_RX,
    CORE_EPOLL_ALLOC_BUF,
    CORE_EPOLL_POST_RX,
    CORE_PROCESS_JRF_END,
    CORE_EPOLL_ENQUEUE,
    CORE_WRITE_POLL_TX,
    CORE_WRITE_POST_SEND,
    CORE_WRITE_BUILD_IOV,
    CORE_WRITE_MEM_COPY,
    CORE_WRITE_UMQ_POST,
    CORE_WRITE_POLL_CQE,
    CORE_WRITE_DO_TX_POLL,
    CORE_WRITE_REARM,
    CORE_WRITE_POLL_TX_FIRST,
    CORE_WRITE_POLL_TX_SECOND,
    CORE_WRITE_POLL_TX_THIRD,
    // 当前为了快速分析CTP性能，brpc复用ubsocket打点和cli查询能力, 点位先放一起。后续有需要考虑解耦开
    BRPC_CLIENT_CALL,
    BRPC_SERIALIZE,
    BRPC_WRITEV,
    BRPC_DESERIALIZE,
    BRPC_READV,
    BRPC_READV_EAGAIN,
    BRPC_SERVER_PROCESS_REQ,
    BRPC_CLIENT_PROCESS_RSP,

    CORE_WRITE_UMQ_POLL,
    CORE_WRITE_ALLOC_TX_BUF,

    CORE_WRITE_POLL_CQE_DECREF,
    CORE_WRITE_POLL_CQE_FREE,

    CORE_READ_RX_DATA_SET_REARM,
    CORE_READ_RX_DATA_SET_RECV,

    // umq函数级打点
    UMQ_BIND_INFO_GET,
    UMQ_BIND,
    UMQ_DEV_ADD,
    UMQ_BUF_ALLOC,
    UMQ_INTERRUPT_FD_GET,
    UMQ_STATE_GET,
    UMQ_REARM_INTERRUPT,
    UMQ_BUF_FREE,
    UMQ_GET_CQ_EVENT,
    UMQ_ACK_INTERRUPT,
    UMQ_DEV_INFO_GET,
    UMQ_GET_ROUTE_LIST,
    UMQ_POLL_WRITE,
    UMQ_POLL_READ,
    UMQ_POST_RX,
    UMQ_CFG_GET,
    UMQ_CREATE,

    /* TX/RX unified poller latency tracepoints.
     * TX_CQE_LATENCY: post READ (umq_post TX) → HandleTxCompletion
     * (TX CQE reclamation) end-to-end latency. The core metric for the
     * unified poller: P99 target < 100us with eventfd wake. */
    TX_CQE_LATENCY_POST_READ,
    TX_CQE_LATENCY_HANDLE_COMPLETION,

    // Native data-plane receive dispatch latency (brpc use_ub_native path):
    //   RX_READ  = poller dispatch entry -> ubs_poll returned data (adopt)
    //   RX_PROC  = ubs_poll returned data -> ProcessNewMessage done (bthread wake)
    BRPC_NATIVE_RX_READ,
    BRPC_NATIVE_RX_PROC,

    // Native data-plane send post latency (ubs_post in DoUbsNativeWrite)
    BRPC_NATIVE_TX_POST,

    // Server service handler pure execution time (PerfTestServiceImpl::Test)
    BRPC_SERVER_SERVICE,

    // Full wire-to-wire gap measurements for ub_native path:
    //   CLI_PRE_POST    = client CallMethod entry -> ubs_post start
    //                     (total client pre-send overhead: serialize + IssueRPC + Write + StartWrite)
    //   SRV_DISPATCH_SVC = server data arrived (OnUbNativeMessages) -> service starts (Test entry)
    //                     (dispatch + ProcessNewMessage + bthread scheduling + deserialize)
    //   SRV_SVC_TO_POST  = server service done (Test exit) -> ubs_post start
    //                     (done->Run + SendRpcResponse + serialize response + Write + StartWrite)
    //   SRV_SVC_DONE_STAMP = stamp slot for service-done time (not a prof histogram entry)
    BRPC_CLI_PRE_POST,
    BRPC_SRV_DISPATCH_SVC,
    BRPC_SRV_SVC_TO_POST,
    BRPC_SRV_SVC_DONE_STAMP,

    // Pure bthread scheduling delay: ubs_poll return → ProcessInputMessage entry
    BRPC_BTHREAD_SCHED,

    /* =========================================================================
     * ub_native / bigdata adaptive send-receive flow overhead tracepoints.
     * Maps 1:1 to the stages in docs/ubsocket/UBSOCKET-BRPC-UB-NATIVE-FLOW.ch.md.
     * Safe on poller/RX threads (thread_local storage, no locks).
     * ========================================================================= */

    /* Send path (TrySenderPost): route small/large segments, build bufs, submit */
    UBS_NATIVE_TRY_SENDER_POST,
    UBS_NATIVE_HANDLE_SMALL_SEGMENT,
    UBS_NATIVE_BUILD_SMALL_DATA,
    UBS_NATIVE_HANDLE_COALESCED_SMALL_SEGMENT,
    UBS_NATIVE_BUILD_COALESCED_SMALL_DATA,
    UBS_NATIVE_HANDLE_LARGE_SEGMENT,
    UBS_NATIVE_FLUSH_PENDING_OFFER,
    UBS_NATIVE_UMQ_POST_SEND,

    /* TX CQE completion (HandleTxCompletion): READ / BIG_CTRL / SEND branches */
    UBS_NATIVE_HANDLE_TX_COMPLETION,
    UBS_NATIVE_TX_CQE_READ,
    UBS_NATIVE_TX_CQE_CTRL,
    UBS_NATIVE_TX_CQE_SEND,
    UBS_NATIVE_FINALIZE_IO,
    UBS_NATIVE_DELIVER_TO_RX_QUEUE,
    UBS_NATIVE_DRAIN_DEFERRED_CTRL,
    UBS_NATIVE_RETRY_PENDING_READS,

    /* RX path (HandleRxControl / DoReadOffer): parse offer, import mempool, post READ WRs */
    UBS_NATIVE_HANDLE_RX_CONTROL,
    UBS_NATIVE_DO_READ_OFFER,
    UBS_NATIVE_PARSE_READ_OFFER,
    UBS_NATIVE_MEMPOOL_IMPORT,
    UBS_NATIVE_READ_WR_ALLOC,
    UBS_NATIVE_CONFIGURE_ORDERED_READ,
    UBS_NATIVE_UMQ_POST_READ,
    UBS_NATIVE_SEND_SIMPLE_CTRL,

    /* End-to-end p99 breakdown tracepoints (added to localise p99=215us long-tail).
     * UBS_NATIVE_BRPC_QUEUE: Channel::CallMethod entry → ubs_post start
     *                       (= brpc-side pre-send overhead incl. bthread queue,
     *                        IssueRPC, serialize, Write, StartWrite).
     * UBS_NATIVE_NET_RTT:    sender ubs_post end → receiver DoUbsNativeRead entry
     *                       (= wire transport latency, the missing p99 gap).
     * UBS_NATIVE_SRV_DISPATCH: server ubs_post end → ProcessInputMessage entry
     *                       (= server-side wake + bthread scheduling).
     * UBS_NATIVE_FULL_RPC:   Channel::CallMethod entry → final done.Run entry
     *                       (= true end-to-end RPC wall-time, used as p99 anchor). */
    UBS_NATIVE_BRPC_QUEUE,
    UBS_NATIVE_NET_RTT,
    UBS_NATIVE_SRV_DISPATCH,
    UBS_NATIVE_FULL_RPC,

    /* FinalizeIo sub-stage tracepoints (added to localise p99=13.6us long-tail).
     * Break the 8.3us avg / 13.6us p99 of UBS_NATIVE_FINALIZE_IO into the
     * individual segments so we can see which part owns the long-tail:
     *   FINALIZE_LINK           - LinkReadQbufsInOrder chain walk
     *   FINALIZE_STATE_LOCK     - state->mutex + active_io.erase + destroying check
     *   FINALIZE_ARRAYSET_GET   - ArraySet<Socket>::GetItem(fd) for DeliverToRxQueue
     *   FINALIZE_DELIVER_ENQ    - RX-enqueue stripe lock + rxQueue->Enqueue (incl. OOO)
     *   FINALIZE_DELIVER_WAKE   - NotifyReadable (eventfd_write / epoll dispatch)
     *   FINALIZE_DONE_ALLOC     - SendSimpleCtrl umq_buf_alloc
     *   FINALIZE_DONE_POST      - SendSimpleCtrl umq_post doorbell */
    UBS_NATIVE_FINALIZE_LINK,
    UBS_NATIVE_FINALIZE_STATE_LOCK,
    UBS_NATIVE_FINALIZE_ARRAYSET_GET,
    UBS_NATIVE_FINALIZE_DELIVER_ENQ,
    UBS_NATIVE_FINALIZE_DELIVER_WAKE,
    UBS_NATIVE_FINALIZE_DONE_ALLOC,
    UBS_NATIVE_FINALIZE_DONE_POST,

    /* =========================================================================
     * DIAG tracepoints: gen-check overhead breakdown (added to localise the
     * +5us P99 regression in CORE_WRITE_DO_TX_POLL observed when gen-check
     * feature was merged).
     *   SWEEP_FOR_SOCK     - full SweepExpiredForSocket wall-time (4 calls/
     *                        socket per poller tick). The "tax" we pay even
     *                        when the feature is off.
     *   SWEEP_FASTPATH     - time spent in the fast-path branch only (feature
     *                        off OR zero-deadline counters). Should be ~30ns.
     *   SWEEP_FULL         - time spent in the full sweep branch (mutex lock
     *                        + container iteration). Non-zero only when
     *                        effective_timeout != 0.
     *   PINNED_SEAL_ONLY   - SealOnly wall-time incl. create_ns timestamp +
     *                        PinnedEntry construction. Hot path on every READ.
     *   READ_GEN_FETCH_ADD - g_read_gen.fetch_add(1) in TrySenderPost. */
    UBS_NATIVE_SWEEP_FOR_SOCK,
    UBS_NATIVE_SWEEP_FASTPATH,
    UBS_NATIVE_SWEEP_FULL,
    UBS_NATIVE_PINNED_SEAL_ONLY,
    UBS_NATIVE_READ_GEN_FETCH_ADD,
    UBS_NATIVE_RETRY_READS_FOR_SOCK,

    // count the number of ProfilingTPId
    UBSOCKET_PROF_COUNT,
};

typedef struct {
    uint32_t tracepoint_count;  /* how many trace points to be recorded */
    int32_t enable_dump;        /* dump to file or not */
    const char *dump_file_path; /* dump file path */
    uint16_t dump_interval_min; /* dump interval in min */
} ubsocket_prof_option_t;

int ubsocket_prof_init(ubsocket_prof_option_t *option);

int ubsocket_prof_uninit();

/* 高性能打点接口（默认）- 使用 thread_local，无锁写入 */
int ubsocket_prof_record(uint32_t tracepoint_id, const char *tracepoint_name, uint64_t timestamp, bool good);

int ubsocket_prof_combind(char **out_buf);

void ubsocket_prof_reset();

/*
 * ubsocket_get_timeNs_compile
 * 始终返回真实时间戳, 供 SplitTrace 等模块使用 (运行时开关控制)
 *
 * ubsocket_get_timeNs
 * 受到 ubsocket_prof_enabled 控制，只用于 高性能打点宏
 * 主流程中不调用该函数
 */
#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
#define ubsocket_get_timeNs_compile()                              \
    ({                                                             \
        uint64_t _timeValue = 0;                                   \
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(_timeValue)); \
        _timeValue * 1000L / ubsocket_arm_cpu_freq;                \
    })
#else
#define ubsocket_get_timeNs_compile()                                                 \
    ({                                                                                \
        uint64_t _result = 0;                                                         \
        do {                                                                          \
            struct timespec _tpDelay = {0, 0};                                        \
            clock_gettime(CLOCK_MONOTONIC, &_tpDelay);                                \
            _result = (uint64_t)(_tpDelay.tv_sec * 1000000000ULL + _tpDelay.tv_nsec); \
        } while (0);                                                                  \
        _result;                                                                      \
    })
#endif

static __always_inline uint64_t ubsocket_get_timeNs()
{
#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
    uint64_t timeValue = 0;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
    return timeValue * 1000L / ubsocket_arm_cpu_freq;
#else
    struct timespec tpDelay = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tpDelay);
    return tpDelay.tv_sec * 1000000000ULL + tpDelay.tv_nsec;
#endif
}

/* 高性能打点宏（默认）*/
#define PROF_START(TP_ID)                           \
    uint64_t tpBegin##TP_ID = 0;                    \
    do {                                            \
        if (ubsocket_prof_enabled == 1) {           \
            tpBegin##TP_ID = ubsocket_get_timeNs(); \
        }                                           \
    } while (0)

#define PROF_END(TP_ID, GOOD)                                                                  \
    do {                                                                                       \
        if (ubsocket_prof_enabled == 1) {                                                      \
            ubsocket_prof_record(TP_ID, #TP_ID, ubsocket_get_timeNs() - tpBegin##TP_ID, GOOD); \
        }                                                                                      \
    } while (0)

#define PROF_RECORD(TP_ID, TP_NUM, GOOD)                       \
    do {                                                       \
        if (ubsocket_prof_enabled == 1) {                      \
            ubsocket_prof_record(TP_ID, #TP_ID, TP_NUM, GOOD); \
        }                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_PROFILING_H
