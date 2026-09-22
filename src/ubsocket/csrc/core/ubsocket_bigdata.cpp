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
 * Send path: ubs_post -> TrySenderPost routes each segment by length. Small
 * segments (len <= UBS_SMALL_DATA_MAX) go inline as SMALL_DATA
 * SEND; large segments are merged into READ_OFFER control messages carrying
 * UbsSeg[] + a strictly 1:1 per-seg ub_mempool_info_t. A READ_OFFER is sealed
 * (and a new one started) when nsegs reaches UBS_SEG_MAX or the 4064B wire
 * budget is exhausted. Small segments interrupt large-segment merging.
 *
 * The receiver-side READ import / Finalize / READ_DONE path and the SN-ordered
 * ubs_poll delivery are filled in a follow-up stage; the SEND completion hook
 * (HandleTxCompletion) is implemented here so a control buffer is freed on its
 * SEND TX CQE while source Blocks stay pinned until READ_DONE/READ_ABORT.
 */
#include "core/ubsocket_bigdata.h"

#include <errno.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_logger.h"
#include "core/ubsocket_bigdata_order.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "core/umq/umq_errno_converter.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_socket.h"
#include "include/ubsocket_data.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/ubsocket_prof.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "profiling/trace/ubs_pkt_trace.h"
#include "umq_errno.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {

using ock::ubs::umq::UmqSetting;
using ock::ubs::umq::UmqSocket;

/* Forward: the state references the READ transaction context and vice-versa. */
struct UbsBigIoCtx;

/*
 * A control packet (READ_OFFER / READ_DONE / READ_ABORT) whose umq_post was
 * paused by UMQ flow control (EAGAIN). Retried on the next flow-control update
 * or TX completion cycle (design §9). Bounded: if the queue exceeds the cap
 * the oldest entry is dropped and its pin rolled back (rely on RPC timeout).
 */
struct UbsDeferredCtrl {
    int fd;
    uint64_t umqh;
    umq_buf_t *qbuf;
};

constexpr size_t UBS_BIG_DEFERRED_CTRL_MAX = 2048; /* 对齐 pending_reads：cover 单连接 ~1–2GiB DONE 积压 */

/* design §4.1: global monotonically increasing generation counter for read_gen
 * validation. fetch_add per post batch; 0 is reserved (means "no validation").
 * Skip-0 protection: if fetch_add returns 0, take another. */
static std::atomic<uint64_t> g_read_gen{1};

/* design §9 (A8): monitoring counters for gen check / timeout release.
 * Read via UbsBigdata::GetGenCheckStats() or logged from the print loop. */
static std::atomic<uint64_t> g_pin_timeout_count{0};       /* sender stage-1 triggers */
static std::atomic<uint64_t> g_read_gen_mismatch_count{0}; /* receiver stale read discards */
static std::atomic<uint64_t> g_read_gen_fallback_count{0}; /* offers with read_gen=0 (sliced/no-timeout/feature-off) */
static std::atomic<uint64_t> g_rx_ctx_timeout_count{0};    /* receiver ctx timeout reclaims */
static std::atomic<uint64_t> g_offer_total_count{0};       /* total offers sealed (denominator for fallback ratio) */
static std::atomic<uint64_t> g_pin_alive_max_ms{0};        /* max pinned entry alive duration (ms) */

/* design §4.1/§4.3: per-transaction pinned entry. Replaces the bare
 * vector<Block*> with metadata for two-stage release and gen validation. */
struct PinnedEntry {
    std::vector<Block *> blocks;
    uint64_t gen{0};                        /* gen written to headroom and ctrl_hdr; 0 = no validation */
    std::vector<void *> headrooms;          /* addresses to clear on timeout stage-1 (data - 8) */
    uint64_t deadline_ns{0};                /* stage-1 trigger: now >= deadline_ns → clear headroom */
    uint64_t grace_deadline_ns{0};          /* stage-2 trigger: now >= grace_deadline_ns → DecRef */
    uint64_t create_ns{0};                  /* design §9: creation timestamp for alive_max_ms */
    bool stage1_done{false};                /* two-stage release: stage-1 (headroom clear) done */
};

/*
 * Per-socket bigdata state (design §8). pinned[seq] holds the source IOBuf
 * Blocks referenced by each in-flight READ_OFFER until READ_DONE/READ_ABORT.
 */
