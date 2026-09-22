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

/* Per-packet RX delivery trace (four-point-trace byte-offset bridge).
 *
 * At ubs_poll delivery time each delivered segment is stamped with the
 * connection-level cumulative delivered-bytes cursor and flushed, keyed by
 * (fd, sn), to a per-process TSV log. The offline tool aligns these records
 * with brpc's rpc_trace log (which carries the same byte cursor per cid) so a
 * cid can be mapped onto the ubsocket SN(s) that carried it, enabling
 * per-packet latency attribution inside ubsocket.
 *
 * Record: fd | sn | byte_cursor | ts_ns  (delivery timestamp).
 *
 * Gated by env UBS_PKT_TRACE_ENABLE (default off); zero data-plane cost when
 * off. */

#ifndef UBS_COMM_UBS_PKT_TRACE_H
#define UBS_COMM_UBS_PKT_TRACE_H

#include <stdint.h>
#include <time.h>

namespace ock {
namespace ubs {

/* Stage events (阶段 4): entry timestamps along the bigdata TX/RX pipeline.
 * Key is (fd, first_sn); first_sn is per-offer (one RPC may span multiple
 * offers, and all fragments of one offer share its first_sn in imm.user_data).
 * Offline join matches by first_sn ∈ rpc.sn_set (membership, not min). */
enum UbsStageId : uint8_t {
    STAGE_PKT_DELIVERY = 0,     /* reserved: packet delivery record */
    STAGE_TRY_SENDER_POST = 1,  /* TrySenderPost entry (TX; key = LoadSeqNum pre-read) */
    STAGE_FLUSH_PENDING_OFFER = 2, /* FlushPendingOffer (TX; key = flushed offer first_sn) */
    STAGE_UMQ_POST_SEND = 3,    /* umq_post(SEND) succeeded (TX) */
    STAGE_READ_CQE = 4,         /* HandleTxCompletion READ-completion branch entry (RX) */
    STAGE_HANDLE_RX_CTRL = 5,   /* HandleRxControl entry (RX; key = offer imm first_sn) */
    STAGE_DO_READ_OFFER = 6,    /* DoReadOffer entry (RX) */
    STAGE_UMQ_POST_READ = 7,    /* umq_post(READ) succeeded (RX) */
    STAGE_FINALIZE_IO = 8,      /* FinalizeIo entry (RX) */
    STAGE_DELIVER_TO_RX_QUEUE = 9,  /* DeliverToRxQueue entry (RX) */
    STAGE_SEND_SIMPLE_CTRL = 10,    /* SendSimpleCtrl entry (RX) */
    STAGE_RX_CQE_DATA = 11,     /* RX dispatch of an inline-data CQE (SMALL_DATA;
                                 * key = segment SN in imm.user_data) */
};

/* One flushed record. stage_id == 0: a delivered segment's delivery stamp
 * (P row). stage_id != 0: a stage event (S row; sn carries the per-offer
 * first_sn or per-segment SN, byte_cursor is 0).
 *
 * sn_count != 0 marks a RANGE event (currently only STAGE_UMQ_POST_SEND):
 * the event covers the whole posted batch, whose wire SNs occupy the
 * contiguous range [sn, sn + sn_count) -- every batch buf reserves exactly
 * one SN (small/coalesced/offer alike, offer fragments share first_sn), so
 * one record lists all SN ids of the batch implicitly. The offline join
 * matches range events by interval intersection with an RPC's SN span. */
struct UbsPktTraceRecord {
    int32_t fd;
    uint32_t sn;          /* packet: segment SN (imm.user_data); stage: per-offer
                             first_sn / segment SN; range event: range start */
    uint64_t byte_cursor; /* packet: connection-level cumulative delivered bytes,
                             inclusive of this segment; stage: 0 */
    uint64_t ts_ns;       /* timestamp (CLOCK_MONOTONIC ns) */
    uint8_t stage_id;     /* UbsStageId */
    uint32_t sn_count;    /* 0 = single-SN event; >0 = range event covering
                             [sn, sn + sn_count). Fits in tail padding: the
                             record stays 32 bytes. */
};

/* Timestamp for trace records. Always CLOCK_MONOTONIC -- the same domain as
 * brpc's butil::cpuwide_time_ns() behind the four-point T2/T3 timestamps, so
 * cross-source deltas (T3 - seg.ts) in the offline join are valid. Do NOT
 * use ubsocket_get_timeNs_compile() here: under ENABLE_CPU_MONOTONIC on
 * aarch64 it reads raw cntvct_el0 whose zero point differs from
 * CLOCK_MONOTONIC, silently skewing those deltas. */
static inline uint64_t UbsPktTraceNowNs()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

/* Read the enable flag (cached at first call; env UBS_PKT_TRACE_ENABLE). */
bool UbsPktTraceEnabled();

/* Record one delivered segment. Lock-free (per-thread ring); no-op when off. */
void UbsPktTrace(int32_t fd, uint32_t sn, uint64_t byte_cursor, uint64_t ts_ns);

/* Record one stage event (阶段 4). Same ring/flush/switch as UbsPktTrace.
 * sn_count != 0 turns it into a range event covering [first_sn, first_sn +
 * sn_count) -- used by STAGE_UMQ_POST_SEND to attribute one posted batch to
 * every RPC whose segments it carries, with a single record. */
void UbsStageTrace(int32_t fd, uint32_t first_sn, uint8_t stage_id, uint64_t ts_ns, uint32_t sn_count = 0);

/* Lifecycle, wired into ubsocket_init/ubsocket_uninit. */
void UbsPktTraceStart();
void UbsPktTraceStop();

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBS_PKT_TRACE_H
