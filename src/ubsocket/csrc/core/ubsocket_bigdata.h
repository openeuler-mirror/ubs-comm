/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/*
 * UBSocket adaptive small/large I/O engine (design §5–§7).
 *
 * The regular ubsocket TX path only issues UMQ_OPC_SEND_IMM. This engine adds a
 * one-sided READ ("pull") path so a large segment can be moved with UMQ_OPC_READ:
 *   sender ubs_post -> adaptively route each segment: small ones go inline as
 *   SMALL_DATA SEND; large ones are merged into READ_OFFER control messages
 *   carrying UbsSeg[] + per-seg mempool info. The receiver imports the mempool,
 *   posts UMQ_OPC_READ to pull the payload, then delivers it via rxQueue and acks.
 *
 * Routing is per-segment by length (design §5.1), NOT a caller-specified policy:
 * a single ubs_post may carry small segments (SEND) and large segments (READ_OFFER)
 * interleaved, all sharing one connection-level ordered SN space so the receiver
 * delivers them in submission order regardless of path.
 *
 * Requires UMQ_FEATURE_ENABLE_REMOTE_MEM_ACCESS on both ends. Integration is
 * additive: the RX / TX-completion hooks are no-ops for ordinary SEND traffic.
 */
#ifndef UBS_COMM_UBSOCKET_BIGDATA_H
#define UBS_COMM_UBSOCKET_BIGDATA_H

#include <cstdint>

#include "core/ubsocket_core_types.h"
#include "core/ubsocket_proto.h"
#include "include/ubsocket_data.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {

class UbsBigdataSocketState;

namespace umq {
class UmqSocket;
}

class UbsBigdata {
public:
    /*
     * Sender entry from ubs_post. Routes data_list->segments adaptively:
     * small segments (len <= UBS_SMALL_DATA_MAX) go inline as SMALL_DATA SEND
     * (the region is pure payload; the kind is carried by imm_data bit 20 = 0);
     * large segments are merged into READ_OFFER control messages (up to
     * UBS_SEG_MAX UbsSeg per offer, respecting the 4064B wire budget). Returns
     * true if the data_list was fully handled by the bigdata path (out set to
     * the accepted segment count or -1/errno); false to fall back to the normal
     * SEND path.
     * An empty or all-small batch may return false so the normal path handles
     * it, but once any READ_OFFER is built the whole batch must be handled here
     * to keep SN assignment coherent.
     */
    static bool TrySenderPost(int fd, const ubs_data_list_t *data_list, ssize_t *out);

    /*
     * RX hook for the umq RX drain path. Returns true if qbuf was a bigdata
     * control message (READ_OFFER / READ_DONE / READ_ABORT) and was consumed;
     * the caller must not surface it to the application.
     */
    static bool HandleRxControl(const SocketPtr &sock, umq_buf_t *qbuf);

    /*
     * TX-completion hook (called from UmqTxHelper::PollUmqTxInternal). Returns
     * true if qbuf was a bigdata control-send or READ completion.
     */
    /* issue#38 根因修复：形参改裸指针——本函数会在 ~UmqSocket 内部的冲刷路径上被调到，
     * 彼时 ref_count_ 已为 0；任何 SocketPtr 形参都会让编译器用 Ref(T*) 隐式构造临时
     * 引用，临时析构时 delete this 重入正在执行的析构（死亡环 idx1584 铁证）。 */
    static bool HandleTxCompletion(Socket *sock, umq_buf_t *qbuf, uint32_t *completion_span,
                                   int *deferred_progress_fd);

    /*
     * Drain bigdata control posts that were paused by UMQ flow control, then
     * wake an outer ubs_post / KeepWrite waiter when protocol progress unblocks.
     */
    static void HandleFlowControlUpdate(const SocketPtr &sock);

    /*
     * Retry EAGAIN/ENOMEM-paused READ transactions for a socket. Called by the
     * TxCqePoller background thread even when no TX CQE was polled (pollNum==0),
     * because SQ space freed by the last CQE in a prior round may have left
     * pending_reads stuck with no new CQE to trigger RetryPendingReads via
     * HandleTxCompletion. Safe because AddQbuf is serialised per socket by
     * the RX-enqueue stripe lock (UmqSocket::RxEnqueueLockFor(fd)).
     */
    static void RetryPendingReadsForSocket(const SocketPtr &sock);

    /*
     * design §4.3: two-stage timeout release for expired pinned entries.
     * Called from the TX poller sweep cycle. Stage 1: clear headroom gen.
     * Stage 2: after grace period, DecRef blocks and erase.
     */
    static void SweepExpiredForSocket(const SocketPtr &sock);
    /* True when this socket still needs the TX poller's per-round attention
     * on the bigdata side: READs queued in pending_reads (RetryPendingReads),
     * pinned entries / receiver ctxs with a deadline (SweepExpired). Used by
     * TxCqePoller to decide whether a socket stays in its active set (see
     * TxCqePoller::MarkActive). */
    static bool NeedsPollerAttention(const SocketPtr &sock);

    /*
     * design §9 (A8): monitoring counters for gen check / timeout release.
     * Returns the four counters by out-params; any nullptr is skipped.
     *   pin_timeout     — sender stage-1 triggers (pinned entry deadline hit)
     *   gen_mismatch    — receiver stale READ discards (gen check failed)
     *   gen_fallback    — offers degraded to read_gen=0 (sliced/no-timeout/off)
     *   rx_ctx_timeout  — receiver ctx timeout reclaims (pending or active)
     */
    struct GenCheckStats {
        uint64_t pin_timeout{0};       /* big_pin_timeout_count */
        uint64_t gen_mismatch{0};      /* read_gen_mismatch_count */
        uint64_t gen_fallback{0};      /* read_gen_fallback_count (numerator) */
        uint64_t rx_ctx_timeout{0};    /* big_rx_ctx_timeout_count */
        uint64_t offer_total{0};       /* total offers sealed (fallback denominator) */
        uint64_t pin_alive_max_ms{0};  /* big_pin_alive_max_ms */
    };
    static GenCheckStats GetGenCheckStats();

    /*
     * Release state owned by one local UMQ generation after destroying its
     * handle has stopped further completions.
     */
    static void CleanupSocketState(umq::UmqSocket *sock);
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_BIGDATA_H