class UbsBigdataSocketState {
public:
    UbsBigdataSocketState()
    {
        mutex = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~UbsBigdataSocketState()
    {
        if (mutex != nullptr) {
            LockRegistry::LOCK_OPS.destroy(mutex);
            mutex = nullptr;
        }
    }
    u_mutex_t *mutex{nullptr};
    std::atomic<bool> destroying{false}; /* set during cleanup so completion handlers skip state access */
    std::unordered_map<uint64_t, PinnedEntry> pinned; /* seq -> pinned source Blocks + gen metadata */
    std::unordered_set<UbsBigIoCtx *> active_io;               /* in-flight READ transactions (receiver) */
    /* 惰性 deque：libstdc++ 空 deque 的构造（_Deque_base::_M_initialize_map）
     * 就做 2 次堆分配（8 指针 map 64B + 一个 ~512B 数据节点），两个成员即
     * ≈1.1KB/链路——而 TrySenderPost 对每条发过包的链路无条件创建本 state，
     * 常规链路两个队列终生为空。nullptr == 空；首次入队时在 mutex 内创建。 */
    std::unique_ptr<std::deque<UbsDeferredCtrl>> deferred_ctrl; /* EAGAIN-paused control packets */
    /* EAGAIN-paused READ transactions whose READ WRs have not been submitted
     * yet (post_read_list returned EAGAIN with nothing accepted). Retried on
     * the next flow-control / TX completion cycle (design §9). */
    std::unique_ptr<std::deque<UbsBigIoCtx *>> pending_reads;

    bool DeferredCtrlEmpty() const
    {
        return deferred_ctrl == nullptr || deferred_ctrl->empty();
    }
    std::size_t DeferredCtrlSize() const
    {
        return deferred_ctrl == nullptr ? 0 : deferred_ctrl->size();
    }
    /* 分配失败返回 nullptr，调用方按"无法入队"降级（与有界队列打满同路） */
    std::deque<UbsDeferredCtrl> *EnsureDeferredCtrl()
    {
        if (deferred_ctrl == nullptr) {
            deferred_ctrl.reset(new (std::nothrow) std::deque<UbsDeferredCtrl>());
        }
        return deferred_ctrl.get();
    }
    bool PendingReadsEmpty() const
    {
        return pending_reads == nullptr || pending_reads->empty();
    }
    std::deque<UbsBigIoCtx *> *EnsurePendingReads()
    {
        if (pending_reads == nullptr) {
            pending_reads.reset(new (std::nothrow) std::deque<UbsBigIoCtx *>());
        }
        return pending_reads.get();
    }

    /* design §9 perf: fast-path counters for SweepExpiredForSocket. Track the
     * number of entries with non-zero deadline_ns. When both are 0, the poller
     * sweep skips entirely — no RefConvert, no CurrentTimeNs, no mutex lock.
     * Avoids O(N) iteration of non-empty containers when gen check is inactive
     * (feature off, effective_timeout=0, or all offers fallback to gen=0). */
    std::atomic<uint32_t> pinned_with_deadline{0};
    std::atomic<uint32_t> ctx_with_deadline{0};

    /* Reference count: 1 owner ref (the socket / CleanupSocketState) + one ref
     * per live UbsBigIoCtx (dropped by DeleteIoUctx). Keeps the state alive
     * while the CQE path (FinalizeIo) or transient users (DoReadOffer /
     * HandleTxCompletion / TrySenderPost) still hold it during teardown,
     * closing the race between ctx->state->mutex lock and delete state. */
    std::atomic<uint32_t> refcnt{1};

    void IncreaseRef() noexcept
    {
        refcnt.fetch_add(1, std::memory_order_relaxed);
    }

    /* Release this ref; when it was the last one, delete the state (the
     * "last ref deletes" pattern, same as Referable::DecreaseRef). */
    void DecreaseRef() noexcept
    {
        if (refcnt.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

/* Bounded cap for the pending-read retry queue (design §9). When full and no
 * READ was submitted, the transaction is dropped (ABORT + repost RX).
 * Default 2048 covers a single ~1–2GiB body at ~64KB/seg (ceil(2GiB/64K/32)
 * ≈ 1025 offers) with headroom under TX_DEPTH≈255. */
constexpr size_t UBS_BIG_PENDING_READ_MAX = 2048;

/*
 * One bigdata READ transaction (receiver-side). Each READ WR points at a stable
 * UbsBigQbufSlot so completions can be attributed even when only the terminal
 * WR produces a CQE (design §6.3). The sender only pins Blocks and posts the
 * offer; it does not allocate an IoCtx.
 */
struct UbsBigIoCtx {
    UbsBigdataSocketState *state{nullptr};
    /* Receiver SocketPtr held for the transaction's lifetime (parallels the
     * state ref below). FinalizeIo / SendSimpleCtrl / DeliverToRxQueue use this
     * instead of re-resolving ArraySet<Socket>::GetItem(fd) on every RPC — at
     * low QPS the hash lookup is a cold-cache memory round-trip. Released when
     * the ctx is deleted. */
    SocketPtr recv_sock;
    int fd{-1};
    uint64_t umqh{0};
    uint64_t seq{0};      /* UbsCtrlHdr.seq, matches the offer's pin entry */
    uint32_t first_sn{0}; /* first connection-level SN; slot i -> sn_add(first_sn, i) */
    uint32_t wr_total{0};
    std::atomic<uint32_t> wr_state{0};
    std::atomic<bool> failed{false};
    std::atomic<bool> finalized{false};
    std::vector<detail::UbsBigQbufSlot> wr_slots;
    /* Retained READ_OFFER RX buffer (design §6.1): kept in the transaction
     * context while the READ WRs are in flight, then reposted back to the RX
     * pool in FinalizeIo after all READs reach a terminal CQE. */
    umq_buf_t *offer_rx_buf{nullptr};
    size_t next_post{0};
    /* design §4.4/§4.5: gen validation + receiver ctx deadline */
    uint64_t expect_gen{0};      /* gen from READ_OFFER ctrl; 0 = no validation */
    uint64_t deadline_ns{0};     /* receiver ctx deadline; 0 = no timeout */
};

namespace {

/* Process-wide bigdata transaction id (design §5.1: UbsCtrlHdr.seq is a
 * process-level id for pin/unpin lookup, distinct from the per-connection
 * ordered SN). uint64 monotonically increasing. */
std::atomic<uint64_t> g_seq{static_cast<uint64_t>(std::random_device{}()) << 32 |
                            static_cast<uint64_t>(std::random_device{}())};

UbsBigdataSocketState *GetBigdataState(const SocketPtr &sock, bool create)
{
    auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
    if (umq_sock == nullptr) {
        return nullptr;
    }
    return create ? umq_sock->GetOrCreateBigdataState() : umq_sock->GetBigdataState();
}

/* issue#38 根因修复：裸指针版——零引用操作，供析构可达的 TX 完成路径使用
 * （SocketPtr 版会经 RefConvert 造临时引用，在 ref_count_==0 的对象上=复活再杀）。 */
UbsBigdataSocketState *GetBigdataState(Socket *sock, bool create)
{
    auto *umq_sock = dynamic_cast<UmqSocket *>(sock);
    if (umq_sock == nullptr) {
        return nullptr;
    }
    return create ? umq_sock->GetOrCreateBigdataState() : umq_sock->GetBigdataState();
}

/* fd -> UmqSocket lookup. The out-param holder keeps the SocketPtr alive
 * across the call; returns nullptr if fd is not a umq socket. */
UmqSocket *GetUmqSocketLookup(int fd, SocketPtr &holder)
{
    holder = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (holder == nullptr) {
        return nullptr;
    }
    return RefConvert<Socket, UmqSocket>(holder).Get();
}

ALWAYS_INLINE umq_io_option_t TxOption()
{
    return umq_io_option_t{UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
}

/* 控制帧 SQ 槽位预留（env UBSOCKET_BIG_CTRL_RESERVED_SLOTS，默认 0 = 关闭）。
 * 返回数据路径本次 post 可用的 WR 额度：tx_queue_avail_num_ - 预留数。数据
 * 路径（DoReadOffer / RetryPendingReads 的 READ 链、TrySenderPost 的批量
 * SEND）按额度让路，保证 READ_DONE/READ_ABORT 等控制帧总有 SQ 槽可用，不被
 * 挤进 deferred-ctrl 队列直至溢出丢帧（丢 DONE → 发送端 pin 到超时才解）。
 * 控制路径（SendSimpleCtrl / DrainDeferredControls）不查额度。关闭或无
 * tx_ops 时返回 SIZE_MAX（不限制）。软计数是近似值（CQE 回补有延迟），预留
 * 是软下限而非硬保证，但配合 DrainDeferredControls 先于 RetryPendingReads
 * 的排水顺序足以避免控制帧长期饿死。 */
size_t DataPostBudget(UmqSocket *umq_sock)
{
    const uint32_t reserved = GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS;
    if (reserved == 0 || umq_sock == nullptr) {
        return SIZE_MAX;
    }
    auto *tx_ops = umq_sock->GetTxOps();
    if (tx_ops == nullptr) {
        return SIZE_MAX;
    }
    const uint16_t avail = tx_ops->tx_queue_avail_num_.load(std::memory_order_acquire);
    return avail > reserved ? static_cast<size_t>(avail - reserved) : 0;
}

/* 发送端单次 post 上限：再与硬件 SQ 深度取 min，避免 UMQ_BATCH_SIZE(256)
 * 硬顶默认 TX_DEPTH(255) 导致整批 EAGAIN 空转（1G 会封出数百个 OFFER）。 */
size_t SenderPostBudget(UmqSocket *umq_sock)
{
    size_t n = DataPostBudget(umq_sock);
    const uint32_t tx_depth = GlobalSetting::UBS_TX_DEPTH;
    if (tx_depth > 0 && static_cast<size_t>(tx_depth) < n) {
        n = static_cast<size_t>(tx_depth);
    }
    return n;
}

ALWAYS_INLINE void ConfigureControlSend(umq_buf_t *ctrl, uint32_t sn, bool has_sn, bool big_ctrl)
{
    /* Control packets (READ_OFFER/DONE/ABORT) carry their metadata in the buffer
     * data region and their kind (BIG_CTRL) + SN in the umq imm word. They must
     * use SEND_IMM: umq_ub_fill_wr only fills imm.user_data for SEND_IMM (the
     * SEND branch leaves imm at the backend default), and the rsvd1 window
     * (bit 20) holding the BIG_CTRL marker is propagated to the wire imm only via
     * the SEND_IMM path. The ordinary data path uses SEND_IMM too and never sets
     * bit 20, so is_big_ctrl stays false there. */
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(ctrl->qbuf_ext);
    (void)memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->flag.bs.complete_enable = 1;
    /* Control metadata lives in the buffer's own data region; do not inline it
     * as UMQ inline data. */
    pro->flag.bs.inline_flag = 0;
    pro->user_ctx = reinterpret_cast<uint64_t>(ctrl);
    if (big_ctrl) {
        /* Design §3.1: classify the data region as a BIG_CTRL packet via imm_data
         * bit 20. The bit sits in the umq imm word's reserved field (io_imm.rsvd1),
         * so setting it does not collide with the SN written below (imm.user_data,
         * high 24 bits) nor with the umq backend's low type/umq_id fields. */
        pro->imm_data = proto::mark_big_ctrl(pro->imm_data);
    }
    if (has_sn) {
        pro->imm.user_data = sn; /* high 24 bits carry the connection-level ordered SN */
    }
}

void FreeQbufChain(umq_buf_t *head)
{
    while (head != nullptr) {
        umq_buf_t *next = head->qbuf_next;
        head->qbuf_next = nullptr;
        UmqApi::umq_buf_free(head);
        head = next;
    }
}

/* Release pinned Blocks for one transaction seq (design §5.3, §6.3). Called on
 * READ_DONE/READ_ABORT receive and on send-failure rollback. Idempotent. */
void ReleasePinned(UbsBigdataSocketState *state, uint64_t seq)
{
    if (state == nullptr) {
        return;
    }
    PinnedEntry entry;
    {
        Locker lk(state->mutex);
        auto it = state->pinned.find(seq);
        if (it == state->pinned.end()) {
            UBS_DATAPATH_LOG("[datapath] ReleasePinned miss (idempotent DONE/ABORT), seq: %llu\n",
                             static_cast<unsigned long long>(seq));
            return;
        }
        /* design §4.3: if stage-1 (timeout headroom clear) already done, the
         * blocks are being swept by SweepExpiredPinned stage-2. Skip DecRef
         * here to avoid double-free; SweepExpiredPinned owns the release. */
        if (it->second.stage1_done) {
            UBS_DATAPATH_LOG("[datapath] ReleasePinned skip (stage-1 done, sweeper owns release), seq: %llu\n",
                             static_cast<unsigned long long>(seq));
            /* design §9 perf: stage-1 done implies deadline_ns != 0, already
             * counted; sweeper stage-2 will decrement via to_release path */
            state->pinned.erase(it);
            return;
        }
        entry = std::move(it->second);
        state->pinned.erase(it);
    }
    /* design §9 perf: decrement fast-path counter if this entry had a deadline */
    if (entry.deadline_ns != 0) {
        state->pinned_with_deadline.fetch_sub(1, std::memory_order_relaxed);
    }
    /* design §9: track max alive duration on normal release too */
    if (entry.create_ns != 0) {
        uint64_t now_ns = Func::CurrentTimeNs();
        uint64_t alive_ms = (now_ns - entry.create_ns) / 1000000ULL;
        uint64_t prev = g_pin_alive_max_ms.load(std::memory_order_relaxed);
        while (alive_ms > prev &&
               !g_pin_alive_max_ms.compare_exchange_weak(prev, alive_ms,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }
    UBS_DATAPATH_LOG("[datapath] ReleasePinned unpin source Blocks, seq: %llu, blocks: %zu\n",
                     static_cast<unsigned long long>(seq), entry.blocks.size());
    for (Block *b : entry.blocks) {
        if (b != nullptr) {
            b->DecRef();
        }
    }
}

/* design §4.3: two-stage timeout release for expired pinned entries.
 * Stage 1: clear headroom gen to 0 (store-release) so late READs fail validation.
 * Stage 2: after grace period (UBS_GRACE_MS), DecRef blocks and erase entry.
 * Called from the TX poller sweep cycle. */
void SweepExpiredPinned(UbsBigdataSocketState *state)
{
    if (state == nullptr) {
        return;
    }
    const uint64_t now_ns = Func::CurrentTimeNs();
    std::vector<std::pair<uint64_t, std::vector<Block *>>> to_release;
    {
        Locker lk(state->mutex);
        if (state->destroying.load(std::memory_order_acquire)) {
            return;
        }
        for (auto it = state->pinned.begin(); it != state->pinned.end();) {
            PinnedEntry &entry = it->second;
            if (entry.stage1_done) {
                /* Stage 2: grace period elapsed → real release */
                if (now_ns >= entry.grace_deadline_ns) {
                    /* design §9: track max alive duration */
                    if (entry.create_ns != 0) {
                        uint64_t alive_ms = (now_ns - entry.create_ns) / 1000000ULL;
                        uint64_t prev = g_pin_alive_max_ms.load(std::memory_order_relaxed);
                        while (alive_ms > prev &&
                               !g_pin_alive_max_ms.compare_exchange_weak(prev, alive_ms,
                                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
                    }
                    to_release.emplace_back(it->first, std::move(entry.blocks));
                    /* design §9 perf: entry had deadline (stage-1 done → deadline!=0) */
                    state->pinned_with_deadline.fetch_sub(1, std::memory_order_relaxed);
                    it = state->pinned.erase(it);
                    continue;
                }
            } else if (entry.deadline_ns != 0 && now_ns >= entry.deadline_ns) {
                /* Stage 1: invalidate headroom, start grace timer */
                for (void *hr : entry.headrooms) {
                    *reinterpret_cast<volatile uint64_t *>(hr) = 0;
                }
                std::atomic_thread_fence(std::memory_order_release);
                entry.stage1_done = true;
                entry.grace_deadline_ns = now_ns +
                    static_cast<uint64_t>(GlobalSetting::UBS_GRACE_MS) * 1000000ULL;
                g_pin_timeout_count.fetch_add(1, std::memory_order_relaxed);
                UBS_DATAPATH_LOG("[datapath] SweepExpiredPinned stage-1, seq: %llu, gen: %llu, grace_deadline: %llu ns\n",
                                 static_cast<unsigned long long>(it->first),
                                 static_cast<unsigned long long>(entry.gen),
                                 static_cast<unsigned long long>(entry.grace_deadline_ns));
            }
            ++it;
        }
    }
    /* DecRef outside the lock to minimize critical section */
    for (auto &kv : to_release) {
        UBS_DATAPATH_LOG("[datapath] SweepExpiredPinned stage-2 release, seq: %llu, blocks: %zu\n",
                         static_cast<unsigned long long>(kv.first), kv.second.size());
        for (Block *b : kv.second) {
            if (b != nullptr) {
                b->DecRef();
            }
        }
    }
}

/*
 * The mempool descriptor carried in the bigdata control packet is an opaque blob
 * to ubsocket: umq's umq_mempool_info_get writes the (ptr, len) and ubsocket only
 * memcpy's it into the wire entry; on receive it forwards the blob verbatim to
 * umq_mempool_info_set / umq_remote_mempool_state_check, and extracts the READ
 * addressing fields via umq_mempool_info_get_remote_fields. ubsocket neither
 * defines nor inspects the blob struct — that layout is umq-private
 * (ub_import_mempool_info_t). addr/length for the READ sge come from the offer's
 * UbsSeg; mempool_id/token_id/token_value come from umq's getter on the blob.
 *
 * Cross-check: ubsocket's wire budget constant UBS_MEMPOOL_INFO_HDR_SIZE must equal
 * umq's UMQ_MEMPOOL_INFO_HDR_SIZE so the sizes ubsocket pre-allocates match what
 * umq writes. ubsocket_bigdata.cpp is the single translation unit that sees both.
 */
static_assert(UBS_MEMPOOL_INFO_HDR_SIZE == UMQ_MEMPOOL_INFO_HDR_SIZE,
              "ubsocket and umq mempool-info header sizes must match");
constexpr uint32_t UBS_URMA_SEG_T_SIZE = 48; /* sizeof(urma_seg_t), for sanity bounds */

/*
 * Build a SMALL_DATA packet for one small segment (design §5.2). The data region
 * is pure payload (no UbsHdr); the packet kind is carried by imm_data bit 20 = 0,
 * which ConfigureControlSend leaves clear. On success returns the allocated
 * control qbuf (caller owns it until its TX CQE); on failure returns nullptr
 * with errno set.
 */
umq_buf_t *BuildSmallData(uint64_t umqh, const ubs_segment_t &seg, uint32_t sn)
{
    PROF_START(UBS_NATIVE_BUILD_SMALL_DATA);
    /* Zero-copy SMALL_DATA: a without_data qbuf (umq_buf_alloc(0, ..)) carries only
     * the header; its buf_data is repointed at the brpc Block's umq-registered data,
     * exactly like the writev path (UmqIovConverter::MemCopy repoints buf_data at
     * iov_base). umq_post_tx recovers the source local tseg from buf_data via
     * umq_data_to_head when mempool_without_data==1 (hy DoWriteAddr uses the same
     * pattern, see its comment). Unlike control bufs, SMALL_DATA is ordinary data:
     * it uses SEND_IMM so umq_ub_fill_wr fills imm.user_data with the SN, and its
     * CQE is reclaimed by the normal ProcessTxCqe path (DataToBlock DecRefs the
     * brpc Block, umq_buf_free returns the without_data header). */
    const uint32_t payload = seg.len;
    const uint32_t wire = payload; /* SMALL_DATA region is pure payload */
    umq_buf_t *buf = UmqApi::umq_buf_alloc(0, 1, umqh, nullptr);
    if (buf == nullptr) {
        PROF_END(UBS_NATIVE_BUILD_SMALL_DATA, false);
        UBS_VLOG_ERR("BuildSmallData umq_buf_alloc failed, umqh: %llu, seg: %p, len: %llu\n", umqh, seg, seg.len);
        return nullptr;
    }
    Block *block = reinterpret_cast<Block *>(seg.block);
    block->IncRef();
    buf->rsvd4 = reinterpret_cast<uint64_t>(block);

    char *data = (seg.start_pos != nullptr) ? static_cast<char *>(seg.start_pos) + seg.offset :
                                              (block->data + seg.offset);

    buf->data_size = wire;
    buf->total_data_size = wire;
    buf->buf_data = data;

    /* Ordinary SEND_IMM data semantics (mirrors umq_data_tx_ops::PostSend). Do NOT
     * use ConfigureControlSend: it sets opcode=UMQ_OPC_SEND, which skips imm fill
     * in umq_ub_fill_wr (only SEND_IMM fills imm.user_data), losing the SN; and its
     * opcode==SEND + bit20=0 shape collides with the umq FC fake buf, misrouting
     * the CQE. bit20 stays clear (default), so is_big_ctrl==false everywhere. */
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
    (void)memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->flag.bs.complete_enable = 1;
    pro->user_ctx = reinterpret_cast<uint64_t>(buf); /* ProcessTxCqe anchors on user_ctx */
    pro->imm.user_data = sn;                         /* high 24 bits: connection-level ordered SN */
    PROF_END(UBS_NATIVE_BUILD_SMALL_DATA, true);
    return buf;
}

/*
 * Build a coalesced SMALL_DATA packet for count consecutive small segments
 * (design §5.2 optimization). Copies all segment payloads into one
 * with_data umq_buf (umq_buf_alloc(total_len, 1, ..)), so N segments share
 * 1 WR and 1 connection-level SN. The receiver parses individual messages
 * from the byte stream via PRPC body_size (not SN boundaries), so SN-per-batch
 * ordering is correct. On success returns the allocated data qbuf; on failure
 * returns nullptr with errno set.
 *
 * Unlike BuildSmallData (zero-copy without_data), this path does NOT IncRef
 * the source brpc Blocks: the data is memcpy'd into the umq region, so the
 * source Blocks are owned by the caller's IOBuf lifecycle. ProcessTxCqe
 * recognizes the coalesced buf via buf->is_coalesced_small and skips
 * DataToBlock/DecRef, only calling umq_buf_free to reclaim the with_data region.
 */
umq_buf_t *BuildCoalescedSmallData(uint64_t umqh, const ubs_segment_t *segs, uint16_t count, uint32_t sn)
{
    PROF_START(UBS_NATIVE_BUILD_COALESCED_SMALL_DATA);
    uint32_t total_len = 0;
    for (uint16_t k = 0; k < count; ++k) {
        total_len += segs[k].len;
    }
    /* Caller guarantees total_len <= UBS_SMALL_DATA_MAX (4064). umq_buf_alloc's
     * minimum with_data block is 4KB (the pool's smallest size_class), so any
     * request_size < 4096 still allocates a full 4KB data region; pass the
     * documented minimum to stay within the supported range, let umq hand back
     * a 4KB region, then narrow data_size/total_data_size to total_len below. */
    umq_buf_t *buf = UmqApi::umq_buf_alloc(UBS_UMQ_MIN_DATA_BLOCK_SIZE, 1, umqh, nullptr);
    if (buf == nullptr || buf->buf_data == nullptr) {
        PROF_END(UBS_NATIVE_BUILD_COALESCED_SMALL_DATA, false);
        UBS_VLOG_ERR("BuildCoalescedSmallData umq_buf_alloc failed, umqh: %llu, total_len: %u, count: %u\n",
                     umqh, total_len, count);
        return nullptr;
    }
    /* Copy each segment's payload into the with_data region. */
    char *dest = static_cast<char *>(buf->buf_data);
    uint32_t off = 0;
    for (uint16_t k = 0; k < count; ++k) {
        const ubs_segment_t &seg = segs[k];
        const char *src = (seg.start_pos != nullptr)
                              ? static_cast<const char *>(seg.start_pos) + seg.offset
                              : static_cast<const Block *>(seg.block)->data + seg.offset;
        (void)memcpy(dest + off, src, seg.len);
        off += seg.len;
    }
    buf->data_size = total_len;
    buf->total_data_size = total_len;

    /* Ordinary SEND_IMM data semantics (mirrors BuildSmallData). The SN in
     * imm.user_data covers the whole coalesced batch; the receiver enqueues
     * this 1 WR by its SN and parses N messages from the byte stream. */
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
    (void)memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->flag.bs.complete_enable = 1;
    pro->user_ctx = reinterpret_cast<uint64_t>(buf); /* ProcessTxCqe anchors on user_ctx */
    pro->imm.user_data = sn;                         /* 1 SN for the whole coalesced batch */
    buf->is_coalesced_small = 1;                     /* skip DataToBlock in CQE reclaim */
    PROF_END(UBS_NATIVE_BUILD_COALESCED_SMALL_DATA, true);
    return buf;
}

/*
 * One in-progress READ_OFFER under construction. Accumulates large segments
 * until nsegs reaches UBS_SEG_MAX or the 4064B budget is exhausted, then is
 * sealed and posted.
 */
struct OfferBuilder {
    umq_buf_t *ctrl{nullptr};
    UbsCtrlHdr *ctrl_hdr{nullptr};
    UbsSeg *segs{nullptr};
    uint8_t *infos{nullptr};
    uint16_t nsegs{0};
    uint16_t nsegs_at_seal{0}; /* snapshot of nsegs when SealOnly produced the buf */
    uint32_t infos_bytes{0};
    uint8_t *info_cursor{nullptr};
    uint32_t ctrl_sn{0};
    uint64_t ctrl_hdr_seq{0};
    std::vector<Block *> pinned;
    std::vector<void *> headrooms; /* design §4.1: headroom addresses for gen cleanup */
    uint64_t gen{0};               /* design §4.1: batch gen; 0 = no validation */
    uint64_t deadline_ns{0};       /* design §4.1: pinned deadline; 0 = no timeout */
    bool has_sn{false};
    bool has_sliced_seg{false};    /* design §4.6: any seg without headroom → offer read_gen=0 */

    /* Whether adding one more UbsSeg + mempool_info fits the 4064B budget and
     * the 32-entry protocol cap (design §4.1). */
    bool CanAppend() const
    {
        if (ctrl == nullptr) {
            return false;
        }
        if (nsegs >= UBS_SEG_MAX) {
            return false;
        }
        const uint32_t next_infos = infos_bytes + UMQ_MEMPOOL_INFO_MAX_SIZE;
        return proto::ctrl_offer_layout_valid(static_cast<uint16_t>(nsegs + 1),
                                              static_cast<uint16_t>(nsegs + 1),
                                              next_infos, 0);
    }

    bool Alloc(uint64_t umqh)
    {
        /* Allocate for the maximum possible fill (32 entries) up front; the
         * 4064B budget is always <= 2072B at inline_data_len=0 (24 + 32*64) so
         * this is bounded and cheap. */
        const uint32_t per_entry = UBS_SEG_SIZE + UMQ_MEMPOOL_INFO_MAX_SIZE;
        const uint32_t cap = UBS_CTRL_HDR_SIZE + UBS_SEG_MAX * per_entry;
        ctrl = UmqApi::umq_buf_alloc(cap, 1, umqh, nullptr);
        if (ctrl == nullptr) {
            errno = EAGAIN;
            return false;
        }
        ctrl_hdr = reinterpret_cast<UbsCtrlHdr *>(ctrl->buf_data);
        segs = reinterpret_cast<UbsSeg *>(ctrl_hdr + 1);
        /* mempool_info entries are appended sequentially at info_cursor; the
         * wire format packs them right after segs[nsegs] at seal time. */
        infos = reinterpret_cast<uint8_t *>(segs + UBS_SEG_MAX);
        info_cursor = infos;
        return true;
    }

    /* Append one large segment. On any internal failure rolls back this offer's
     * pinned Blocks and frees the ctrl buffer, returning false. */
    bool Append(uint64_t umqh, const ubs_segment_t &seg, int fd, uint16_t seg_idx)
    {
        if (!CanAppend()) {
            return false;
        }

        FillWireSeg(seg);
        uint32_t info_len = 0;
        if (!FillWireMempoolInfo(umqh, seg, info_len)) {
            UBS_VLOG_ERR("FillWireMempoolInfo block->qbuf failed, seg: %u, seg_idx: %u\n", seg, seg_idx);
            return false;
        }
        segs[nsegs].mempool_info_len = static_cast<uint16_t>(info_len);
        infos_bytes += info_len;
        info_cursor = reinterpret_cast<uint8_t *>(
            reinterpret_cast<char *>(info_cursor) + info_len);

        Block *block = reinterpret_cast<Block *>(seg.block);
        char *data = (seg.start_pos != nullptr) ? static_cast<char *>(seg.start_pos) + seg.offset :
                                                  (block->data + seg.offset);
        block->IncRef();
        pinned.push_back(block);

        /* design §4.1: write gen into headroom if the block has one and the
         * seg starts at the block's data origin (whole-block seg, not sliced).
         * start_pos may alias block->data; arbitrary start_pos with offset>0
         * is sliced — see §4.6 compatibility matrix. */
        const bool at_block_origin =
            seg.offset == 0 && (seg.start_pos == nullptr || seg.start_pos == block->data);
        if (gen != 0 && (block->flags & IOBUF_BLOCK_FLAGS_GEN_HEADROOM) && at_block_origin) {
            *reinterpret_cast<volatile uint64_t *>(data - 8) = gen;
            headrooms.push_back(data - 8);
        } else if (gen != 0) {
            /* design §4.6: sliced seg or no headroom flag → offer read_gen=0 */
            has_sliced_seg = true;
        }

        ++nsegs;
        UBS_DATAPATH_LOG("[datapath] offer UbsSeg filled (big), fd: %d, offer_sn: %u, seq: %llu, slot: %u, addr: 0x%llx, "
                         "len: %u, mempool_info_idx: %u, mempool_info_len: %u\n",
                         fd, ctrl_sn, static_cast<unsigned long long>(ctrl_hdr_seq), nsegs - 1,
                         static_cast<unsigned long long>(segs[nsegs - 1].addr), segs[nsegs - 1].length,
                         segs[nsegs - 1].mempool_info_idx, segs[nsegs - 1].mempool_info_len);
        return true;
    }

    void FillWireSeg(const ubs_segment_t &seg)
    {
        Block *block = reinterpret_cast<Block *>(seg.block);
        char *data = (seg.start_pos != nullptr) ? static_cast<char *>(seg.start_pos) + seg.offset :
                                                  (block->data + seg.offset);

        UbsSeg &sge = segs[nsegs];
        (void)memset(&sge, 0, sizeof(sge));
        sge.addr = reinterpret_cast<uint64_t>(data);
        sge.length = seg.len;
        sge.mempool_info_idx = static_cast<uint8_t>(nsegs); /* strict 1:1 by index */
        return;
    }

    bool FillWireMempoolInfo(uint64_t umqh, const ubs_segment_t &seg, uint32_t &out_len)
    {
        Block *block = reinterpret_cast<Block *>(seg.block);
        char *data = (seg.start_pos != nullptr) ? static_cast<char *>(seg.start_pos) + seg.offset :
                                                  (block->data + seg.offset);
        /* Recover the umq qbuf backing this Block (data -> owning qbuf). */
        umq_buf_t *qbuf = UmqApi::umq_data_to_head(data);
        if (qbuf == nullptr || qbuf->buf_data > data || qbuf->buf_data + qbuf->buf_size < data + seg.len) {
            UBS_VLOG_ERR("PostRead block->qbuf failed, data: %u, seg: %u\n", data, seg);
            return false;
        }

        /* umq serializes the mempool descriptor directly into the wire entry at
         * info_cursor (umq's private layout, opaque to ubsocket). ubsocket treats
         * the blob as opaque (ptr, len) — it neither defines nor inspects fields;
         * umq's returned blob_len is the exact entry byte length to carry. */
        uint8_t *out = info_cursor;
        uint32_t blob_len = 0;
        if (UmqApi::umq_mempool_info_get(umqh, qbuf->mempool_id, out,
                                         UMQ_MEMPOOL_INFO_MAX_SIZE, &blob_len) != 0) {
            UBS_VLOG_ERR("PostRead umq_mempool_info_get failed, data: %u, seg: %u, mempool_id: %u\n", data, seg,
                         qbuf->mempool_id);
            return false;
        }
        /* Sanity bounds only: umq wrote blob_len bytes; trust it within the
         * [header+plain_seg, header+max_seg] range without inspecting fields. */
        if (blob_len < UBS_MEMPOOL_INFO_HDR_SIZE + UBS_URMA_SEG_T_SIZE ||
            blob_len > UMQ_MEMPOOL_INFO_MAX_SIZE) {
            UBS_VLOG_ERR("FillWireMempoolInfo bad blob_len=%u, mempool_id: %u\n", blob_len, qbuf->mempool_id);
            return false;
        }
        out_len = blob_len;
        return true;
    }

    /* Seal the offer: finalize headers, register pin, and return the control
     * buffer (NOT posted). The caller batches sealed control buffers and small
     * bufs into one chain and posts them in a single umq_post (design §5.1),
     * so partial-success semantics are decided once for the whole batch
     * instead of per-segment. Returns the control buffer on success, nullptr on
     * failure (pin rolled back, buffer freed). */
    umq_buf_t *SealOnly(UbsBigdataSocketState *state)
    {
        if (nsegs == 0) {
            UBS_VLOG_ERR("SealOnly empty offer, fd: %d\n", state);
            return nullptr; /* nothing to seal */
        }

        (void)memset(ctrl_hdr, 0, sizeof(*ctrl_hdr));
        ctrl_hdr->type = UBS_READ_OFFER;
        ctrl_hdr->nsegs = nsegs;
        ctrl_hdr->nmempool_infos = nsegs; /* strict 1:1 */
        ctrl_hdr->inline_data_len = 0;
        ctrl_hdr->total_len = static_cast<uint16_t>(proto::ctrl_total_len(nsegs, infos_bytes, 0));
        ctrl_hdr->seq = ctrl_hdr_seq;
        /* design §4.1/§4.6: set read_gen to gen, or 0 if any seg is sliced
         * (no headroom) — the receiver must not extend READ for segs without
         * headroom. 0 means no validation (§4.6 fallback matrix). */
        ctrl_hdr->read_gen = has_sliced_seg ? 0 : gen;
        if (ctrl_hdr->read_gen == 0) {
            g_read_gen_fallback_count.fetch_add(1, std::memory_order_relaxed);
        }
        g_offer_total_count.fetch_add(1, std::memory_order_relaxed);

        /* Compact infos[] to the wire position: Alloc placed infos at
         * segs + UBS_SEG_MAX (offset 528), but the wire format (ParseReadOffer)
         * expects infos immediately after segs[nsegs] (offset 16 + nsegs*16). */
        {
            char *base = reinterpret_cast<char *>(ctrl_hdr);
            uint32_t dst_off = UBS_CTRL_HDR_SIZE + static_cast<uint32_t>(nsegs) * UBS_SEG_SIZE;
            char *dst = base + dst_off;
            char *src = reinterpret_cast<char *>(infos);
            if (dst != src && infos_bytes > 0) {
                (void)memmove(dst, src, static_cast<size_t>(infos_bytes));
            }
        }

        const uint32_t wire = ctrl_hdr->total_len; /* BIG_CTRL region starts at UbsCtrlHdr */
        ctrl->data_size = wire;
        ctrl->total_data_size = wire;
        ConfigureControlSend(ctrl, ctrl_sn, has_sn, /*big_ctrl=*/true);

        {
            PROF_START(UBS_NATIVE_PINNED_SEAL_ONLY);
            Locker lk(state->mutex);
            PinnedEntry &entry = state->pinned[ctrl_hdr_seq];
            entry.blocks = std::move(pinned);
            entry.gen = ctrl_hdr->read_gen; /* 0 if has_sliced_seg */
            entry.headrooms = std::move(headrooms);
            /* Pin reclaim: always set deadline so SweepExpiredPinned can release
             * blocks even when read_gen=0 (sliced / no receiver validation). */
            entry.deadline_ns = deadline_ns;
            entry.stage1_done = false;
            entry.create_ns = Func::CurrentTimeNs(); /* design §9: for alive_max_ms */
            /* design §9 perf: track entries with non-zero deadline for sweep fast-path */
            if (entry.deadline_ns != 0) {
                state->pinned_with_deadline.fetch_add(1, std::memory_order_relaxed);
            }
            PROF_END(UBS_NATIVE_PINNED_SEAL_ONLY, true);
        }
        pinned.clear();
        headrooms.clear();

        umq_buf_t *out = ctrl;
        ctrl = nullptr;        /* caller now owns it until posted + its SEND TX CQE */
        nsegs_at_seal = nsegs; /* remember count for batch accounting */
        nsegs = 0;
        infos_bytes = 0;       /* reset for next offer — must match nsegs reset,
                                * otherwise total_len uses stale accumulated bytes */
        info_cursor = nullptr;
        return out;
    }

    void Rollback()
    {
        for (Block *b : pinned) {
            if (b != nullptr) {
                b->DecRef();
            }
        }
        pinned.clear();
        headrooms.clear();
        has_sliced_seg = false;
        if (ctrl != nullptr) {
            FreeQbufChain(ctrl);
            ctrl = nullptr;
        }
    }
};

/* =========================================================================
 * Receiver path (design §6): parse READ_OFFER, import mempools, post RDMA READ
 * WRs, finalize on completion, deliver SN-ordered, ack the sender.
 * ========================================================================= */

/*
 * READ_OFFER data-region layout (design §4):
 *   UbsCtrlHdr | UbsSeg[nsegs] | ub_mempool_info_t[variable] | inline
 * The mempool_info region is variable-length: entry i is UbsSeg[i].mempool_info_len
 * bytes (it carries the urma_get_seg_ctx seg blob, which includes a has_user_info
 * extension tail on bonding devices). The receiver steps by mempool_info_len,
 * not a fixed stride. Here inline_data_len == 0, so the layout ends after the
 * last mempool_info entry.
 */
struct UbsReadOfferView {
    const UbsSeg *segs;
    const uint8_t *infos; /* region base; entry i at infos + Σ_{j<i} segs[j].mempool_info_len */
    uint32_t infos_bytes;          /* total byte length of the mempool_info region */
};

bool ParseReadOffer(const UbsCtrlHdr *ctrl, const umq_buf_t *qbuf, UbsReadOfferView *view)
{
    if (ctrl == nullptr || qbuf == nullptr || view == nullptr) {
        UBS_VLOG_ERR("ParseReadOffer null parameter.\n");
        return false;
    }
    if (!proto::ctrl_offer_counts_valid(ctrl->nsegs, ctrl->nmempool_infos)) {
        UBS_VLOG_ERR(
            "ParseReadOffer invalid counts, nsegs: %u, nmempool_infos: %u, type: %u, total_len: %u, data_size: %u\n",
            ctrl->nsegs, ctrl->nmempool_infos, ctrl->type, ctrl->total_len, qbuf->data_size);
        return false;
    }
    const uint16_t nsegs = ctrl->nsegs;
    if (ctrl->inline_data_len != 0) {
        UBS_VLOG_ERR("ParseReadOffer non-zero inline_data_len.\n");
        return false;
    }
    /* The data region starts with UbsCtrlHdr; UbsSeg[] follows immediately. */
    const uint32_t segs_off = UBS_CTRL_HDR_SIZE;
    const uint32_t infos_off = segs_off + static_cast<uint32_t>(nsegs) * UBS_SEG_SIZE;
    if (qbuf->data_size < infos_off) {
        UBS_VLOG_ERR("ParseReadOffer data_size too small for segs, data_size: %u, infos_off: %u\n",
                     qbuf->data_size, infos_off);
        return false;
    }
    view->segs = reinterpret_cast<const UbsSeg *>(qbuf->buf_data + segs_off);

    /* Strict 1:1 index invariant (design §3.4 / §4), and accumulate the
     * variable-length mempool_info region byte length from UbsSeg[i].mempool_info_len. */
    uint32_t infos_bytes = 0;
    for (uint16_t i = 0; i < nsegs; ++i) {
        if (view->segs[i].mempool_info_idx != static_cast<uint8_t>(i)) {
            UBS_VLOG_ERR("ParseReadOffer mempool_info_idx mismatch, i: %u, mempool_info_idx: %u\n", i,
                         view->segs[i].mempool_info_idx);
            return false;
        }
        const uint16_t info_len = view->segs[i].mempool_info_len;
        if (info_len < UBS_MEMPOOL_INFO_HDR_SIZE + UBS_URMA_SEG_T_SIZE) {
            UBS_VLOG_ERR("ParseReadOffer mempool_info_len too small, i: %u, len: %u\n", i, info_len);
            return false;
        }
        /* Bound against the buffer end to avoid overflow while summing. */
        if (infos_bytes > qbuf->data_size - infos_off - info_len) {
            UBS_VLOG_ERR("ParseReadOffer mempool_info region overruns buffer, i: %u, infos_bytes: %u, len: %u\n",
                         i, infos_bytes, info_len);
            return false;
        }
        infos_bytes += info_len;
    }

    /* total_len must exactly cover UbsCtrlHdr + UbsSeg[] + mempool_info[] with
     * no inline data (design §4: inline_data_len == 0 on the offer). */
    const uint32_t expected = proto::ctrl_total_len(nsegs, infos_bytes, 0);
    if (ctrl->total_len != static_cast<uint16_t>(expected)) {
        UBS_VLOG_ERR("ParseReadOffer total_len mismatch, total_len: %u, expected: %u\n", ctrl->total_len, expected);
        return false;
    }
    const uint32_t end = infos_off + infos_bytes;
    if (qbuf->data_size < end) {
        UBS_VLOG_ERR("ParseReadOffer data_size too small, data_size: %u, end: %u\n", qbuf->data_size, end);
        return false;
    }
    view->infos = reinterpret_cast<const uint8_t *>(qbuf->buf_data + infos_off);
    view->infos_bytes = infos_bytes;
    return true;
}

/* Drain EAGAIN-paused control packets (design §9). Called from
 * HandleFlowControlUpdate and after each bigdata TX completion. Each retry
 * re-posts; EAGAIN keeps the entry, other failures drop it. */
void DrainDeferredControls(UbsBigdataSocketState *state)
{
    PROF_START(UBS_NATIVE_DRAIN_DEFERRED_CTRL);
    if (state == nullptr) {
        PROF_END(UBS_NATIVE_DRAIN_DEFERRED_CTRL, true);
        return;
    }
    for (;;) {
        UbsDeferredCtrl item{};
        {
            Locker lk(state->mutex);
            if (state->DeferredCtrlEmpty()) {
                PROF_END(UBS_NATIVE_DRAIN_DEFERRED_CTRL, true);
                return;
            }
            item = state->deferred_ctrl->front();
            state->deferred_ctrl->pop_front();
        }
        umq_buf_t *bad = nullptr;
        umq_io_option_t opt = TxOption();
        int ret = UmqApi::umq_post(item.umqh, item.qbuf, &opt, &bad);
        const int savedErrno = errno; /* save immediately: success/retry branches may clobber errno (AGENTS.md) */
        if (ret == 0) {
            UBS_DATAPATH_LOG("[datapath] deferred ctrl reposted (big), fd: %d\n", item.fd);
            /* Account for the control SEND WR just posted to the SQ. The
             * matching CQE does fetch_add(1) via HandleTxCompletion's
             * BIG_CTRL branch (deferred_progress_fd) when bit 20 is
             * preserved, or via ProcessTxCqe when bit 20 is stripped. */
            auto sock = ArraySet<Socket>::GetInstance().GetItem(item.fd);
            if (sock.Get() != nullptr) {
                auto umq_sk = RefStaticCast<UmqSocket>(sock);
                auto *tx_ops = umq_sk->GetTxOps();
                if (tx_ops != nullptr) {
                    tx_ops->tx_queue_avail_num_.fetch_sub(1, std::memory_order_acq_rel);
                }
                TxCqePoller::Instance().MarkActive(sock);
            }
            /* Same-thread (RX thread): mark inflight only, no eventfd wake. */
            if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
                TxCqePoller::Instance().NotifyInflight();
            }
            continue; /* posted: UMQ owns it now until its SEND TX CQE */
        }
        if (ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
            ret == -UMQ_ERR_EMLINK) {
            /* Still paused (EAGAIN/ENOBUFS/ENOMEM/EMLINK): put it back at the
             * head and stop this drain. ENOMEM from the URMA driver (SQ full /
             * WR alloc failure) is transient and resolves the same way as
             * EAGAIN/ENOBUFS by reaping TX completions which free SQ slots.
             * EMLINK means no free jetty in the transport pool; it is also
             * transient and resolves as jettys are released by TX completions
             * (UmqTpWaitQueue::WakeUp). */
            Locker lk(state->mutex);
            state->deferred_ctrl->push_front(std::move(item));
            UBS_DATAPATH_LOG("[datapath] deferred ctrl still paused (big), fd: %d, ret: %d\n", item.fd, ret);
            PROF_END(UBS_NATIVE_DRAIN_DEFERRED_CTRL, true);
            return;
        }
        UBS_VLOG_ERR("DrainDeferredControls retry failed, fd: %d, ret: %d, errno: %d, desc: %s\n", item.fd, ret,
                     umq::UmqErrnoConverter::Convert(umq::UmqOperation::WRITEV, ret, savedErrno),
                     umq::UmqErrnoConverter::GetErrorDescription(umq::UmqOperation::WRITEV, ret));
        FreeQbufChain(item.qbuf);
    }
}

/* Build and post a minimal control message (READ_DONE / READ_ABORT). The data
 * region is just UbsCtrlHdr (design §3.3: DONE/ABORT carry no variable region),
 * classified as BIG_CTRL via imm_data bit 20. seq echoes the offer so the sender
 * can unpin. On EAGAIN/ENOBUFS/ENOMEM the packet enters the deferred-ctrl queue
 * for bounded retry (design §9). ENOMEM from the URMA driver (SQ full / WR alloc
 * failure) is transient and resolves the same way as EAGAIN/ENOBUFS by reaping
 * TX completions which free SQ slots. */
void SendSimpleCtrl(UbsBigdataSocketState *state, int fd, uint64_t umqh, uint8_t ctrl_type, uint64_t seq,
                    const SocketPtr &tx_sock = SocketPtr())
{
    PROF_START(UBS_NATIVE_SEND_SIMPLE_CTRL);
    /* Sub-stage split (FinalizeIo p99 long-tail breakdown): umq_buf_alloc
     * cold-path can hit TLB miss (1-2us). Sibling block (NOT nested in
     * UBS_NATIVE_SEND_SIMPLE_CTRL). */
    PROF_START(UBS_NATIVE_FINALIZE_DONE_ALLOC);
    umq_buf_t *ctrl = UmqApi::umq_buf_alloc(UBS_CTRL_HDR_SIZE, 1, umqh, nullptr);
    PROF_END(UBS_NATIVE_FINALIZE_DONE_ALLOC, ctrl != nullptr);
    if (ctrl == nullptr) {
        PROF_END(UBS_NATIVE_SEND_SIMPLE_CTRL, false);
        UBS_VLOG_ERR("SendSimpleCtrl alloc failed, fd: %d, seq: %llu, type: %u\n", fd,
                     static_cast<unsigned long long>(seq), ctrl_type);
        return;
    }
    auto *chdr = reinterpret_cast<UbsCtrlHdr *>(ctrl->buf_data);
    (void)memset(chdr, 0, sizeof(*chdr));
    chdr->type = ctrl_type;
    chdr->nsegs = 0;
    chdr->nmempool_infos = 0;
    chdr->inline_data_len = 0;
    chdr->total_len = UBS_CTRL_HDR_SIZE;
    chdr->seq = seq;
    const uint32_t wire = UBS_CTRL_HDR_SIZE;
    ctrl->data_size = wire;
    ctrl->total_data_size = wire;
    ConfigureControlSend(ctrl, 0, false, /*big_ctrl=*/true); /* DONE/ABORT do not carry a data SN */
    umq_buf_t *bad = nullptr;
    umq_io_option_t opt = TxOption();
    /* Sub-stage split: umq_post doorbell write (SQ ring write + MMIO).
     * Sibling block (NOT nested in UBS_NATIVE_SEND_SIMPLE_CTRL). */
    PROF_START(UBS_NATIVE_FINALIZE_DONE_POST);
    int ret = UmqApi::umq_post(umqh, ctrl, &opt, &bad);
    const int savedErrno = errno; /* save immediately: PROF_END / lock scope may clobber errno (AGENTS.md) */
    PROF_END(UBS_NATIVE_FINALIZE_DONE_POST, ret == 0);
    if (ret == 0) {
        UBS_DATAPATH_LOG("[datapath] ctrl SEND posted (big), fd: %d, seq: %llu, type: %u\n", fd,
                         static_cast<unsigned long long>(seq), ctrl_type);
        /* Account for the control SEND WR just posted to the SQ; its CQE
         * will fetch_add(1) via HandleTxCompletion BIG_CTRL or ProcessTxCqe.
         * Use the pre-resolved socket (when provided) to avoid a per-RPC
         * ArraySet<Socket>::GetItem(fd) cold-cache lookup. */
        SocketPtr sock = tx_sock;
        if (sock.Get() == nullptr) {
            sock = ArraySet<Socket>::GetInstance().GetItem(fd);
        }
        if (sock.Get() != nullptr) {
            auto umq_sk = RefStaticCast<UmqSocket>(sock);
            auto *tx_ops = umq_sk->GetTxOps();
            if (tx_ops != nullptr) {
                tx_ops->tx_queue_avail_num_.fetch_sub(1, std::memory_order_acq_rel);
            }
            TxCqePoller::Instance().MarkActive(sock);
            if (GlobalSetting::UBS_MONITOR_ENABLE) {
                if (auto *mgr = umq_sk->GetStatsMgr()) {
                    mgr->UpdateTraceStats(Statistics::StatsMgr::BIGDATA_CTRL_SEND_COUNT, 1);
                }
            }
        }
        /* Same-thread (RX thread): mark inflight only, no eventfd wake. */
        if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().NotifyInflight();
        }
        PROF_END(UBS_NATIVE_SEND_SIMPLE_CTRL, true);
        return;
    }
    if ((ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
         ret == -UMQ_ERR_EMLINK) &&
        state != nullptr) {
        Locker lk(state->mutex);
        std::deque<UbsDeferredCtrl> *dq = state->EnsureDeferredCtrl();
        if (dq == nullptr) {
            /* 惰性 deque 分配失败：无处可延迟，按打满同路丢弃本包，靠 RPC 超时兜底 */
            UBS_DATAPATH_LOG("[datapath] ctrl deferred queue alloc failed, drop (big), fd: %d, seq: %llu, type: %u\n",
                             fd, static_cast<unsigned long long>(seq), ctrl_type);
            FreeQbufChain(ctrl);
            PROF_END(UBS_NATIVE_SEND_SIMPLE_CTRL, true);
            return;
        }
        if (dq->size() >= UBS_BIG_DEFERRED_CTRL_MAX) {
            /* Bounded queue full: drop the oldest, rely on RPC timeout. */
            UBS_DATAPATH_LOG("[datapath] ctrl deferred queue full, drop oldest (big), fd: %d, seq: %llu, type: %u\n",
                             fd, static_cast<unsigned long long>(seq), ctrl_type);
            UbsDeferredCtrl &oldest = dq->front();
            FreeQbufChain(oldest.qbuf);
            dq->pop_front();
        }
        dq->push_back(UbsDeferredCtrl{fd, umqh, ctrl});
        UBS_DATAPATH_LOG("[datapath] ctrl SEND deferred (big), fd: %d, seq: %llu, type: %u, ret: %d\n", fd,
                         static_cast<unsigned long long>(seq), ctrl_type, ret);
        PROF_END(UBS_NATIVE_SEND_SIMPLE_CTRL, true);
        return;
    }
    const int mappedErrno = umq::UmqErrnoConverter::Convert(umq::UmqOperation::WRITEV, ret, savedErrno);
    UBS_VLOG_ERR("SendSimpleCtrl post failed, fd: %d, seq: %llu, type: %u, ret: %d, errno: %d, state: %p, "
                 "desc: %s\n",
                 fd, static_cast<unsigned long long>(seq), ctrl_type, ret, mappedErrno, state,
                 umq::UmqErrnoConverter::GetErrorDescription(umq::UmqOperation::WRITEV, ret));
    errno = mappedErrno;
    FreeQbufChain(ctrl);
    PROF_END(UBS_NATIVE_SEND_SIMPLE_CTRL, false);
}

/* Deliver a completed READ chain to the receive queue (design §6.4). The chain
 * shares one SN (first_sn); the head is enqueued as a single entry with its
 * qbuf_next chain intact so that the OOO (EnqueueInOrder) path sees one SN
 * and processes it correctly. ubs_poll later expands the chain into multiple
 * segments for the caller; a too-long chain is partially consumed and the
 * remainder stashed on the socket for the next poll. */
bool DeliverToRxQueue(const SocketPtr &sock, umq_buf_t *head)
{
    PROF_START(UBS_NATIVE_DELIVER_TO_RX_QUEUE);
    auto umq = RefConvert<Socket, UmqSocket>(sock);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (umq == nullptr || sockBase == nullptr || head == nullptr) {
        FreeQbufChain(head);
        PROF_END(UBS_NATIVE_DELIVER_TO_RX_QUEUE, false);
        return false;
    }
    for (umq_buf_t *b = head; b != nullptr; b = b->qbuf_next) {
        b->io_direction = UMQ_IO_RX;
    }
    UbsStageTrace(sock->raw_socket_, reinterpret_cast<umq_buf_pro_t *>(head->qbuf_ext)->imm.user_data,
                  STAGE_DELIVER_TO_RX_QUEUE, UbsPktTraceNowNs());
    /* Sub-stage split (FinalizeIo p99 long-tail breakdown): lock + rxQueue->Enqueue
     * (OOO path: SN normalize + meltdown check + PushChainToRing). Sibling block
     * (NOT nested in UBS_NATIVE_DELIVER_TO_RX_QUEUE) so the two histograms
     * sum to the superset total. */
    PROF_START(UBS_NATIVE_FINALIZE_DELIVER_ENQ);
    const bool enq_ok = (umq->AddQbuf(head) == UBS_OK);
    PROF_END(UBS_NATIVE_FINALIZE_DELIVER_ENQ, enq_ok);
    if (!enq_ok) {
        UBS_VLOG_ERR("DeliverToRxQueue enqueue failed, fd: %d, errno: %d\n", sock->raw_socket_, errno);
        PROF_END(UBS_NATIVE_DELIVER_TO_RX_QUEUE, false);
        return false;
    }
    /* Sub-stage split: NotifyReadable (TryDirectDispatchEvent fast-path OR
     * eventfd_write / epoll slow-path). Wake contention is a major source of
     * the 13.6us p99 in FinalizeIo when the target worker is busy. */
    PROF_START(UBS_NATIVE_FINALIZE_DELIVER_WAKE);
    (void)sockBase->NotifyReadable();
    UBS_DATAPATH_LOG("[datapath] DeliverToRxQueue enqueued + notify readable (big), fd: %d, first_sn: %u\n",
                     sock->raw_socket_, reinterpret_cast<umq_buf_pro_t *>(head->qbuf_ext)->imm.user_data);
    PROF_END(UBS_NATIVE_FINALIZE_DELIVER_WAKE, true);
    PROF_END(UBS_NATIVE_DELIVER_TO_RX_QUEUE, true);
    return true;
}

/* Completion bookkeeping (design §6.3): the terminal ordered READ CQE accounts
 * for all WRs in its span. When posting is done and every WR has reached a
 * terminal CQE, FinalizeIo runs. */
constexpr uint32_t UBS_BIG_POSTING_DONE = 1U << 31;
constexpr uint32_t UBS_BIG_COMPLETION_COUNT_MASK = UBS_BIG_POSTING_DONE - 1;

struct BigDataCompletionProgress {
    uint32_t completed;
    bool finalize;
};

void FinalizeIo(UbsBigIoCtx *ctx);

/* RAII ref on UbsBigdataSocketState: keeps the state alive while this thread
 * (DoReadOffer / HandleTxCompletion / HandleFlowControlUpdate / TrySenderPost)
 * accesses it concurrently with CleanupSocketState's owner-ref release. The
 * last DecreaseRef deletes the state. */
class BigdataStateRef {
public:
    explicit BigdataStateRef(UbsBigdataSocketState *state) : state_(state)
    {
        if (state_ != nullptr) {
            state_->IncreaseRef();
        }
    }

    ~BigdataStateRef()
    {
        if (state_ != nullptr) {
            state_->DecreaseRef();
        }
    }

    BigdataStateRef(const BigdataStateRef &) = delete;
    BigdataStateRef &operator=(const BigdataStateRef &) = delete;

    UbsBigdataSocketState *Get() const noexcept
    {
        return state_;
    }

private:
    UbsBigdataSocketState *state_;
};

/* Delete an IoCtx and release its state ref. The state is deleted by the
 * last DecreaseRef (teardown dropped the owner ref and no other ctx is
 * alive). Must be the ctx's final operation: nothing may touch ctx or
 * ctx->state afterwards. */
static void DeleteIoUctx(UbsBigIoCtx *ctx)
{
    UbsBigdataSocketState *state = ctx->state;
    /* design §9 perf: decrement fast-path counter if this ctx had a deadline.
     * Covers both pending_reads sweep (to_delete path) and active_io finalize
     * (FinalizeIo path) — single chokepoint for all ctx deletions. */
    if (state != nullptr && ctx->deadline_ns != 0) {
        state->ctx_with_deadline.fetch_sub(1, std::memory_order_relaxed);
    }
    ctx->state = nullptr;
    delete ctx;
    if (state != nullptr) {
        state->DecreaseRef();
    }
}

ALWAYS_INLINE BigDataCompletionProgress MarkDataWrComplete(UbsBigIoCtx *ctx, uint32_t count)
{
    const uint32_t previous = ctx->wr_state.fetch_add(count, std::memory_order_acq_rel);
    const uint32_t completed = (previous & UBS_BIG_COMPLETION_COUNT_MASK) + count;
    const bool finalize = (previous & UBS_BIG_POSTING_DONE) != 0 && completed == ctx->wr_total;
    return BigDataCompletionProgress{completed, finalize};
}

ALWAYS_INLINE void FinishPosting(UbsBigIoCtx *ctx, bool failed)
{
    if (failed) {
        ctx->failed.store(true, std::memory_order_release);
    }
    ctx->wr_total = static_cast<uint32_t>(ctx->next_post);
    const uint32_t wr_total = ctx->wr_total;
    const uint32_t previous = ctx->wr_state.fetch_or(UBS_BIG_POSTING_DONE, std::memory_order_acq_rel);
    if ((previous & UBS_BIG_COMPLETION_COUNT_MASK) == wr_total) {
        FinalizeIo(ctx);
    }
}

void FinalizeIo(UbsBigIoCtx *ctx)
{
    /* Guard against double finalize: MarkDataWrComplete (terminal READ CQE)
     * and FinishPosting (post-completion) can both observe the final
     * completion count and both call FinalizeIo. Only the first may run;
     * a second run would re-link read_head and double-free the READ
     * destination buffers via DeliverToRxQueue (design §6.3). */
    bool expected = false;
    if (!ctx->finalized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        /* Double-finalize guard: second call returns immediately without
         * recording a fresh PROF entry (the first call already accounted). */
        return;
    }
    UbsStageTrace(ctx->fd, ctx->first_sn, STAGE_FINALIZE_IO, UbsPktTraceNowNs());
    PROF_START(UBS_NATIVE_FINALIZE_IO);
    umq_buf_t *read_head = nullptr;
    umq_buf_t *read_tail = nullptr;
    /* Sub-stage split (FinalizeIo p99 long-tail breakdown): walk the READ WR
     * chain and link them into a single delivery list. Usually wr_total=1 so
     * this is one iteration; multi-WR offers are bucketed to single-WR by the
     * Fast path in DoReadOffer so this stays cheap. */
    PROF_START(UBS_NATIVE_FINALIZE_LINK);
    if (ctx->wr_total != 0 && !ctx->failed.load(std::memory_order_acquire)) {
        if (!detail::LinkReadQbufsInOrder(ctx->wr_slots.data(), ctx->wr_total, &read_head, &read_tail)) {
            UBS_VLOG_ERR("Finalize READ invalid slot chain, fd: %d, seq: %llu, wr_total: %u\n", ctx->fd,
                         static_cast<unsigned long long>(ctx->seq), ctx->wr_total);
            ctx->failed.store(true, std::memory_order_release);
        }
    }
    PROF_END(UBS_NATIVE_FINALIZE_LINK, true);

    /* Sub-stage split: state->mutex (pthread_mutex_t, per-socket shared by
     * all concurrent RPCs on this connection) + unordered_set erase +
     * destroying flag check. Lock contention is the primary p99 culprit
     * on single-connection multi-bthread workloads. */
    {
        /* If state was detached (socket cleanup), skip state access entirely. */
        if (ctx->state == nullptr) {
            PROF_START(UBS_NATIVE_FINALIZE_STATE_LOCK);
            PROF_END(UBS_NATIVE_FINALIZE_STATE_LOCK, true);
            for (auto &slot : ctx->wr_slots) {
                if (slot.pending != nullptr) {
                    slot.pending->qbuf_next = nullptr; /* detach inter-slot chain */
                }
                FreeQbufChain(slot.pending);
                slot.pending = nullptr;
            }
            FreeQbufChain(ctx->offer_rx_buf);
            DeleteIoUctx(ctx);
            PROF_END(UBS_NATIVE_FINALIZE_IO, true);
            return;
        }
        PROF_START(UBS_NATIVE_FINALIZE_STATE_LOCK);
        {
            Locker lk(ctx->state->mutex);
            ctx->state->active_io.erase(ctx);
            if (ctx->state->destroying.load(std::memory_order_relaxed)) {
                /* State is being torn down: free resources without state access. */
                for (auto &slot : ctx->wr_slots) {
                    if (slot.pending != nullptr) {
                        slot.pending->qbuf_next = nullptr; /* detach inter-slot chain */
                    }
                    FreeQbufChain(slot.pending);
                    slot.pending = nullptr;
                }
                FreeQbufChain(ctx->offer_rx_buf);
            }
        }
        PROF_END(UBS_NATIVE_FINALIZE_STATE_LOCK, true);
        if (ctx->state->destroying.load(std::memory_order_relaxed)) {
            /* DeleteIoUctx must run OUTSIDE the lock: it calls
             * state->DecreaseRef() which may delete state (and its
             * mutex) when this is the last ref. Deleting state while
             * the Locker still holds its mutex would crash in the
             * Locker destructor's unlock. */
            DeleteIoUctx(ctx);
            PROF_END(UBS_NATIVE_FINALIZE_IO, true);
            return;
        }
    }

    /* Free the retained OFFER RX buffer instead of reposting it. The
     * UmqShareJfrEpollRunnerOps already posted a replacement RX buffer when
     * the OFFER's CQE was processed (it counts every polled buffer, including
     * control messages consumed by HandleRxControl). Reposting here would
     * double-post and exhaust the rx_buf_ctx pool, causing "rx buf ctx is
     * used up" and blocking the RX refill path. */
    auto repost_offer_rx_buf = [ctx]() {
        umq_buf_t *rx = ctx->offer_rx_buf;
        ctx->offer_rx_buf = nullptr;
        if (rx != nullptr) {
            FreeQbufChain(rx);
        }
    };

    if (ctx->failed.load(std::memory_order_acquire)) {
        /* All submitted READs have reached a terminal CQE (MarkDataWrComplete
         * only finalizes once completed == wr_total), so the "drain remaining
         * in-flight READs" requirement (design §6.3) is satisfied: there is
         * nothing left to cancel or wait for. UMQ does not expose an in-flight
         * WR cancel, so drain (wait for terminal CQEs) is the chosen path.
         * Now release destinations, ABORT, repost RX, and disconnect. */
        UBS_DATAPATH_LOG("[datapath] READ finalize FAILED (big), fd: %d, seq: %llu, wr_total: %u\n", ctx->fd,
                         static_cast<unsigned long long>(ctx->seq), ctx->wr_total);
        for (auto &slot : ctx->wr_slots) {
            if (slot.pending != nullptr) {
                slot.pending->qbuf_next = nullptr; /* detach inter-slot chain */
            }
            FreeQbufChain(slot.pending);
            slot.pending = nullptr;
        }
        SendSimpleCtrl(ctx->state, ctx->fd, ctx->umqh, UBS_READ_ABORT, ctx->seq, ctx->recv_sock);
        repost_offer_rx_buf();
        /* Disconnect: a failed READ transaction indicates a hardware/protocol
         * error; mark the socket closed so the upper layer tears down. */
        SocketPtr failed_sock = ctx->recv_sock;
        if (failed_sock == nullptr) {
            failed_sock = ArraySet<Socket>::GetInstance().GetItem(ctx->fd);
        }
        if (failed_sock != nullptr) {
            failed_sock->State(SOCK_STAT_CLOSE);
        }
        DeleteIoUctx(ctx);
        PROF_END(UBS_NATIVE_FINALIZE_IO, true);
        return;
    }

    /* design §4.4: gen validation. When expect_gen != 0, each READ dest buf
     * starts with the 8B headroom gen from the sender. Validate all match
     * expect_gen; any mismatch → stale READ after sender timeout, discard. */
    if (ctx->expect_gen != 0) {
        bool gen_mismatch = false;
        for (uint32_t i = 0; i < ctx->wr_total; ++i) {
            umq_buf_t *b = ctx->wr_slots[i].pending;
            if (b == nullptr || b->data_size < 8) {
                gen_mismatch = true;
                break;
            }
            uint64_t got = *reinterpret_cast<const uint64_t *>(b->buf_data);
            if (got != ctx->expect_gen) {
                UBS_VLOG_WARN("read gen mismatch (stale READ), fd: %d, seq: %llu, slot: %u, expect: %llu, got: %llu\n",
                              ctx->fd, static_cast<unsigned long long>(ctx->seq), i,
                              static_cast<unsigned long long>(ctx->expect_gen),
                              static_cast<unsigned long long>(got));
                g_read_gen_mismatch_count.fetch_add(1, std::memory_order_relaxed);
                gen_mismatch = true;
                break;
            }
        }
        if (gen_mismatch) {
            /* Discard: free all read bufs, free offer_rx_buf, no ABORT (sender
             * already timed out), no disconnect (not a hardware error). */
            for (auto &slot : ctx->wr_slots) {
                if (slot.pending != nullptr) {
                    slot.pending->qbuf_next = nullptr;
                    FreeQbufChain(slot.pending);
                    slot.pending = nullptr;
                }
            }
            read_head = nullptr;
            repost_offer_rx_buf();
            DeleteIoUctx(ctx);
            PROF_END(UBS_NATIVE_FINALIZE_IO, true);
            return;
        }
        /* All gens match: strip 8B headroom from each buf before delivery */
        for (uint32_t i = 0; i < ctx->wr_total; ++i) {
            umq_buf_t *b = ctx->wr_slots[i].pending;
            if (b != nullptr) {
                b->buf_data += 8;
                b->data_size -= 8;
                b->total_data_size -= 8;
            }
        }
    }

    /* All fragments of one READ_OFFER share a single connection-level SN
     * (first_sn). The offer reserved exactly one SN (FetchAddSeqNum(1) in
     * AllocNewOffer), so stamping first_sn+i would overrun the reservation
     * and collide with the next RPC's SN. DeliverToRxQueue hands the whole
     * chain to AddQbuf as one head; EnqueueInOrder judges the SN once on the
     * head and PushChainToRing pushes each fragment into the ring, after which
     * they never re-enter the SN ordering logic — so sharing one SN is safe
     * and matches the rxQueue expect+=1 progression. SN is already a 24-bit
     * normalized value (DoReadOffer read it from imm.user_data), no wrap
     * needed. */
    for (uint32_t i = 0; i < ctx->wr_total; ++i) {
        umq_buf_t *b = ctx->wr_slots[i].pending;
        ctx->wr_slots[i].pending = nullptr;
        if (b != nullptr) {
            auto *pro = reinterpret_cast<umq_buf_pro_t *>(b->qbuf_ext);
            pro->imm.user_data = ctx->first_sn;
        }
    }

    bool delivered = false;
    /* Sub-stage split: use the SocketPtr captured at DoReadOffer time
     * (ctx->recv_sock) instead of re-resolving ArraySet<Socket>::GetItem(fd)
     * on every READ — at low QPS the lookup is a cold-cache memory round-trip.
     * Falls back to a resolve only if the socket disappeared (teardown). */
    PROF_START(UBS_NATIVE_FINALIZE_ARRAYSET_GET);
    SocketPtr recv_sock = ctx->recv_sock;
    if (recv_sock.Get() == nullptr) {
        recv_sock = ArraySet<Socket>::GetInstance().GetItem(ctx->fd);
    }
    PROF_END(UBS_NATIVE_FINALIZE_ARRAYSET_GET, recv_sock != nullptr);
    if (recv_sock != nullptr) {
        delivered = DeliverToRxQueue(recv_sock, read_head);
    } else {
        FreeQbufChain(read_head);
    }
    UBS_DATAPATH_LOG("[datapath] READ finalize (big), fd: %d, seq: %llu, wr_total: %u, first_sn: %u, delivered: %d\n",
                     ctx->fd, static_cast<unsigned long long>(ctx->seq), ctx->wr_total, ctx->first_sn,
                     static_cast<int>(delivered));
    UbsStageTrace(ctx->fd, ctx->first_sn, STAGE_SEND_SIMPLE_CTRL, UbsPktTraceNowNs());
    SendSimpleCtrl(ctx->state, ctx->fd, ctx->umqh, delivered ? UBS_READ_DONE : UBS_READ_ABORT, ctx->seq, recv_sock);
    repost_offer_rx_buf();
    DeleteIoUctx(ctx);
    PROF_END(UBS_NATIVE_FINALIZE_IO, true);
}

/*
 * Handle a READ_OFFER: validate, import every carried mempool (design §3.5),
 * allocate one local READ WR per UbsSeg, configure ordered completions, post.
 * The offer's first_sn plus per-seg index maps each READ to its SN.
 */
void DoReadOffer(const SocketPtr &sock, uint64_t umqh, umq_buf_t *qbuf)
{
    PROF_START(UBS_NATIVE_DO_READ_OFFER);
    const int recv_fd = sock->raw_socket_;
    UbsBigdataSocketState *state = GetBigdataState(sock, true);
    /* RAII: free the OFFER RX buffer on any early return before the transaction
     * ctx takes ownership of it. Once ctx->offer_rx_buf = qbuf, ownership moves
     * to the ctx and FinalizeIo reposts it (design §6.1). */
    bool offer_owned = true;
    auto free_offer_on_return = [&]() {
        if (offer_owned && qbuf != nullptr) {
            FreeQbufChain(qbuf);
            offer_owned = false;
        }
    };
    if (state == nullptr) {
        free_offer_on_return();
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    /* Keep the state alive for this whole function: the socket may be closed
     * concurrently (CleanupSocketState drops the owner ref and may delete the
     * state once no ctx holds a ref). */
    BigdataStateRef state_ref(state);
    if (qbuf->data_size < UBS_CTRL_HDR_SIZE) {
        SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, 0, sock);
        free_offer_on_return();
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    auto *ctrl = reinterpret_cast<const UbsCtrlHdr *>(qbuf->buf_data);
    if (ctrl->type != UBS_READ_OFFER) {
        free_offer_on_return();
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    UbsReadOfferView offer{};
    PROF_START(UBS_NATIVE_PARSE_READ_OFFER);
    const bool parsed = ParseReadOffer(ctrl, qbuf, &offer);
    PROF_END(UBS_NATIVE_PARSE_READ_OFFER, parsed);
    if (!parsed) {
        UBS_VLOG_WARN("DoReadOffer rejected malformed offer, fd: %d, seq: %llu\n", recv_fd,
                      static_cast<unsigned long long>(ctrl->seq));
        SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
        free_offer_on_return();
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    const uint16_t nsegs = ctrl->nsegs;
    /* A READ_OFFER carries one connection-level SN in imm.user_data (high 24
     * bits). All fragments of this offer share this single SN (the offer
     * reserved exactly one SN via FetchAddSeqNum(1) at the sender); FinalizeIo
     * stamps every fragment with first_sn and the rxQueue advances expect by
     * exactly one for the whole chain. Classification is by imm_data bit 20,
     * so there is no HAS_SN flag to test. */
    const uint32_t first_sn = reinterpret_cast<const umq_buf_pro_t *>(qbuf->qbuf_ext)->imm.user_data;
    UbsStageTrace(recv_fd, first_sn, STAGE_DO_READ_OFFER, UbsPktTraceNowNs());
    UBS_DATAPATH_LOG("[datapath] DoReadOffer parsed, fd: %d, seq: %llu, first_sn: %u, nsegs: %u\n", recv_fd,
                     static_cast<unsigned long long>(ctrl->seq), first_sn, nsegs);

    /* Import every carried mempool per its version (design §3.5). The backend
     * returns UBS_REMOTE_MEMPOOL_STATE_REUSE = cached version matches, UBS_REMOTE_MEMPOOL_STATE_NEED_IMPORT = need import,
     * UBS_REMOTE_MEMPOOL_STATE_NEED_REIMPORT = version grew (unimport & re-import)
     * UBS_REMOTE_MEMPOOL_STATE_ERR = error.
     * through umq_mempool_info_set, which records the carried version on the
     * imported entry for the next decision. */
    PROF_START(UBS_NATIVE_MEMPOOL_IMPORT);
    const uint8_t *info_ptr = reinterpret_cast<const uint8_t *>(offer.infos);
    for (uint16_t i = 0; i < nsegs; ++i) {
        if (state->destroying.load(std::memory_order_acquire)) {
            free_offer_on_return();
            PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
            return;
        }
        const uint8_t *info = info_ptr;
        const uint16_t info_len = offer.segs[i].mempool_info_len;
        int st = UmqApi::umq_remote_mempool_state_check(umqh, info, info_len);
        UBS_DATAPATH_LOG("[datapath] mempool state, fd: %d, seq: %llu, slot: %u, info_len: %u, state: %d\n",
                         recv_fd, static_cast<unsigned long long>(ctrl->seq), i, info_len, st);
        if (st == UMQ_REMOTE_MEMPOOL_STATE_REUSE) {
            info_ptr += info_len;
            continue;
        }
        if (st == UMQ_REMOTE_MEMPOOL_STATE_ERR) {
            UBS_VLOG_ERR("DoReadOffer mempool state query failed, fd: %d, seq: %llu, info_len: %u\n", recv_fd,
                         static_cast<unsigned long long>(ctrl->seq), info_len);
            SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
            free_offer_on_return();
            return;
        }
        /* Forward the wire blob verbatim to umq for import — ubsocket does not
         * parse or reassemble fields (umq reads mempool_id/version/seg internally). */
        if (UmqApi::umq_mempool_info_set(umqh, info, info_len) != 0) {
            UBS_VLOG_ERR("DoReadOffer mempool import failed, fd: %d, seq: %llu, info_len: %u\n", recv_fd,
                         static_cast<unsigned long long>(ctrl->seq), info_len);
            SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
            free_offer_on_return();
            return;
        }
        info_ptr += info_len;
    }
    PROF_END(UBS_NATIVE_MEMPOOL_IMPORT, true);

    auto *ctx = new (std::nothrow) UbsBigIoCtx{};
    if (ctx == nullptr) {
        SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
        free_offer_on_return();
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    ctx->state = state;
    state->IncreaseRef(); /* ctx holds a state ref until DeleteIoUctx (FinalizeIo / cleanup) */
    ctx->recv_sock = sock;
    ctx->fd = recv_fd;
    ctx->umqh = umqh;
    ctx->seq = ctrl->seq;
    ctx->first_sn = first_sn;
    ctx->offer_rx_buf = qbuf; /* retained until FinalizeIo reposts it (§6.1) */
    offer_owned = false;      /* ctx now owns the OFFER RX buffer */
    ctx->wr_slots.reserve(nsegs);

    /* design §4.4/§4.5: store expect_gen for FinalizeIo validation, and set
     * receiver ctx deadline from the peer's RPC timeout (negotiated at connect).
     * effective_timeout = local!=0 ? local : peer; but on the receiver side
     * the "local" is the server's own timeout (typically 0) and "peer" is the
     * client's. We use peer_rpc_timeout_ms as the ctx timeout. */
    ctx->expect_gen = ctrl->read_gen;
    {
        auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
        uint32_t peer_timeout_ms = (umq_sock != nullptr) ? umq_sock->GetPeerRpcTimeoutMs() : 0;
        if (peer_timeout_ms > 0) {
            uint64_t now_ns = Func::CurrentTimeNs();
            uint64_t timeout_ns = static_cast<uint64_t>(peer_timeout_ms) * 1000000ULL;
            uint64_t margin_ns = static_cast<uint64_t>(GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS) * 1000000ULL;
            ctx->deadline_ns = now_ns + timeout_ns + margin_ns;
            /* design §9 perf: track ctxs with non-zero deadline for sweep fast-path */
            state->ctx_with_deadline.fetch_add(1, std::memory_order_relaxed);
            TxCqePoller::Instance().MarkActive(sock); /* SweepExpiredCtxs must visit this socket */
        }
    }

    umq_alloc_option_t dest_option{};
    dest_option.flag = UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE;
    dest_option.headroom_size = sizeof(Block);
    const bool gen_check = (ctx->expect_gen != 0);
    PROF_START(UBS_NATIVE_READ_WR_ALLOC);
    const uint8_t *info_ptr2 = reinterpret_cast<const uint8_t *>(offer.infos);
    for (uint16_t i = 0; i < nsegs; ++i) {
        const UbsSeg &sge = offer.segs[i];
        const uint16_t info_len = sge.mempool_info_len;
        /* design §4.4: when gen_check is on, extend READ range by 8B to pull
         * the headroom (gen) back along with the payload. The dest buf must
         * be large enough to hold gen + payload. */
        const uint64_t read_addr = gen_check ? sge.addr - 8 : sge.addr;
        const uint32_t read_len = gen_check ? sge.length + 8 : sge.length;
        umq_buf_t *dest = UmqApi::umq_buf_alloc(read_len, 1, umqh, &dest_option);
        if (dest == nullptr || !detail::ValidateStandaloneReadQbufChain(dest, read_len, nullptr)) {
            FreeQbufChain(dest);
            for (auto &slot : ctx->wr_slots) {
                FreeQbufChain(slot.pending);
            }
            FreeQbufChain(ctx->offer_rx_buf); /* nothing was posted: free the retained RX buffer */
            ctx->offer_rx_buf = nullptr;
            DeleteIoUctx(ctx);
            SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
            PROF_END(UBS_NATIVE_READ_WR_ALLOC, false);
            PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
            return;
        }
        auto *pro = reinterpret_cast<umq_buf_pro_t *>(dest->qbuf_ext);
        (void)memset(pro, 0, sizeof(*pro));
        pro->opcode = UMQ_OPC_READ;
        pro->remote_sge.addr = read_addr;   /* from the offer's UbsSeg, not the blob */
        pro->remote_sge.length = read_len;  /* from the offer's UbsSeg, not the blob */
        /* umq parses the opaque blob and extracts mempool_id/token_id/token_value;
         * ubsocket never inspects blob fields. */
        uint32_t mp_id = 0, tok_id = 0, tok_val = 0;
        if (UmqApi::umq_mempool_info_get_remote_fields(umqh, info_ptr2, info_len, &mp_id, &tok_id,
                                                        &tok_val) != 0) {
            UBS_VLOG_ERR("DoReadOffer get_remote_fields failed, fd: %d, seq: %llu, info_len: %u\n", recv_fd,
                         static_cast<unsigned long long>(ctrl->seq), info_len);
            FreeQbufChain(dest);
            for (auto &slot : ctx->wr_slots) {
                FreeQbufChain(slot.pending);
            }
            FreeQbufChain(ctx->offer_rx_buf); /* nothing was posted: free the retained RX buffer */
            ctx->offer_rx_buf = nullptr;
            DeleteIoUctx(ctx);
            SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
            PROF_END(UBS_NATIVE_READ_WR_ALLOC, false);
            PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
            return;
        }
        pro->remote_sge.mempool_id = mp_id;
        pro->remote_sge.token_id = tok_id;
        pro->remote_sge.token_value = tok_val;
        ctx->wr_slots.emplace_back();
        detail::UbsBigQbufSlot &slot = ctx->wr_slots.back();
        slot.owner = ctx;
        slot.pending = dest;
        pro->user_ctx = reinterpret_cast<uint64_t>(&slot);
        info_ptr2 += sge.mempool_info_len;
        UBS_DATAPATH_LOG("[datapath] READ WR built (big), fd: %d, seq: %llu, slot: %u, remote_addr: 0x%llx, len: %u, "
                         "mempool_id: %u, token_id: %u\n",
                         recv_fd, static_cast<unsigned long long>(ctrl->seq), i,
                         static_cast<unsigned long long>(sge.addr), sge.length, mp_id, tok_id);
    }
    PROF_END(UBS_NATIVE_READ_WR_ALLOC, true);

    /* issue#38 (unbind race): HandleRxControl's teardown guard is
     * check-then-act — between it and the post below, the reaper's
     * RetireStep (or UnbindAndFlushRemoteUmq) can run umq_unbind and free
     * queue->bind_ctx; the post then faults in umq_ub_fill_wr. Every unbind
     * path runs UbsBigdata::CleanupSocketState BEFORE umq_unbind, and
     * CleanupSocketState must take this same state->mutex to drain — so
     * holding the mutex from the destroying/retiring recheck through
     * umq_post guarantees the unbind has not started when the WRs hit the
     * SQ. MarkActive / SendSimpleCtrl / DeleteIoUctx stay outside the hold:
     * the first two take the poller pool lock (reaper order is pool→state;
     * taking pool under state would invert it), and DeleteIoUctx can drop
     * the last state ref (must not destroy a held mutex). */
    auto *guard_sock = dynamic_cast<UmqSocket *>(sock.Get());
    bool teardown_bail = false;
    bool configure_failed = false;
    umq_buf_t *head = nullptr;
    umq_buf_t *bad = nullptr;
    int ret = 0;
    int savedErrno = 0;
    const size_t read_wr_count = ctx->wr_slots.size();
    {
        Locker lk(state->mutex);
        if (state->destroying.load(std::memory_order_acquire) || guard_sock == nullptr ||
            guard_sock->IsRetiring()) {
            teardown_bail = true;
        } else {
            state->active_io.insert(ctx);
            /* Configure each READ WR to produce its own CQE so SQ slots are
             * reclaimed independently, then post the chain in one batch
             * (design §6.3). */
            PROF_START(UBS_NATIVE_CONFIGURE_ORDERED_READ);
            const bool configure_ok =
                detail::ConfigureOrderedReadCompletions(ctx->wr_slots.data(), ctx->wr_slots.size());
            PROF_END(UBS_NATIVE_CONFIGURE_ORDERED_READ, configure_ok);
            if (!configure_ok) {
                state->active_io.erase(ctx);
                configure_failed = true;
            } else {
                head = ctx->wr_slots[0].pending;
                for (size_t i = 0; i + 1 < ctx->wr_slots.size(); ++i) {
                    detail::FindReadWrTail(ctx->wr_slots[i].pending)->qbuf_next = ctx->wr_slots[i + 1].pending;
                }
                /* 控制帧槽位预留：额度不足整链时不 post（一条 READ 链最长
                 * UBS_SEG_MAX=32 WR，远小于预留后的额度上限，不会永久饿死），
                 * 按"整链被拒 EAGAIN"走下方 pending_reads 入队路径，链保持
                 * 完整，由 RetryPendingReads 在额度恢复后续投。 */
                if (read_wr_count > DataPostBudget(guard_sock)) {
                    ret = -UMQ_ERR_EAGAIN;
                    bad = head; /* nothing submitted */
                } else {
                    umq_io_option_t opt = TxOption();
                    PROF_START(UBS_NATIVE_UMQ_POST_READ);
                    ret = UmqApi::umq_post(umqh, head, &opt, &bad);
                    savedErrno = errno; /* save immediately: PROF_END / lock scope may clobber errno (AGENTS.md) */
                    PROF_END(UBS_NATIVE_UMQ_POST_READ, ret == 0);
                }
            }
        }
    }
    if (teardown_bail || configure_failed) {
        for (auto &slot : ctx->wr_slots) {
            FreeQbufChain(slot.pending);
        }
        FreeQbufChain(ctx->offer_rx_buf); /* nothing was posted: free the retained RX buffer */
        ctx->offer_rx_buf = nullptr;
        DeleteIoUctx(ctx);
        if (configure_failed) {
            /* teardown_bail sends nothing: the ABORT would be one more post
             * racing the same unbind; the peer learns via disconnect. */
            SendSimpleCtrl(state, recv_fd, umqh, UBS_READ_ABORT, ctrl->seq, sock);
        }
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    if (ret == 0) {
        /* Account for the READ WRs just submitted to the SQ. HandleTxCompletion
         * does fetch_add(completion_span) for each READ CQE; with per-WR CQEs
         * each span is 1, so read_wr_count CQEs balance this fetch_sub.
         * Without the matching fetch_sub here, tx_queue_avail_num_ leaks
         * upward (only add, never sub), eventually overflowing uint16_t and
         * falsely reporting SQ space that doesn't exist in hardware, causing
         * status:12 (SQ full) stalls. */
        auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
        if (umq_sock != nullptr) {
            auto *tx_ops = umq_sock->GetTxOps();
            if (tx_ops != nullptr) {
                tx_ops->tx_queue_avail_num_.fetch_sub(static_cast<uint16_t>(read_wr_count), std::memory_order_acq_rel);
            }
            if (GlobalSetting::UBS_MONITOR_ENABLE) {
                if (auto *mgr = umq_sock->GetStatsMgr()) {
                    mgr->UpdateTraceStats(Statistics::StatsMgr::BIGDATA_READ_COUNT,
                                          static_cast<uint32_t>(read_wr_count));
                }
            }
        }
        TxCqePoller::Instance().MarkActive(sock);
        UbsStageTrace(recv_fd, first_sn, STAGE_UMQ_POST_READ, UbsPktTraceNowNs());
        /* Same-thread (RX thread): mark inflight only, no eventfd wake.
         * DoReadOffer's post is on the share-JFR RX thread which is the
         * same thread running RunUnifiedActiveLoop, so the poller is by
         * definition not sleeping. */
        if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
            TxCqePoller::Instance().NotifyInflight();
        }
        UBS_DATAPATH_LOG("[datapath] RDMA READ posted (big), fd: %d, umqh: %llu, seq: %llu, first_sn: %u, read_wrs: %zu\n",
                         recv_fd, umqh, static_cast<unsigned long long>(ctrl->seq), first_sn, read_wr_count);
    }
    if (ret != 0) {
        /* On EAGAIN/ENOBUFS/ENOMEM the SQ is full. Do NOT drain TX CQEs here
         * (that is the TxCqePoller's job, now running as a bthread); instead
         * enqueue the transaction on pending_reads and let
         * RetryPendingReadsForSocket (in the TxCqePoller bthread) retry the
         * post once SQ slots free up. This keeps the RX (share-JFR) thread
         * focused on RX CQE processing. */
        const bool nothing_submitted = (bad == nullptr || bad == head);
        if ((ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
             ret == -UMQ_ERR_EMLINK) &&
            nothing_submitted) {
            bool enqueued = false;
            {
                Locker lk(state->mutex);
                std::deque<UbsBigIoCtx *> *pr = state->EnsurePendingReads();
                if (pr != nullptr && pr->size() < UBS_BIG_PENDING_READ_MAX) {
                    pr->push_back(ctx);
                    enqueued = true;
                }
            }
            if (enqueued) {
                TxCqePoller::Instance().MarkActive(sock); /* RetryPendingReads must visit this socket */
                UBS_DATAPATH_LOG("[datapath] READ deferred to pending_reads (big), fd: %d, seq: %llu, read_wrs: %zu, "
                                 "ret: %d\n",
                                 recv_fd, static_cast<unsigned long long>(ctrl->seq), read_wr_count, ret);
                PROF_END(UBS_NATIVE_DO_READ_OFFER, true);
                return; /* retried later by RetryPendingReadsForSocket */
            }
            /* Queue full + no READ submitted: drop the transaction, rely on RPC
             * timeout (design §9). Fall through to finalize-as-failed. */
            UBS_DATAPATH_LOG("[datapath] pending_reads full, dropping transaction (big), fd: %d, seq: %llu\n", recv_fd,
                             static_cast<unsigned long long>(ctrl->seq));
        }
        /* Partial submit: some WRs were accepted by the SQ but not all.
         * Advance next_post past the submitted WRs and enqueue for retry
         * instead of finalizing (which would close the connection). */
        if ((ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
             ret == -UMQ_ERR_EMLINK) &&
            !nothing_submitted) {
            size_t submitted_wrs = 0;
            for (size_t i = 0; i < ctx->wr_slots.size(); ++i) {
                if (ctx->wr_slots[i].pending == bad) {
                    submitted_wrs = i;
                    break;
                }
            }
            /* Account for the submitted WRs. */
            if (submitted_wrs > 0) {
                auto *umq_sock_p = RefConvert<Socket, UmqSocket>(sock).Get();
                if (umq_sock_p != nullptr) {
                    auto *tx_ops = umq_sock_p->GetTxOps();
                    if (tx_ops != nullptr) {
                        tx_ops->tx_queue_avail_num_.fetch_sub(static_cast<uint16_t>(submitted_wrs),
                                                              std::memory_order_acq_rel);
                    }
                }
            }
            ctx->next_post = submitted_wrs;
            bool enqueued = false;
            {
                Locker lk(state->mutex);
                std::deque<UbsBigIoCtx *> *pr = state->EnsurePendingReads();
                if (pr != nullptr && pr->size() < UBS_BIG_PENDING_READ_MAX) {
                    pr->push_back(ctx);
                    enqueued = true;
                }
            }
            if (enqueued) {
                TxCqePoller::Instance().MarkActive(sock);
                UBS_DATAPATH_LOG("[datapath] READ partial submit deferred (big), fd: %d, seq: %llu, "
                                 "submitted: %zu, remaining: %zu\n",
                                 recv_fd, static_cast<unsigned long long>(ctrl->seq),
                                 submitted_wrs, ctx->wr_slots.size() - submitted_wrs);
                PROF_END(UBS_NATIVE_DO_READ_OFFER, true);
                return;
            }
            /* Queue full: fall through to finalize-as-failed. */
            UBS_DATAPATH_LOG("[datapath] pending_reads full on partial submit, dropping transaction (big), "
                             "fd: %d, seq: %llu\n", recv_fd, static_cast<unsigned long long>(ctrl->seq));
        }
        const int mappedErrno = umq::UmqErrnoConverter::Convert(umq::UmqOperation::READV, ret, savedErrno);
        UBS_VLOG_ERR("DoReadOffer umq_post(READ) failed, fd: %d, seq: %llu, ret: %d, errno: %d, desc: %s\n", recv_fd,
                     static_cast<unsigned long long>(ctrl->seq), ret, mappedErrno,
                     umq::UmqErrnoConverter::GetErrorDescription(umq::UmqOperation::READV, ret));
        /* Compute how many WRs were actually submitted to the SQ.  bad points
         * to the first unsubmitted buf; everything before it was submitted.
         * Set wr_total to the submitted count so FinalizeIo waits for all
         * submitted WRs to reach a terminal CQE before freeing their bufs
         * (preventing use-after-free).  If nothing was submitted (bad == head
         * or bad == nullptr), wr_total=0 and FinalizeIo runs immediately,
         * sending READ_ABORT and closing the socket. */
        size_t submitted_wrs = 0;
        if (bad == nullptr) {
            submitted_wrs = ctx->wr_slots.size();
        } else {
            for (size_t i = 0; i < ctx->wr_slots.size(); ++i) {
                if (ctx->wr_slots[i].pending == bad) {
                    submitted_wrs = i;
                    break;
                }
            }
        }
        ctx->next_post = submitted_wrs;
        FinishPosting(ctx, true);
        PROF_END(UBS_NATIVE_DO_READ_OFFER, false);
        return;
    }
    ctx->next_post = ctx->wr_slots.size();
    FinishPosting(ctx, false);
    PROF_END(UBS_NATIVE_DO_READ_OFFER, true);
}

/* Retry EAGAIN-paused READ transactions (design §9). Called from flow-control /
 * TX-completion cycles. Each ctx still owns its READ WRs and offer_rx_buf; on
 * success it proceeds to FinishPosting, on persistent EAGAIN it stays queued,
 * on other errors it finalizes as failed. */
void RetryPendingReads(UbsBigdataSocketState *state, uint64_t umqh, UmqSocket *umq_sock)
{
    PROF_START(UBS_NATIVE_RETRY_PENDING_READS);
    if (state == nullptr) {
        PROF_END(UBS_NATIVE_RETRY_PENDING_READS, true);
        return;
    }
    for (;;) {
        UbsBigIoCtx *ctx = nullptr;
        umq_buf_t *head = nullptr;
        umq_buf_t *bad = nullptr;
        size_t start = 0;
        size_t remaining_wr_count = 0;
        int ret = 0;
        int savedErrno = 0;
        {
            Locker lk(state->mutex);
            if (state->PendingReadsEmpty() ||
                state->destroying.load(std::memory_order_acquire)) {
                PROF_END(UBS_NATIVE_RETRY_PENDING_READS, true);
                return;
            }
            ctx = state->pending_reads->front();
            state->pending_reads->pop_front();
            /* issue#38 (unbind race): post while still holding the mutex the
             * destroying check above was made under — same reasoning as
             * DoReadOffer. Popping under the lock and posting outside it
             * reopens the window against umq_unbind. */
            /* Resume from next_post: WRs before next_post were already submitted
             * in a previous partial retry; only post the remaining WRs. The
             * qbuf_next chain from wr_slots[next_post] to the end is still intact
             * (umq_post reads but never modifies qbuf_next). */
            start = ctx->next_post;
            head = ctx->wr_slots[start].pending;
            remaining_wr_count = ctx->wr_slots.size() - start;
            /* 控制帧槽位预留：剩余链长超过额度时不 post，按"仍被暂停"放回
             * 队头结束本轮；DrainDeferredControls 在本函数之前排水，控制帧
             * 优先拿到刚回补的槽位。 */
            if (remaining_wr_count > DataPostBudget(umq_sock)) {
                ret = -UMQ_ERR_EAGAIN;
                bad = head; /* nothing submitted */
            } else {
                umq_io_option_t opt = TxOption();
                ret = UmqApi::umq_post(umqh, head, &opt, &bad);
                savedErrno = errno; /* save immediately: lock scope exit may clobber errno (AGENTS.md) */
            }
        }
        if (ret == 0) {
            /* All remaining WRs submitted. Account for them. */
            if (umq_sock != nullptr) {
                auto *tx_ops = umq_sock->GetTxOps();
                if (tx_ops != nullptr) {
                    tx_ops->tx_queue_avail_num_.fetch_sub(static_cast<uint16_t>(remaining_wr_count),
                                                          std::memory_order_acq_rel);
                }
                TxCqePoller::Instance().MarkActive(umq_sock);
            }
            ctx->next_post = ctx->wr_slots.size();
            FinishPosting(ctx, false);
            UBS_DATAPATH_LOG("[datapath] pending READ reposted (big), fd: %d, seq: %llu, read_wrs: %zu\n", ctx->fd,
                             static_cast<unsigned long long>(ctx->seq), remaining_wr_count);
            /* Same-thread (poller thread): mark inflight only. */
            if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
                TxCqePoller::Instance().NotifyInflight();
            }
            continue;
        }
        const bool nothing_submitted = (bad == nullptr || bad == head);
        if ((ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
             ret == -UMQ_ERR_EMLINK) &&
            nothing_submitted) {
            /* Still paused: put it back at the head and stop this retry round. */
            Locker lk(state->mutex);
            state->pending_reads->push_front(ctx);
            UBS_DATAPATH_LOG("[datapath] pending READ still paused (big), fd: %d, seq: %llu, ret: %d\n", ctx->fd,
                             static_cast<unsigned long long>(ctx->seq), ret);
            PROF_END(UBS_NATIVE_RETRY_PENDING_READS, true);
            return;
        }
        /* Partial submit: some WRs were accepted by the SQ but not all.
         * Advance next_post past the submitted WRs and re-enqueue for
         * later retry. Do NOT finalize or drain — the submitted WRs are
         * in-flight and will produce CQEs; the remaining WRs will be
         * posted once SQ slots free up. */
        if ((ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
             ret == -UMQ_ERR_EMLINK) &&
            !nothing_submitted) {
            /* Find how many WRs were submitted in this round. */
            size_t submitted = 0;
            for (size_t i = start; i < ctx->wr_slots.size(); ++i) {
                if (ctx->wr_slots[i].pending == bad) {
                    submitted = i - start;
                    break;
                }
            }
            /* Account for the submitted WRs. */
            if (submitted > 0 && umq_sock != nullptr) {
                auto *tx_ops = umq_sock->GetTxOps();
                if (tx_ops != nullptr) {
                    tx_ops->tx_queue_avail_num_.fetch_sub(static_cast<uint16_t>(submitted),
                                                          std::memory_order_acq_rel);
                }
                TxCqePoller::Instance().MarkActive(umq_sock);
            }
            /* Advance next_post past the submitted WRs. */
            ctx->next_post = start + submitted;
            UBS_DATAPATH_LOG("[datapath] pending READ partial submit (big), fd: %d, seq: %llu, "
                             "submitted: %zu, remaining: %zu, next_post: %zu\n",
                             ctx->fd, static_cast<unsigned long long>(ctx->seq),
                             submitted, ctx->wr_slots.size() - ctx->next_post,
                             ctx->next_post);
            /* Re-enqueue for later retry. */
            Locker lk(state->mutex);
            if (state->pending_reads->size() < UBS_BIG_PENDING_READ_MAX) {
                state->pending_reads->push_front(ctx);
            } else {
                /* Queue full: finalize this ctx as failed. */
                ctx->next_post = ctx->wr_slots.size();
                FinishPosting(ctx, true);
            }
            if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
                TxCqePoller::Instance().NotifyInflight();
            }
            PROF_END(UBS_NATIVE_RETRY_PENDING_READS, true);
            return;
        }
        /* Other (fatal) error: finalize as failed. */
        const int mappedErrno = umq::UmqErrnoConverter::Convert(umq::UmqOperation::READV, ret, savedErrno);
        const char *errDesc = umq::UmqErrnoConverter::GetErrorDescription(umq::UmqOperation::READV, ret);
        if (umq_sock != nullptr && umq_sock->IsRetiring()) {
            UBS_VLOG_DEBUG("RetryPendingReads umq_post failed during retire, fd: %d, seq: %llu, ret: %d, "
                           "errno: %d, state: %p, desc: %s\n",
                           ctx->fd, static_cast<unsigned long long>(ctx->seq), ret, mappedErrno, state, errDesc);
        } else {
            UBS_VLOG_ERR("RetryPendingReads umq_post failed, fd: %d, seq: %llu, ret: %d, errno: %d, state: %p, "
                         "desc: %s\n",
                         ctx->fd, static_cast<unsigned long long>(ctx->seq), ret, mappedErrno, state, errDesc);
        }
        errno = mappedErrno;
        ctx->next_post = ctx->wr_slots.size();
        FinishPosting(ctx, true);
        /* Do NOT drain remaining pending reads — they may succeed once
         * SQ slots free up. Only the current transaction hit a fatal error. */
        PROF_END(UBS_NATIVE_RETRY_PENDING_READS, true);
        return;
    }
}

bool validateSegs(const ubs_data_list_t *data_list)
{
    uint64_t total = 0;
    for (uint16_t i = 0; i < data_list->nsegs; ++i) {
        const ubs_segment_t &s = data_list->segments[i];
        if (s.block == nullptr) {
            errno = EINVAL;
            UBS_VLOG_ERR("validateSegs segment block is null, data_list: %u, i: %u, nsegs: %u\n", data_list, i,
                         data_list->nsegs);
            return false;
        }
        if (s.offset > s.offset + s.len) { /* 32-bit overflow guard */
            errno = EINVAL;
            UBS_VLOG_ERR("validateSegs segment offset overflow, data_list: %u, i: %u, nsegs: %u\n", data_list, i,
                         data_list->nsegs);
            return false;
        }
        total += s.len;
        if (total > static_cast<uint64_t>(std::numeric_limits<ssize_t>::max())) {
            errno = EOVERFLOW;
            UBS_VLOG_ERR("validateSegs total length overflow, data_list: %u, total: %llu, i: %u, nsegs: %u\n",
                         data_list, total, i, data_list->nsegs);
            return false;
        }
    }

    return true;
}

/*
 * Per-buffer bookkeeping for the batch submit (design §5.1). Each buf appended
 * to the batch chain records how many input segments it covers (for translating
 * umq_post's accepted-prefix buf count back to accepted segment count) and how
 * many connection-level SNs were reserved for it (for rolling back the tail when
 * umq_post accepts only a prefix).
 */
struct BatchBufMeta {
    umq_buf_t *buf{nullptr};
    uint32_t segs{0};     /* input segments covered by this buf (1 for SMALL, nsegs for an offer) */
    uint32_t sn_count{0}; /* SNs reserved for this buf (1 each) */
    uint64_t seq{0};      /* offer seq (0 for SMALL_DATA); for tail pin rollback */
    Block *block{nullptr}; /* BuildSmallData: brpc Block that was IncRef'd (needs DecRef on rollback) */
};

/* Sender context shared across the per-segment routing loop in TrySenderPost. */
struct SenderPostCtx {
    int fd{0};
    uint64_t umqh{0};
    UmqSocket *umq_sock{nullptr};
    UbsBigdataSocketState *state{nullptr};
    OfferBuilder offer_builder;
    std::vector<BatchBufMeta> batch; /* sealed bufs awaiting one umq_post */
    bool built_any_big{false};
    bool had_error{false};
    uint64_t batch_gen{0};            /* design §4.1: one gen per batch */
    uint64_t batch_deadline_ns{0};    /* design §4.1: pinned deadline for this batch */
};

/* Seal the in-flight offer into a buf and append it to the batch chain. */
bool FlushPendingOffer(SenderPostCtx &ctx)
{
    if (ctx.offer_builder.nsegs == 0) {
        return true;
    }
    PROF_START(UBS_NATIVE_FLUSH_PENDING_OFFER);
    umq_buf_t *sealed = ctx.offer_builder.SealOnly(ctx.state);
    if (sealed != nullptr && ctx.umq_sock != nullptr) {
        /* The pinned entry (possibly with a deadline) exists from here on,
         * whether or not the offer's SEND gets posted below — make sure
         * SweepExpiredPinned will visit this socket. */
        TxCqePoller::Instance().MarkActive(ctx.umq_sock);
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            if (auto *mgr = ctx.umq_sock->GetStatsMgr()) {
                mgr->UpdateTraceStats(Statistics::StatsMgr::BIGDATA_CTRL_SEND_COUNT, 1);
            }
        }
    }
    if (sealed == nullptr) {
        PROF_END(UBS_NATIVE_FLUSH_PENDING_OFFER, false);
        UBS_VLOG_ERR("FlushPendingOffer SealOnly failed, fd: %d, umqh: %llu, nsegs: %u\n", ctx.fd, ctx.umqh,
                     ctx.offer_builder.nsegs);
        return false;
    }
    BatchBufMeta meta;
    meta.buf = sealed;
    meta.segs = ctx.offer_builder.nsegs_at_seal;
    meta.sn_count = 1; /* an offer carries first_sn; slot i -> sn_add(first_sn, i) */
    meta.seq = ctx.offer_builder.ctrl_hdr_seq;
    ctx.batch.push_back(meta);
    UBS_DATAPATH_LOG("[datapath] READ_OFFER sealed (big), fd: %d, umqh: %llu, nsegs: %u, seq: %llu, first_sn: %u\n",
                     ctx.fd, ctx.umqh, ctx.offer_builder.nsegs_at_seal,
                     static_cast<unsigned long long>(ctx.offer_builder.ctrl_hdr_seq), ctx.offer_builder.ctrl_sn);
    UbsStageTrace(ctx.fd, ctx.offer_builder.ctrl_sn, STAGE_FLUSH_PENDING_OFFER, UbsPktTraceNowNs());
    PROF_END(UBS_NATIVE_FLUSH_PENDING_OFFER, true);
    return true;
}

/* Allocate a fresh offer control buffer and reserve seq + first SN. */
bool AllocNewOffer(SenderPostCtx &ctx)
{
    if (!ctx.offer_builder.Alloc(ctx.umqh)) {
        return false;
    }
    ctx.offer_builder.ctrl_hdr_seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    ctx.offer_builder.ctrl_sn = ctx.umq_sock->FetchAddSeqNum(1);
    ctx.offer_builder.has_sn = true;
    /* design §4.1: propagate batch gen and deadline to this offer. */
    ctx.offer_builder.gen = ctx.batch_gen;
    ctx.offer_builder.deadline_ns = ctx.batch_deadline_ns;
    UBS_DATAPATH_LOG("[datapath] new READ_OFFER allocated (big), fd: %d, umqh: %llu, seq: %llu, offer_sn: %u, gen: %llu\n", ctx.fd,
                     ctx.umqh, static_cast<unsigned long long>(ctx.offer_builder.ctrl_hdr_seq),
                     ctx.offer_builder.ctrl_sn, static_cast<unsigned long long>(ctx.offer_builder.gen));
    return true;
}

/* Small segment: interrupt any pending offer, then build a SMALL_DATA buf and
 * append it to the batch chain (not posted yet). */
void HandleSmallSegment(SenderPostCtx &ctx, const ubs_segment_t &s)
{
    PROF_START(UBS_NATIVE_HANDLE_SMALL_SEGMENT);
    if (ctx.offer_builder.nsegs > 0 && !FlushPendingOffer(ctx)) {
        ctx.had_error = true;
        PROF_END(UBS_NATIVE_HANDLE_SMALL_SEGMENT, false);
        UBS_VLOG_ERR("HandleSmallSegment FlushPendingOffer failed, fd: %d, umqh: %llu, nsegs: %u\n", ctx.fd, ctx.umqh,
                     ctx.offer_builder.nsegs);
        return;
    }
    const uint32_t sn = ctx.umq_sock->FetchAddSeqNum(1);
    umq_buf_t *small = BuildSmallData(ctx.umqh, s, sn);
    if (small == nullptr) {
        ctx.umq_sock->FetchSubSeqNum(1); /* undo the SN reservation */
        ctx.had_error = true;
        PROF_END(UBS_NATIVE_HANDLE_SMALL_SEGMENT, false);
        UBS_VLOG_ERR("HandleSmallSegment BuildSmallData failed, fd: %d, umqh: %llu, ubs_segment_t: %u, len: %llu\n",
                     ctx.fd, ctx.umqh, s, s.len);
        return;
    }
    BatchBufMeta meta;
    meta.buf = small;
    meta.segs = 1;
    meta.sn_count = 1;
    meta.seq = 0;
    meta.block = reinterpret_cast<Block *>(s.block);
    ctx.batch.push_back(meta);
    UBS_DATAPATH_LOG("[datapath] SMALL_DATA SEND, fd: %d, umqh: %llu, sn: %u, len: %u, offset: %u, block: %p\n", ctx.fd,
                     ctx.umqh, sn, s.len, s.offset, s.block);
    PROF_END(UBS_NATIVE_HANDLE_SMALL_SEGMENT, true);
}

/*
 * Coalesce count consecutive small segments into one with_data SMALL_DATA buf
 * (design §5.2 optimization). Flushes any pending READ_OFFER first to preserve
 * SN ordering, then builds one coalesced buf covering all segments with a
 * single SN, and appends it to the batch chain.
 */
void HandleCoalescedSmallSegments(SenderPostCtx &ctx, const ubs_segment_t *segs, uint16_t count)
{
    PROF_START(UBS_NATIVE_HANDLE_COALESCED_SMALL_SEGMENT);
    if (ctx.offer_builder.nsegs > 0 && !FlushPendingOffer(ctx)) {
        ctx.had_error = true;
        PROF_END(UBS_NATIVE_HANDLE_COALESCED_SMALL_SEGMENT, false);
        UBS_VLOG_ERR("HandleCoalescedSmallSegments FlushPendingOffer failed, fd: %d, umqh: %llu, nsegs: %u\n",
                     ctx.fd, ctx.umqh, ctx.offer_builder.nsegs);
        return;
    }
    const uint32_t sn = ctx.umq_sock->FetchAddSeqNum(1);
    umq_buf_t *coalesced = BuildCoalescedSmallData(ctx.umqh, segs, count, sn);
    if (coalesced == nullptr) {
        ctx.umq_sock->FetchSubSeqNum(1); /* undo the SN reservation */
        ctx.had_error = true;
        PROF_END(UBS_NATIVE_HANDLE_COALESCED_SMALL_SEGMENT, false);
        UBS_VLOG_ERR("HandleCoalescedSmallSegments BuildCoalescedSmallData failed, fd: %d, umqh: %llu, count: %u\n",
                     ctx.fd, ctx.umqh, count);
        return;
    }
    BatchBufMeta meta;
    meta.buf = coalesced;
    meta.segs = static_cast<uint32_t>(count);
    meta.sn_count = 1;
    meta.seq = 0;
    ctx.batch.push_back(meta);
    PROF_END(UBS_NATIVE_HANDLE_COALESCED_SMALL_SEGMENT, true);
}

/* Large segment: route via READ_OFFER, sealing/reallocating as needed. */
void HandleLargeSegment(SenderPostCtx &ctx, const ubs_segment_t &s, uint16_t idx)
{
    PROF_START(UBS_NATIVE_HANDLE_LARGE_SEGMENT);
    ctx.built_any_big = true;
    if (ctx.offer_builder.ctrl == nullptr) {
        if (!AllocNewOffer(ctx)) {
            ctx.had_error = true;
            PROF_END(UBS_NATIVE_HANDLE_LARGE_SEGMENT, false);
            UBS_VLOG_ERR("HandleLargeSegment AllocNewOffer failed, fd: %d, umqh: %llu, ubs_segment_t: %u, len: %llu\n",
                         ctx.fd, ctx.umqh, s, s.len);
            return;
        }
    }
    if (!ctx.offer_builder.CanAppend()) {
        /* Seal the current offer and start a new one for this segment. */
        if (!FlushPendingOffer(ctx)) {
            ctx.had_error = true;
            PROF_END(UBS_NATIVE_HANDLE_LARGE_SEGMENT, false);
            UBS_VLOG_ERR("HandleLargeSegment FlushPendingOffer failed, fd: %d, umqh: %llu, nsegs: %u, ubs_segment_t: "
                         "%u, len: %llu\n",
                         ctx.fd, ctx.umqh, ctx.offer_builder.nsegs, s, s.len);
            return;
        }
        ctx.offer_builder = OfferBuilder{};
        if (!AllocNewOffer(ctx)) {
            ctx.had_error = true;
            PROF_END(UBS_NATIVE_HANDLE_LARGE_SEGMENT, false);
            UBS_VLOG_ERR("HandleLargeSegment AllocNewOffer failed, fd: %d, umqh: %llu, ubs_segment_t: %u, len: %llu\n",
                         ctx.fd, ctx.umqh, s, s.len);
            return;
        }
    }
    if (!ctx.offer_builder.Append(ctx.umqh, s, ctx.fd, idx)) {
        ctx.offer_builder.Rollback();
        ctx.umq_sock->FetchSubSeqNum(1);
        ctx.had_error = true;
        PROF_END(UBS_NATIVE_HANDLE_LARGE_SEGMENT, false);
        UBS_VLOG_ERR("HandleLargeSegment Append failed, fd: %d, umqh: %llu, nsegs: %u, ubs_segment_t: %u, len: %llu\n",
                     ctx.fd, ctx.umqh, ctx.offer_builder.nsegs, s, s.len);
        return;
    }
    UBS_DATAPATH_LOG("[datapath] READ_OFFER append (big), fd: %d, umqh: %llu, input_idx: %u, len: %u, offset: %u, "
                     "offer_sn: %u, offer_nsegs: %u\n",
                     ctx.fd, ctx.umqh, idx, s.len, s.offset, ctx.offer_builder.ctrl_sn, ctx.offer_builder.nsegs);
    PROF_END(UBS_NATIVE_HANDLE_LARGE_SEGMENT, true);
}

} // namespace

/* ---- UmqSocket state accessors (state type is complete here) ---- */

namespace umq {

UbsBigdataSocketState *UmqSocket::GetOrCreateBigdataState()
{
    SocketExt *ext = EnsureExt();
    if (ext == nullptr) {
        return nullptr;
    }
    void *state = ext->bigdata.load(std::memory_order_acquire);
    if (state != nullptr) {
        return static_cast<UbsBigdataSocketState *>(state);
    }
    /* issue#38 (unbind race): once teardown starts, CleanupSocketState has
     * detached-and-drained the state (or found none). Minting a fresh one
     * here would hand callers a destroying=false state that no cleanup will
     * ever visit, and their post would race umq_unbind unguarded. The
     * refusal is ordered by ext->bigdata itself: a creator that observed
     * cleanup's nullptr store also observes the retiring_ store that
     * precedes it. */
    if (retiring_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    auto *candidate = new (std::nothrow) UbsBigdataSocketState;
    if (candidate == nullptr) {
        return nullptr;
    }
    if (!ext->bigdata.compare_exchange_strong(state, candidate, std::memory_order_release,
                                              std::memory_order_acquire)) {
        delete candidate;
        return static_cast<UbsBigdataSocketState *>(state);
    }
    return candidate;
}

UbsBigdataSocketState *UmqSocket::GetBigdataState() const noexcept
{
    SocketExt *ext = ExtOrNull();
    return ext != nullptr ? static_cast<UbsBigdataSocketState *>(ext->bigdata.load(std::memory_order_acquire))
                          : nullptr;
}

UbsBigdataSocketState *UmqSocket::ReleaseBigdataState() noexcept
{
    SocketExt *ext = ExtOrNull();
    return ext != nullptr
               ? static_cast<UbsBigdataSocketState *>(ext->bigdata.exchange(nullptr, std::memory_order_acq_rel))
               : nullptr;
}

} // namespace umq

/* ---- Adaptive send path (design §5.1) ---- */

bool UbsBigdata::TrySenderPost(int fd, const ubs_data_list_t *data_list, ssize_t *out)
{
    PROF_START(UBS_NATIVE_TRY_SENDER_POST);
    if (data_list == nullptr || out == nullptr) {
        if (out != nullptr) {
            *out = -1;
        }
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, false);
        UBS_VLOG_ERR("TrySenderPost data_list is invalid, fd: %d, data_list: %u, out: %p\n", fd, data_list, out);
        return false;
    }
    if (data_list->nsegs == 0) {
        *out = 0;
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, true);
        return true;
    }
    if (data_list->segments == nullptr) {
        errno = EINVAL;
        *out = -1;
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, false);
        UBS_VLOG_ERR("TrySenderPost data_list segments is invalid, fd: %d, data_list: %u, out: %p\n", fd, data_list,
                     out);
        return false;
    }

    SocketPtr holder;
    auto *umq_sock = GetUmqSocketLookup(fd, holder);
    if (umq_sock == nullptr) {
        errno = EPIPE;
        *out = -1;
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, false);
        UBS_VLOG_ERR("TrySenderPost not a umq socket, fd: %d, data_list: %u, out: %p\n", fd, data_list, out);
        return false; /* not a umq socket: let the caller fall back */
    }
    /* 惰性获取 bigdata state：全小包批次（常规 RPC 的绝对主流）根本不需要它——
     * state 只服务大段（READ_OFFER 的 pin/回滚簿记）与 ctrl 回压。此前无条件
     * GetOrCreateBigdataState 让每条发过包的链路都付 ~0.26KB 的 state 对象
     * （外加其构造的 LockRegistry 锁），profiler 上即 GetOrCreateBigdataState
     * 帧。现在仅在批次遇到首个大段时创建；已存在的 state（此前用过单边）照常
     * 取用并全程持引用。 */
    UbsBigdataSocketState *state = umq_sock->GetBigdataState();
    /* Keep the state alive across the whole send: the socket may be closed
     * concurrently (CleanupSocketState drops the owner ref). */
    BigdataStateRef state_ref(state);
    std::unique_ptr<BigdataStateRef> lazy_state_ref;
    const uint64_t umqh = umq_sock->UmqHandle();

    /* Stage trace (阶段 4): entry_sn = next SN to be allocated = first SN of
     * this batch's first offer/send, used as the (fd, first_sn) key for the
     * TX-side stage events below. */
    const uint32_t entry_sn = umq_sock->LoadSeqNum();
    UbsStageTrace(fd, entry_sn, STAGE_TRY_SENDER_POST, UbsPktTraceNowNs());

    /* design §4.2: compute effective RPC timeout = local!=0 ? local : peer.
     * Used to derive the pinned deadline for two-stage release (§4.3).
     * When gen check is on but neither side propagated rpc_timeout (e.g. server),
     * use a 30s fallback so pinned blocks are swept instead of leaking. */
    uint32_t effective_timeout_ms = 0;
    if (GlobalSetting::UBS_READ_GEN_CHECK_ENABLED) {
        uint32_t local = umq_sock->GetLocalRpcTimeoutMs();
        uint32_t peer = umq_sock->GetPeerRpcTimeoutMs();
        effective_timeout_ms = (local != 0) ? local : peer;
        if (effective_timeout_ms == 0) {
            effective_timeout_ms = 30000;
        }
    }

    /* design §4.1: take one gen per batch. Skip-0 protection. */
    uint64_t gen = 0;
    if (effective_timeout_ms > 0) {
        PROF_START(UBS_NATIVE_READ_GEN_FETCH_ADD);
        gen = g_read_gen.fetch_add(1, std::memory_order_relaxed);
        if (UNLIKELY(gen == 0)) {
            gen = g_read_gen.fetch_add(1, std::memory_order_relaxed);
        }
        PROF_END(UBS_NATIVE_READ_GEN_FETCH_ADD, true);
    }

    /* design §4.1: deadline = now + effective_timeout, plus the big-pin margin
     * to avoid racing with the brpc RPC timeout. 0 = no timeout (no validation). */
    uint64_t deadline_ns = 0;
    if (gen != 0) {
        uint64_t now_ns = Func::CurrentTimeNs();
        uint64_t timeout_ns = static_cast<uint64_t>(effective_timeout_ms) * 1000000ULL;
        uint64_t margin_ns = static_cast<uint64_t>(GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS) * 1000000ULL;
        deadline_ns = now_ns + timeout_ns + margin_ns;
    }

    /* Validate segments and compute the checked total length (design §2.3). */
    if (!validateSegs(data_list)) {
        /* validateSegs sets errno */
        *out = -1;
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, false);
        UBS_VLOG_ERR("TrySenderPost data_list segments invalid, fd: %d, data_list: %u, out: %p\n", fd, data_list, out);
        return false;
    }

    const uint32_t small_max = proto::small_data_payload_max(); /* 4064: full SMALL_DATA region is payload */
    SenderPostCtx ctx;
    auto ensure_state = [&]() -> UbsBigdataSocketState * {
        if (state == nullptr) {
            state = umq_sock->GetOrCreateBigdataState();
            if (state != nullptr) {
                lazy_state_ref.reset(new (std::nothrow) BigdataStateRef(state));
                if (lazy_state_ref == nullptr) {
                    state = nullptr; /* 无法持引用则视为获取失败 */
                }
            }
            ctx.state = state;
        }
        return state;
    };
    ctx.fd = fd;
    ctx.umqh = umqh;
    ctx.umq_sock = umq_sock;
    ctx.state = state;
    ctx.batch_gen = gen;
    ctx.batch_deadline_ns = deadline_ns;

    uint16_t i = 0;
    while (i < data_list->nsegs) {
        const ubs_segment_t &s = data_list->segments[i];
        if (s.len > small_max) {
            if (ensure_state() == nullptr) {
                /* 大段需要 state（pin/回滚簿记）；分配失败按资源不足终止本批，
                 * 语义与旧实现的入口 ENOMEM 一致，只是收敛到真正需要的批次 */
                errno = ENOMEM;
                ctx.had_error = true;
                UBS_VLOG_ERR("TrySenderPost bigdata state alloc failed, fd: %d\n", fd);
                break;
            }
            HandleLargeSegment(ctx, s, i);
            ++i;
        } else {
            /* s is small: scan forward for a run of consecutive small segments
             * whose cumulative payload fits the 4064B SMALL_DATA budget. A run
             * of count >= 2 is coalesced into one with_data WR (1 SN); a
             * singleton falls back to the zero-copy BuildSmallData path. */
            uint16_t j = i;
            uint32_t total = 0;
            while (j < data_list->nsegs && data_list->segments[j].len <= small_max) {
                if (total + data_list->segments[j].len > small_max) {
                    break;
                }
                total += data_list->segments[j].len;
                ++j;
            }
            const uint16_t count = static_cast<uint16_t>(j - i);
            if (count >= 2) {
                HandleCoalescedSmallSegments(ctx, &data_list->segments[i], count);
            } else {
                HandleSmallSegment(ctx, s);
            }
            i = j;
        }
        if (ctx.had_error) {
            UBS_VLOG_ERR("TrySenderPost prepare segment break, fd: %d, nsegs: %u, i: %u\n", fd, data_list->nsegs, i);
            break;
        }
    }

    /* Flush any trailing pending offer into the batch chain. */
    if (!ctx.had_error && ctx.offer_builder.nsegs > 0 && !FlushPendingOffer(ctx)) {
        ctx.had_error = true;
        UBS_VLOG_ERR("TrySenderPost FlushPendingOffer failed, fd: %d, umqh: %llu, nsegs: %u\n", ctx.fd, ctx.umqh,
                     ctx.offer_builder.nsegs);
    }

    if (ctx.had_error || ctx.batch.empty()) {
        if (!ctx.had_error && ctx.batch.empty()) {
            UBS_VLOG_ERR("TrySenderPost batch empty (no error), fd: %d, nsegs: %u, small_max: %u\n", fd,
                         data_list->nsegs, small_max);
            for (uint16_t i = 0; i < data_list->nsegs; ++i) {
                UBS_VLOG_ERR("  seg[%u]: block=%p, offset=%u, len=%u, start_pos=%p\n", i, data_list->segments[i].block,
                             data_list->segments[i].offset, data_list->segments[i].len,
                             data_list->segments[i].start_pos);
            }
        }
        ctx.offer_builder.Rollback();
        /* Free any bufs already staged in the batch (none were posted yet).
         * Sealed READ_OFFER bufs (seq != 0) already transferred their pinned
         * Blocks into state->pinned[seq] in SealOnly; ReleasePinned undoes
         * that, matching the post-umq_post tail rollback below. */
        for (auto &m : ctx.batch) {
            if (m.block != nullptr) {
                m.block->DecRef();
            }
            FreeQbufChain(m.buf);
            umq_sock->FetchSubSeqNum(static_cast<uint32_t>(m.sn_count));
            if (state != nullptr && m.seq != 0) {
                /* seq != 0 蕴含大段已走过 ensure_state；判空纯防御 */
                ReleasePinned(state, m.seq);
            }
        }
        ctx.batch.clear();
        if (ctx.had_error) {
            errno = errno != 0 ? errno : EIO;
            *out = -1;
            PROF_END(UBS_NATIVE_TRY_SENDER_POST, false);
            UBS_VLOG_ERR("TrySenderPost batch empty after validation, fd: %d, umqh: %llu, nsegs: %u\n", ctx.fd,
                         ctx.umqh, ctx.offer_builder.nsegs);
            return false;
        }
        *out = 0; /* batch empty after validation */
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, true);
        return false;
    }

    /* Submit at most UMQ_BATCH_SIZE WRs per umq_post: umq_ub refuses a single
     * SEND chain longer than UMQ_BATCH_SIZE (256, the bond layer's
     * BONDP_BATCH_POST_MAX_NUM counterpart) with "wr count exceeds 256, not
     * supported" (umq_pro_ub.c). One 150MiB request over a bonding device seals
     * several hundred READ_OFFERs, which must not be posted as a single chain.
     *
     * Only the first post_count bufs are chained and submitted; bufs beyond
     * post_count are never chained and stay owned by ubsocket, so the tail
     * rollback below frees them and the partial-accept path (accepted_bufs <
     * batch.size()) signals EAGAIN, handing the remainder back to KeepWrite to
     * re-enter TrySenderPost for the next chunk, in order. umq_post may still
     * accept only a contiguous prefix within the chunk (FC backpressure); that
     * prefix is resolved by the same bad_qbuf logic. */
    /* 控制帧槽位预留 + TX_DEPTH：再把本批裁剪到 SQ 额度内。被裁掉的
     * 尾部与 UMQ_BATCH_SIZE 截断走同一条回滚路径（free + SN 回退 +
     * ReleasePinned + errno=EAGAIN），由 KeepWrite 重入续发。额度为 0 时整批
     * 不 post（等 TX CQE 回补槽位），语义同整批被拒 EAGAIN。 */
    const size_t post_count =
        std::min(std::min(ctx.batch.size(), static_cast<size_t>(UMQ_BATCH_SIZE)), SenderPostBudget(umq_sock));
    umq_buf_t *head = ctx.batch[0].buf;
    umq_buf_t *bad = nullptr;
    int ret;
    if (post_count == 0) {
        ret = -UMQ_ERR_EAGAIN; /* budget exhausted: nothing posted this round (bad=nullptr → accepted 0) */
    } else {
        umq_buf_t *tail = head;
        for (size_t i = 1; i < post_count; ++i) {
            tail->qbuf_next = ctx.batch[i].buf;
            tail = ctx.batch[i].buf;
        }
        tail->qbuf_next = nullptr;

        umq_io_option_t opt = TxOption();
        PROF_START(UBS_NATIVE_UMQ_POST_SEND);
        ret = UmqApi::umq_post(umqh, head, &opt, &bad);
        PROF_END(UBS_NATIVE_UMQ_POST_SEND, ret == 0);
        if (ret == 0) {
            /* Whole posted chain accepted: attribute it to every RPC it
             * carries with ONE range record. Only the first post_count bufs
             * were chained and posted (the trimmed tail is rolled back below
             * and its SNs will be reused by the next batch), so sum sn_count
             * over exactly those bufs: each reserves exactly one SN (offer
             * fragments share first_sn), allocated contiguously from
             * entry_sn -- the posted chain's wire SNs occupy
             * [entry_sn, entry_sn + total_sns). */
            uint32_t total_sns = 0;
            for (size_t i = 0; i < post_count; ++i) {
                total_sns += ctx.batch[i].sn_count;
            }
            UbsStageTrace(fd, entry_sn, STAGE_UMQ_POST_SEND, UbsPktTraceNowNs(), total_sns);
        }
    }

    /* On EAGAIN/ENOBUFS/ENOMEM, return directly to the caller without
     * retrying. Re-posting from `head` after PollTx is unsafe: TX CQE
     * reclamation (umq_ub_poll_tx -> umq_buf_free) clears total_data_size
     * of completed bufs, so re-posting them hits "total_data_size is 0"
     * (umq_pro_ub.c) and double-frees the recycled qbufs. KeepWrite's
     * WaitEpollOut fallback (50ms timeout in the UB native path) ensures
     * forward progress by retrying the whole batch after the SQ drains.
     * This also matches the original design documented in faa6f51f:
     * "KeepWrite's WaitEpollOut timeout (50ms) ensures forward progress".
     * The per-WR CQE fix (ConfigureOrderedReadCompletions) makes SQ-full
     * rare, so this slow path is seldom hit. */

    /* Determine the accepted prefix length (buf index) from bad_qbuf. umq_post
     * reports the first unsubmitted buf in bad_qbuf; everything before it was
     * accepted. If bad == nullptr (or == head only on total failure), prefix = 0.
     * Only the first post_count bufs were submitted, so the prefix is bounded by
     * post_count; bufs beyond it were never posted and are rolled back below. */
    size_t accepted_bufs = post_count;
    const bool eagain = ret == -UMQ_ERR_EAGAIN || ret == -UMQ_ERR_ENOBUFS || ret == -UMQ_ERR_ENOMEM ||
                        ret == -UMQ_ERR_EMLINK;
    if (ret != 0) {
        if (bad == nullptr || bad == head) {
            accepted_bufs = 0;
        } else {
            accepted_bufs = 0;
            for (size_t i = 0; i < post_count; ++i) {
                if (ctx.batch[i].buf == bad) {
                    break;
                }
                ++accepted_bufs;
            }
        }
    }

    /* Roll back the unsubmitted tail: free its bufs, undo SN reservations, and
       ReleasePinned any offer seqs in the tail. The accepted prefix is owned by
       UMQ until its SEND TX CQE (small) / READ_DONE/ABORT (offer).
       The batch was chained into one qbuf_next list before umq_post (above).
       FreeQbufChain follows qbuf_next, so we must detach each buf first to
       avoid double-freeing later bufs. Also detach the accepted tail's
       qbuf_next so the UMQ-owned prefix doesn't walk into freed memory on
       TX CQE reclamation. */
    if (accepted_bufs > 0 && accepted_bufs < ctx.batch.size()) {
        ctx.batch[accepted_bufs - 1].buf->qbuf_next = nullptr;

        /* Partial accept: some bufs were posted but the rest hit flow-control
         * backpressure.  Signal EAGAIN so the caller (ubs_post → PostMessage)
         * retries the remaining segments instead of treating this as fatal. */
        errno = EAGAIN;
    }

    for (size_t i = accepted_bufs; i < ctx.batch.size(); ++i) {
        ctx.batch[i].buf->qbuf_next = nullptr;
        if (ctx.batch[i].block != nullptr) {
            ctx.batch[i].block->DecRef();
        }
        FreeQbufChain(ctx.batch[i].buf);
        umq_sock->FetchSubSeqNum(static_cast<uint32_t>(ctx.batch[i].sn_count));
        if (state != nullptr && ctx.batch[i].seq != 0) {
            ReleasePinned(state, ctx.batch[i].seq);
        }
    }

    if (accepted_bufs == 0) {
        /* Whole batch rejected. Return true (handled) with EAGAIN so ubs_post
         * passes errno through instead of overwriting with EPIPE. KeepWrite
         * retries with the same list. Or EPIPE if the underlying UMQ is in
         * error state. */
        errno = eagain ? EAGAIN : EPIPE;
        *out = -1;
        PROF_END(UBS_NATIVE_TRY_SENDER_POST, true);
        return true;
    }

    /* Cross-thread wake: TrySenderPost runs on the app thread (KeepWrite
     * path), so we must both mark inflight and wake the poller via eventfd.
     * Only wake when at least one buf was accepted (nothing_submitted →
     * no inflight, no wake). flag=OFF: no-ops (poller not started). */
    if (GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED) {
        TxCqePoller::Instance().NotifyInflight();
        TxCqePoller::Instance().NotifyPosted();
    }

    /* Account for ALL SEND WRs just submitted to the SQ (both SMALL_DATA
     * seq==0 and BIG_CTRL control bufs seq!=0). Every buf in the batch is
     * posted as SEND_IMM and consumes one SQ slot. The matching TX CQE path
     * does fetch_add(1) for each: ProcessTxCqe for ordinary SENDs, and
     * HandleTxCompletion (BIG_CTRL branch, via deferred_progress_fd) when
     * bit 20 is preserved. When bit 20 is stripped by the URMA bond layer,
     * BIG_CTRL CQEs also fall through to ProcessTxCqe, so fetch_add still
     * fires. Without this fetch_sub, tx_queue_avail_num_ leaks upward past
     * UBS_TX_DEPTH, causing TxSweepOnce to skip ForceDrainTx (avail ==
     * UBS_TX_DEPTH) and READ completion CQEs to never be drained. */
    if (accepted_bufs > 0) {
        auto *tx_ops = umq_sock->GetTxOps();
        if (tx_ops != nullptr) {
            tx_ops->tx_queue_avail_num_.fetch_sub(static_cast<uint16_t>(accepted_bufs), std::memory_order_acq_rel);
        }
        TxCqePoller::Instance().MarkActive(umq_sock);
    }

    /* Translate accepted buf count back to accepted input segment count (§5.1).
     * A SMALL_DATA buf covers 1 segment; a READ_OFFER buf covers its nsegs. */
    ssize_t accepted_segs = 0;
    for (size_t i = 0; i < accepted_bufs; ++i) {
        accepted_segs += ctx.batch[i].segs;
    }
    *out = accepted_segs;
    (void)ctx.built_any_big;
    UBS_DATAPATH_LOG("[datapath] umq_post done, fd: %d, umqh: %llu, in_nsegs: %u, batch_bufs: %zu, accepted_bufs: %zu, "
                     "accepted_segs: %zd, ret: %d\n",
                     fd, umqh, data_list->nsegs, ctx.batch.size(), accepted_bufs, accepted_segs, ret);
    PROF_END(UBS_NATIVE_TRY_SENDER_POST, true);
    return true;
}

/* ---- RX control hook (design §6.2) ---- */

bool UbsBigdata::HandleRxControl(const SocketPtr &sock, umq_buf_t *qbuf)
{
    if (sock == nullptr || qbuf == nullptr) {
        UBS_VLOG_WARN("HandleRxControl invalid parameter: sock=%p qbuf=%p\n", sock.Get(), qbuf);
        return false;
    }
    if (qbuf->buf_data == nullptr || qbuf->data_size < UBS_CTRL_HDR_SIZE) {
        return false;
    }
    /* Content-based classification: callers rely on these field checks as
     * the primary classifier (not just defence-in-depth) because some URMA
     * bond layers strip imm_data bit 20, making is_big_ctrl unreliable.
     * A valid control header:
     *   - type in {READ_OFFER, READ_DONE, READ_ABORT}
     *   - total_len == data_size (control message fills the buffer exactly)
     *   - nsegs and nmempool_infos within protocol bounds (<= 32) */
    auto *ctrl = reinterpret_cast<const UbsCtrlHdr *>(qbuf->buf_data);
    if (ctrl->type != UBS_READ_OFFER && ctrl->type != UBS_READ_DONE && ctrl->type != UBS_READ_ABORT) {
        UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: bad type, fd: %d, type: %u, data_size: %u\n",
                         sock->raw_socket_, ctrl->type, qbuf->data_size);
        return false;
    }
    if (ctrl->total_len != qbuf->data_size) {
        UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: total_len!=data_size, fd: %d, type: %u, total_len: %u, "
                         "data_size: %u\n",
                         sock->raw_socket_, ctrl->type, ctrl->total_len, qbuf->data_size);
        return false;
    }
    if (ctrl->nsegs > UBS_SEG_MAX || ctrl->nmempool_infos > UBS_SEG_MAX) {
        UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: nsegs/ninfos out of range, fd: %d, type: %u, nsegs: %u, "
                         "nmempool_infos: %u\n",
                         sock->raw_socket_, ctrl->type, ctrl->nsegs, ctrl->nmempool_infos);
        return false;
    }
    /* READ_OFFER must carry at least 1 segment with 1:1 mempool infos.
     * READ_DONE/ABORT carry 0 segments. This filters data buffers whose
     * first byte coincidentally matches a control type. */
    if (ctrl->type == UBS_READ_OFFER) {
        if (ctrl->nsegs == 0 || ctrl->nsegs != ctrl->nmempool_infos) {
            UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: offer nsegs!=nmempool_infos, fd: %d, nsegs: %u, "
                             "nmempool_infos: %u\n",
                             sock->raw_socket_, ctrl->nsegs, ctrl->nmempool_infos);
            return false;
        }
    } else {
        if (ctrl->nsegs != 0 || ctrl->nmempool_infos != 0) {
            UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: done/abort carries segs, fd: %d, type: %u, nsegs: %u, "
                             "nmempool_infos: %u\n",
                             sock->raw_socket_, ctrl->type, ctrl->nsegs, ctrl->nmempool_infos);
            return false;
        }
    }
    const uint32_t segs_region = static_cast<uint32_t>(ctrl->nsegs) * UBS_SEG_SIZE;
    if (segs_region > qbuf->data_size - UBS_CTRL_HDR_SIZE) {
        return false;
    }
    const UbsSeg *segs = reinterpret_cast<const UbsSeg *>(qbuf->buf_data + UBS_CTRL_HDR_SIZE);
    uint32_t infos_bytes = 0;
    for (uint16_t i = 0; i < ctrl->nsegs; ++i) {
        const uint16_t info_len = segs[i].mempool_info_len;
        if (info_len < UBS_MEMPOOL_INFO_HDR_SIZE + UBS_URMA_SEG_T_SIZE) {
            return false;
        }
        if (infos_bytes > (qbuf->data_size - UBS_CTRL_HDR_SIZE - segs_region) - info_len) {
            return false;
        }
        infos_bytes += info_len;
    }
    const uint32_t expected_len = proto::ctrl_total_len(ctrl->nsegs, infos_bytes, ctrl->inline_data_len);
    if (ctrl->total_len != expected_len) {
        UBS_DATAPATH_LOG("[datapath] HandleRxControl reject: layout mismatch, fd: %d, type: %u, total_len: %u, "
                         "expected_len: %u, nsegs: %u, nmempool_infos: %u, inline_data_len: %u\n",
                         sock->raw_socket_, ctrl->type, ctrl->total_len, expected_len, ctrl->nsegs,
                         ctrl->nmempool_infos, ctrl->inline_data_len);
        return false;
    }

    auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
    if (umq_sock == nullptr) {
        UBS_VLOG_ERR("HandleRxControl convert to UmqSocket failed.\n");
        return false;
    }
    if (umq_sock->State() == SOCK_STAT_CLOSE || umq_sock->IsRetiring()) {
        UBS_VLOG_WARN("HandleRxControl skip: socket tearing down, fd: %d\n", sock->raw_socket_);
        return false;
    }
    const uint64_t umqh = umq_sock->UmqHandle();
    UbsBigdataSocketState *state = GetBigdataState(sock, true);

    if (GlobalSetting::UBS_MONITOR_ENABLE) {
        auto *sockBase = RefConvert<Socket, SocketBase>(sock).Get();
        if (sockBase != nullptr) {
            if (auto *mgr = sockBase->GetStatsMgr()) {
                mgr->UpdateTraceStats(Statistics::StatsMgr::BIGDATA_CTRL_RECV_COUNT, 1);
            }
        }
    }

    PROF_START(UBS_NATIVE_HANDLE_RX_CONTROL);
    switch (ctrl->type) {
        case UBS_READ_OFFER: {
            UBS_DATAPATH_LOG("[datapath] RX READ_OFFER (big), fd: %d, umqh: %llu, seq: %llu, nsegs: %u\n",
                             sock->raw_socket_, umqh, static_cast<unsigned long long>(ctrl->seq), ctrl->nsegs);
            UbsStageTrace(sock->raw_socket_,
                          reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext)->imm.user_data,
                          STAGE_HANDLE_RX_CTRL, UbsPktTraceNowNs());
            DoReadOffer(sock, umqh, qbuf);
            PROF_END(UBS_NATIVE_HANDLE_RX_CONTROL, true);
            return true;
        }
        case UBS_READ_DONE:
            UBS_DATAPATH_LOG("[datapath] RX READ_DONE (big), fd: %d, umqh: %llu, seq: %llu\n", sock->raw_socket_, umqh,
                             static_cast<unsigned long long>(ctrl->seq));
            ReleasePinned(state, ctrl->seq);
            UmqApi::umq_buf_free(qbuf);
            PROF_END(UBS_NATIVE_HANDLE_RX_CONTROL, true);
            return true;
        case UBS_READ_ABORT:
            UBS_DATAPATH_LOG("[datapath] RX READ_ABORT (big), fd: %d, umqh: %llu, seq: %llu\n", sock->raw_socket_, umqh,
                             static_cast<unsigned long long>(ctrl->seq));
            ReleasePinned(state, ctrl->seq);
            UmqApi::umq_buf_free(qbuf);
            PROF_END(UBS_NATIVE_HANDLE_RX_CONTROL, true);
            return true;
        default:
            UBS_VLOG_WARN("HandleRxControl unknown control type.\n");
            PROF_END(UBS_NATIVE_HANDLE_RX_CONTROL, false);
            return false;
    }
}

/* ---- TX completion hook (design §6.3) ---- */

bool UbsBigdata::HandleTxCompletion(Socket *sock, umq_buf_t *qbuf, uint32_t *completion_span,
                                    int *deferred_progress_fd)
{
    if (completion_span != nullptr) {
        *completion_span = 0;
    }
    if (deferred_progress_fd != nullptr) {
        *deferred_progress_fd = -1;
    }
    if (qbuf == nullptr) {
        return false;
    }
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
    if (pro == nullptr) {
        return false;
    }

    PROF_START(UBS_NATIVE_HANDLE_TX_COMPLETION);

    /* READ completion (receiver side): attribute via the slot. Each WR
     * produces its own CQE (span=1); FinalizeIo runs when all WRs of the
     * transaction complete (design §6.3). */
    if (pro->opcode == UMQ_OPC_READ) {
        PROF_START(UBS_NATIVE_TX_CQE_READ);
        /* RNR 软反压通知（status=99）：URMA bondp 重试中上报的通知 CQE，数据 WR
         * 仍在途，不是最终完成。若在此处累计完成计数（MarkDataWrComplete），
         * wr_total=1 时第一次 status=99 即 completed==wr_total → 误触发 FinalizeIo
         * 提前释放 READ buf + 断链，而 bondp 重试完成后新 CQE 携带同一 buf →
         * double free / UAF。此处返回 false，让 PollUmqTxInternal 统一走
         * HandleRnrNotify（仅置反压、不释放 buf），由最终 CQE（status=0 成功 /
         * status=10 耗尽）走正常路径释放。 */
        if (qbuf->status == UMQ_BUF_RNR_RETRY_CNT_EXC) {
            PROF_END(UBS_NATIVE_TX_CQE_READ, true);
            PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
            return false;
        }
        auto *slot = reinterpret_cast<detail::UbsBigQbufSlot *>(pro->user_ctx);
        if (slot == nullptr) {
            qbuf->qbuf_next = nullptr;
            UmqApi::umq_buf_free(qbuf);
            PROF_END(UBS_NATIVE_TX_CQE_READ, true);
            PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
            return true;
        }
        auto *ctx = static_cast<UbsBigIoCtx *>(slot->owner);
        UbsStageTrace(ctx->fd, ctx->first_sn, STAGE_READ_CQE, UbsPktTraceNowNs());
        /* Hold a state ref across FinalizeIo and the retry pumps below: the
         * terminal CQE may delete the last ctx (and with it the state, once
         * teardown dropped the owner ref) while this thread still uses it. */
        BigdataStateRef state_ref(GetBigdataState(sock, false));
        if (qbuf->status != 0) {
            ctx->failed.store(true, std::memory_order_release);
        }
        const uint32_t span = slot->completion_span != 0 ? slot->completion_span : 1;
        const BigDataCompletionProgress progress = MarkDataWrComplete(ctx, span);
        if (completion_span != nullptr) {
            *completion_span = span;
        }
        /* Report the READ CQE's fd so PollUmqTxInternal restores
         * tx_queue_avail_num_ for the socket that submitted the READ WRs.
         * DoReadOffer did fetch_sub(read_wr_count) on post; without this
         * fetch_add the SQ slot count leaks downward and eventually stalls
         * at SQ-full (status:12). */
        if (deferred_progress_fd != nullptr) {
            *deferred_progress_fd = ctx->fd;
        }
        UBS_DATAPATH_LOG("[datapath] READ TX CQE (big), fd: %d, seq: %llu, first_sn: %u, status: %d, span: %u, "
                         "finalize: %d\n",
                         ctx->fd, static_cast<unsigned long long>(ctx->seq), ctx->first_sn, qbuf->status, span,
                         static_cast<int>(progress.finalize));
        if (progress.finalize) {
            FinalizeIo(ctx);
            /* Only drain/retry on the finalizing CQE: intermediate READ CQEs
             * (multi-WR transactions) would redundantly lock state->mutex
             * and check empty queues. The final CQE sees the complete state
             * and handles all pending deferred controls and reads. */
            UbsBigdataSocketState *state = state_ref.Get();
            if (state != nullptr) {
                DrainDeferredControls(state);
                auto *umq_sock = dynamic_cast<UmqSocket *>(sock); /* issue#38：裸转换，零引用操作 */
                if (umq_sock != nullptr && !umq_sock->IsRetiring()) {
                    RetryPendingReads(state, umq_sock->UmqHandle(), umq_sock);
                }
            }
        }
        PROF_END(UBS_NATIVE_TX_CQE_READ, true);
        PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
        return true;
    }

    /* Control SEND_IMM completion (both ends): release the control buffer and
     * pump the EAGAIN-retry queues. Control packets use SEND_IMM (so umq_ub_fill_wr
     * fills imm) with bit 20 set (is_big_ctrl); SMALL_DATA and ordinary data also
     * use SEND_IMM but leave bit 20 clear, so they fall through to the normal
     * ProcessTxCqe path. The buf_data guard rejects any non-control buffer that
     * somehow reached here with bit 20 set but a too-small data region. */
    if (pro->opcode == UMQ_OPC_SEND_IMM && proto::is_big_ctrl(pro->imm_data)) {
        PROF_START(UBS_NATIVE_TX_CQE_CTRL);
        if (qbuf->buf_data == nullptr || qbuf->data_size < UBS_CTRL_HDR_SIZE) {
            PROF_END(UBS_NATIVE_TX_CQE_CTRL, false);
            PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, false);
            return false;
        }
        /* RNR 软反压通知（status=99）：控制消息 WR 仍在 bondp 重试中，不能提前
         * umq_buf_free——否则 bondp 复用 user_ctx 重发完成后新 CQE 携带同一 buf
         * → double free。返回 false，由 PollUmqTxInternal 统一走 HandleRnrNotify
         * （仅置反压不释放），最终 CQE 走正常路径释放。 */
        if (qbuf->status == UMQ_BUF_RNR_RETRY_CNT_EXC) {
            PROF_END(UBS_NATIVE_TX_CQE_CTRL, true);
            PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
            return false;
        }
        UBS_DATAPATH_LOG("[datapath] control SEND TX CQE (big), status: %d\n", qbuf->status);
        qbuf->qbuf_next = nullptr;
        UmqApi::umq_buf_free(qbuf);
        if (completion_span != nullptr) {
            *completion_span = 1;
        }
        /* Balance TrySenderPost's fetch_sub for this BIG_CTRL control buf.
         * When bit 20 is preserved (is_big_ctrl true), the CQE lands here
         * instead of ProcessTxCqe; without setting deferred_progress_fd,
         * PollUmqTxInternal would skip fetch_add, leaving fetch_sub
         * unmatched and avail leaking downward. */
        if (deferred_progress_fd != nullptr) {
            *deferred_progress_fd = sock->raw_socket_;
        }
        /* A control SEND completed: retry any EAGAIN-paused control packets
         * and EAGAIN-paused READ transactions (design §9). */
        BigdataStateRef state_ref(GetBigdataState(sock, false));
        UbsBigdataSocketState *state = state_ref.Get();
        DrainDeferredControls(state);
        auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
        if (umq_sock != nullptr && state != nullptr && !umq_sock->IsRetiring()) {
            RetryPendingReads(state, umq_sock->UmqHandle(), umq_sock);
        }
        PROF_END(UBS_NATIVE_TX_CQE_CTRL, true);
        PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
        return true;
    }
    /* Regular data SEND completion (SMALL_DATA / READ_OFFER): SQ slots freed.
     * Retry EAGAIN/ENOMEM-paused READ transactions and deferred controls, since
     * the control-SEND-completion path may not fire when control SENDs also
     * fail with ENOMEM and flow control is disabled (design §9). */
    PROF_START(UBS_NATIVE_TX_CQE_SEND);
    {
        BigdataStateRef state_ref(GetBigdataState(sock, false));
        UbsBigdataSocketState *state = state_ref.Get();
        if (state != nullptr) {
            bool need_retry = false;
            {
                Locker lk(state->mutex);
                need_retry = !state->PendingReadsEmpty() || !state->DeferredCtrlEmpty();
            }
            if (need_retry) {
                DrainDeferredControls(state);
                auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
                if (umq_sock != nullptr && !umq_sock->IsRetiring()) {
                    RetryPendingReads(state, umq_sock->UmqHandle(), umq_sock);
                }
            }
        }
    }
    PROF_END(UBS_NATIVE_TX_CQE_SEND, true);
    PROF_END(UBS_NATIVE_HANDLE_TX_COMPLETION, true);
    return false;
}

void UbsBigdata::HandleFlowControlUpdate(const SocketPtr &sock)
{
    BigdataStateRef state_ref(GetBigdataState(sock, false));
    UbsBigdataSocketState *state = state_ref.Get();
    UBS_DATAPATH_LOG("[datapath] FC_UPDATE, fd: %d, has_state: %d\n", sock != nullptr ? sock->raw_socket_ : -1,
                     static_cast<int>(state != nullptr));
    DrainDeferredControls(state);
    /* TX credit / queue space may have recovered: retry EAGAIN-paused READ
     * transactions (design §9). */
    auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
    if (umq_sock != nullptr && state != nullptr && !umq_sock->IsRetiring()) {
        RetryPendingReads(state, umq_sock->UmqHandle(), umq_sock);
    }
}

void UbsBigdata::RetryPendingReadsForSocket(const SocketPtr &sock)
{
    PROF_START(UBS_NATIVE_RETRY_READS_FOR_SOCK);
    if (sock == nullptr) {
        PROF_END(UBS_NATIVE_RETRY_READS_FOR_SOCK, false);
        return;
    }
    auto *umq_sock = RefConvert<Socket, UmqSocket>(sock).Get();
    if (umq_sock != nullptr &&
        (umq_sock->State() == SOCK_STAT_CLOSE || umq_sock->IsRetiring())) {
        PROF_END(UBS_NATIVE_RETRY_READS_FOR_SOCK, true);
        return;
    }
    UbsBigdataSocketState *state = GetBigdataState(sock, false);
    if (state == nullptr) {
        PROF_END(UBS_NATIVE_RETRY_READS_FOR_SOCK, false);
        return;
    }
    bool need_retry = false;
    {
        Locker lk(state->mutex);
        need_retry = !state->PendingReadsEmpty();
    }
    if (!need_retry) {
        PROF_END(UBS_NATIVE_RETRY_READS_FOR_SOCK, true);
        return;
    }
    if (umq_sock != nullptr) {
        RetryPendingReads(state, umq_sock->UmqHandle(), umq_sock);
    }
    PROF_END(UBS_NATIVE_RETRY_READS_FOR_SOCK, true);
}

/* design §4.5: sweep expired receiver ctxs. pending_reads (WRs not posted)
 * can be freed directly; active_io (WRs in flight) are marked failed and left
 * for FinalizeIo to clean up when CQEs arrive. */
static void SweepExpiredCtxs(UbsBigdataSocketState *state)
{
    if (state == nullptr) {
        return;
    }
    const uint64_t now_ns = Func::CurrentTimeNs();
    std::vector<UbsBigIoCtx *> to_delete;
    {
        Locker lk(state->mutex);
        if (state->destroying.load(std::memory_order_acquire)) {
            return;
        }
        /* pending_reads: WRs never posted → safe to free bufs and delete ctx */
        if (state->pending_reads != nullptr) {
            for (auto it = state->pending_reads->begin(); it != state->pending_reads->end();) {
                UbsBigIoCtx *ctx = *it;
                if (ctx->deadline_ns != 0 && now_ns >= ctx->deadline_ns) {
                    it = state->pending_reads->erase(it);
                    /* paused ctx 在 DoReadOffer 也被插入 active_io: 必须同步移除,
                     * 否则同一次 sweep 的 active_io 循环对存活 ctx 重复计数
                     * rx_ctx_timeout(delta=2),且下次 sweep 解引用已删除 ctx(UAF) */
                    state->active_io.erase(ctx);
                    to_delete.push_back(ctx);
                    g_rx_ctx_timeout_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++it;
                }
            }
        }
        /* active_io: WRs in flight → mark failed, let FinalizeIo clean up.
         * ctx_with_deadline is NOT decremented here — FinalizeIo/DeleteIoUctx
         * owns the cleanup for active_io ctxs (they're not removed from
         * active_io in this sweep, just marked failed). */
        for (UbsBigIoCtx *ctx : state->active_io) {
            if (ctx->deadline_ns != 0 && now_ns >= ctx->deadline_ns &&
                !ctx->failed.load(std::memory_order_acquire)) {
                ctx->failed.store(true, std::memory_order_release);
                g_rx_ctx_timeout_count.fetch_add(1, std::memory_order_relaxed);
                UBS_DATAPATH_LOG("[datapath] SweepExpiredCtxs active_io ctx timed out, seq: %llu, fd: %d\n",
                                 static_cast<unsigned long long>(ctx->seq), ctx->fd);
            }
        }
    }
    /* Free bufs for pending_reads ctxs outside the lock */
    for (UbsBigIoCtx *ctx : to_delete) {
        UBS_DATAPATH_LOG("[datapath] SweepExpiredCtxs pending_read ctx timed out, seq: %llu, fd: %d\n",
                         static_cast<unsigned long long>(ctx->seq), ctx->fd);
        for (auto &slot : ctx->wr_slots) {
            if (slot.pending != nullptr) {
                FreeQbufChain(slot.pending);
                slot.pending = nullptr;
            }
        }
        FreeQbufChain(ctx->offer_rx_buf);
        ctx->offer_rx_buf = nullptr;
        DeleteIoUctx(ctx);
    }
}

bool UbsBigdata::NeedsPollerAttention(const SocketPtr &sock)
{
    if (sock == nullptr) {
        return false;
    }
    UbsBigdataSocketState *state = GetBigdataState(sock, false);
    if (state == nullptr) {
        return false;
    }
    if (state->pinned_with_deadline.load(std::memory_order_acquire) != 0 ||
        state->ctx_with_deadline.load(std::memory_order_acquire) != 0) {
        return true;
    }
    Locker lk(state->mutex);
    return !state->PendingReadsEmpty();
}

void UbsBigdata::SweepExpiredForSocket(const SocketPtr &sock)
{
    PROF_START(UBS_NATIVE_SWEEP_FOR_SOCK);
    if (sock == nullptr) {
        PROF_END(UBS_NATIVE_SWEEP_FOR_SOCK, false);
        return;
    }
    /* design §9 perf: skip entirely when feature is off — no entries can
     * have non-zero deadlines, so sweep is a pure no-op. Avoids RefConvert
     * + GetBigdataState + atomic loads on every poller tick (4 calls/socket
     * per tick). The branch is highly predictable (always-false when feature
     * is off), costing ~1ns vs ~100ns for the full fast-path. */
    if (!GlobalSetting::UBS_READ_GEN_CHECK_ENABLED) {
        PROF_END(UBS_NATIVE_SWEEP_FOR_SOCK, true);
        return;
    }
    /* design §9 perf: fast-path — skip the sweep (state lookup + 2x
     * CurrentTimeNs + 2x mutex lock + container iteration) when there are
     * zero entries with non-zero deadlines. Common when effective_timeout=0
     * or all offers fallback to gen=0. */
    PROF_START(UBS_NATIVE_SWEEP_FASTPATH);
    UbsBigdataSocketState *state = GetBigdataState(sock, false);
    if (state == nullptr) {
        PROF_END(UBS_NATIVE_SWEEP_FASTPATH, false);
        PROF_END(UBS_NATIVE_SWEEP_FOR_SOCK, true);
        return;
    }
    if (state->pinned_with_deadline.load(std::memory_order_relaxed) == 0 &&
        state->ctx_with_deadline.load(std::memory_order_relaxed) == 0) {
        PROF_END(UBS_NATIVE_SWEEP_FASTPATH, true);
        PROF_END(UBS_NATIVE_SWEEP_FOR_SOCK, true);
        return;
    }
    PROF_END(UBS_NATIVE_SWEEP_FASTPATH, true);
    PROF_START(UBS_NATIVE_SWEEP_FULL);
    SweepExpiredPinned(state);
    SweepExpiredCtxs(state);
    PROF_END(UBS_NATIVE_SWEEP_FULL, true);
    PROF_END(UBS_NATIVE_SWEEP_FOR_SOCK, true);
}

void UbsBigdata::CleanupSocketState(UmqSocket *sock)
{
    if (sock == nullptr) {
        return;
    }
    UbsBigdataSocketState *state = sock->ReleaseBigdataState();
    if (state == nullptr) {
        return;
    }
    /* Mark the state as failed so in-flight completion handlers skip
     * state access and avoid use-after-free during teardown. */
    state->destroying.store(true, std::memory_order_release);
    /* Release any still-pinned source Blocks and deferred control packets so
     * they do not leak on close (design §8). */
    std::unordered_map<uint64_t, PinnedEntry> pinned;
    std::deque<UbsDeferredCtrl> deferred;
    std::deque<UbsBigIoCtx *> pending;
    std::unordered_set<UbsBigIoCtx *> active;
    {
        Locker lk(state->mutex);
        pinned.swap(state->pinned);
        if (state->deferred_ctrl != nullptr) {
            deferred.swap(*state->deferred_ctrl);
        }
        if (state->pending_reads != nullptr) {
            pending.swap(*state->pending_reads);
        }
        active.swap(state->active_io);
    }
    for (auto &kv : pinned) {
        for (Block *b : kv.second.blocks) {
            if (b != nullptr) {
                b->DecRef();
            }
        }
    }
    for (auto &item : deferred) {
        FreeQbufChain(item.qbuf);
    }
    /* EAGAIN-paused READ transactions: their WRs were never posted, so no
     * CQE will ever arrive — free their WRs and retained OFFER RX buffer
     * here. A paused ctx is ALSO in active (it was inserted there before
     * being enqueued as pending), so erase it from the active set first to
     * avoid freeing the same ctx twice. */
    for (auto *ctx : pending) {
        active.erase(ctx);
        for (auto &slot : ctx->wr_slots) {
            if (slot.pending != nullptr) {
                slot.pending->qbuf_next = nullptr; /* detach inter-slot chain */
            }
            FreeQbufChain(slot.pending);
        }
        FreeQbufChain(ctx->offer_rx_buf);
        DeleteIoUctx(ctx);
    }
    /* In-flight READ transactions: their WRs are posted on the hardware.
     * After teardown the hardware flushes them and emits error CQEs; the
     * CQE path (HandleTxCompletion -> MarkDataWrComplete -> FinalizeIo)
     * frees wr_slots / offer_rx_buf and deletes ctx. So here we only mark
     * failed — freeing the buffers now would let the pending CQE double-free
     * them. Each ctx keeps its state ref, so the state survives until the
     * last in-flight ctx finalizes (DeleteIoUctx); only then does the CQE
     * thread run the final DecreaseRef (this thread's owner ref is dropped below). */
    for (auto *ctx : active) {
        ctx->failed.store(true, std::memory_order_release);
    }
    /* Drop the owner ref. The state is deleted by the last DecreaseRef: here
     * when no ctx (and no transient user such as DoReadOffer /
     * HandleTxCompletion) is still holding it, otherwise on that thread. */
    state->DecreaseRef();
}

/* design §9 (A8): snapshot monitoring counters for external observability. */
UbsBigdata::GenCheckStats UbsBigdata::GetGenCheckStats()
{
    GenCheckStats s;
    s.pin_timeout      = g_pin_timeout_count.load(std::memory_order_relaxed);
    s.gen_mismatch     = g_read_gen_mismatch_count.load(std::memory_order_relaxed);
    s.gen_fallback     = g_read_gen_fallback_count.load(std::memory_order_relaxed);
    s.rx_ctx_timeout   = g_rx_ctx_timeout_count.load(std::memory_order_relaxed);
    s.offer_total      = g_offer_total_count.load(std::memory_order_relaxed);
    s.pin_alive_max_ms = g_pin_alive_max_ms.load(std::memory_order_relaxed);
    return s;
}

/* design §9 (A8): atexit stats dump for integration tests.
 * Activated by ENV UBS_GEN_CHECK_STATS_DUMP=1. Prints a single summary
 * line at process exit so test scripts can grep for it. */
static void DumpGenCheckStatsAtExit()
{
    UbsBigdata::GenCheckStats s = UbsBigdata::GetGenCheckStats();
    UBS_VLOG_INFO("[GenCheckStats] pin_timeout=%llu, gen_mismatch=%llu, gen_fallback=%llu, "
                  "rx_ctx_timeout=%llu, offer_total=%llu, pin_alive_max_ms=%llu\n",
                  static_cast<unsigned long long>(s.pin_timeout),
                  static_cast<unsigned long long>(s.gen_mismatch),
                  static_cast<unsigned long long>(s.gen_fallback),
                  static_cast<unsigned long long>(s.rx_ctx_timeout),
                  static_cast<unsigned long long>(s.offer_total),
                  static_cast<unsigned long long>(s.pin_alive_max_ms));
}

/* design §9 (A8): periodic stats dump thread for integration tests.
 * Since test scripts typically kill the server with SIGTERM (atexit does
 * not fire), we also start a background thread that dumps stats every
 * few seconds so the test can grep the log while the server is running. */
static void StatsDumpThread()
{
    /* First dump after a brief warm-up so loading logs are not interleaved */
    std::this_thread::sleep_for(std::chrono::seconds(3));
    while (true) {
        DumpGenCheckStatsAtExit();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

namespace {
struct StatsDumpRegistrar {
    StatsDumpRegistrar()
    {
        const char *env = std::getenv("UBS_GEN_CHECK_STATS_DUMP");
        if (env != nullptr && env[0] == '1') {
            std::atexit(DumpGenCheckStatsAtExit);
            /* Also start periodic dump (daemon thread, destroyed on exit) */
            std::thread(StatsDumpThread).detach();
        }
    }
};
static StatsDumpRegistrar g_stats_dump_registrar;
} // namespace

} // namespace ubs
} // namespace ock
