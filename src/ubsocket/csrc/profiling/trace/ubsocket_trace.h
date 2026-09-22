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
#ifndef UBS_COMM_UBSOCKET_TRACE_H
#define UBS_COMM_UBSOCKET_TRACE_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "include/ubsocket_def.h"

namespace ock {
namespace ubs {

class Socket;

enum TracePath : uint8_t {
    PATH_TX_WRITEV = 0,
    PATH_TX_POST   = 1,
    PATH_RX_READV  = 2,
    PATH_RX_POLL   = 3,
};

inline bool IsTxPath(uint8_t path) noexcept { return path == PATH_TX_WRITEV || path == PATH_TX_POST; }

enum TxWritevPhase : uint16_t {
    TX_WV_ENTRY = 0,
    TX_WV_BUILD_IOV,
    TX_WV_ALLOC_BUF,
    TX_WV_MEM_COPY,
    TX_WV_UMQ_POST,
    TX_WV_EXIT,
    TX_WV_ASYNC_UMQ_POLL,
    TX_WV_ASYNC_PROCESS_CQE,
    TX_WV_ASYNC_BUF_FREE,
    TX_WV_ASYNC_NOTIFY,
    TX_WV_PHASE_COUNT,
};

enum TxPostPhase : uint16_t {
    TX_POST_ENTRY = 0,
    TX_POST_SENDER_POST,
    TX_POST_HANDLE_SMALL,
    TX_POST_HANDLE_LARGE,
    TX_POST_UMQ_POST,
    TX_POST_EXIT,
    TX_POST_ASYNC_UMQ_POLL,
    TX_POST_ASYNC_PROCESS_CQE,
    TX_POST_ASYNC_BUF_FREE,
    TX_POST_ASYNC_NOTIFY,
    TX_POST_PHASE_COUNT,
};

enum RxReadvPhase : uint16_t {
    RX_RV_ENTRY = 0,
    RX_RV_POLL_RX,
    RX_RV_HANDLE_BUF,
    RX_RV_DATA_SET,
    RX_RV_REARM,
    RX_RV_EXIT,
    RX_RV_ASYNC_UMQ_POLL,
    RX_RV_ASYNC_SIFT,
    RX_RV_ASYNC_ENQUEUE,
    RX_RV_ASYNC_NOTIFY,
    RX_RV_ASYNC_ALLOC_BUF,
    RX_RV_ASYNC_POST_RX,
    RX_RV_ASYNC_REARM,
    RX_RV_PHASE_COUNT,
};

enum RxPollPhase : uint16_t {
    RX_POLL_ENTRY = 0,
    RX_POLL_GET_AND_POP,
    RX_POLL_BIG_CTRL,
    RX_POLL_DELIVER_SEG,
    RX_POLL_EXIT,
    RX_POLL_ASYNC_UMQ_POLL,
    RX_POLL_ASYNC_SIFT,
    RX_POLL_ASYNC_ENQUEUE,
    RX_POLL_ASYNC_NOTIFY,
    RX_POLL_ASYNC_ALLOC_BUF,
    RX_POLL_ASYNC_POST_RX,
    RX_POLL_ASYNC_REARM,
    RX_POLL_PHASE_COUNT,
};

template <uint16_t N>
struct PhaseData {
    uint64_t phase_start[N];
    uint64_t phase_end[N];
    std::atomic<uint64_t> phase_bitmap[(N + 63) / 64];

    void ResetBitmap() noexcept
    {
        for (uint16_t i = 0; i < (N + 63) / 64; i++) {
            phase_bitmap[i].store(0, std::memory_order_relaxed);
        }
        for (uint16_t i = 0; i < N; i++) {
            phase_start[i] = 0;
            phase_end[i] = 0;
        }
    }

    void RecordPhase(uint16_t idx, uint64_t s, uint64_t e) noexcept
    {
        if (idx >= N) {
            return;
        }
        phase_start[idx] = s;
        phase_end[idx] = e;
        std::atomic_thread_fence(std::memory_order_release);
        phase_bitmap[idx / 64].fetch_or(1ULL << (idx % 64), std::memory_order_relaxed);
    }

    bool HasPhase(uint16_t idx) const noexcept
    {
        if (idx >= N) {
            return false;
        }
        return (phase_bitmap[idx / 64].load(std::memory_order_acquire) & (1ULL << (idx % 64))) != 0;
    }
};

struct TraceSlot {
    static constexpr uint32_t INVALID_SEQ = 0;
    static constexpr uint8_t STATE_IDLE = 0;
    static constexpr uint8_t STATE_TRACING = 1;
    static constexpr uint8_t STATE_DONE = 2;

    std::atomic<uint8_t> state{0};
    uint8_t path{0};
    uint32_t seq_no{0};
    int32_t fd{-1};
    uint64_t io_start_ts{0};
    uint64_t io_end_ts{0};
    uint32_t data_size{0};
    uint32_t offset{0};

    union {
        PhaseData<TX_WV_PHASE_COUNT>   tx_writev;
        PhaseData<TX_POST_PHASE_COUNT>  tx_post;
        PhaseData<RX_RV_PHASE_COUNT>    rx_readv;
        PhaseData<RX_POLL_PHASE_COUNT>   rx_poll;
    } phases;

    void Reset() noexcept;
    void RecordPhase(uint8_t p, uint16_t idx, uint64_t s, uint64_t e) noexcept;
};

class GlobalTracePool {
public:
    static constexpr uint16_t MAX_SLOTS = 256;
    static constexpr uint32_t LOOKUP_SIZE = 8192;  // O(1) 查找表大小 (2 的幂)
    static constexpr uint32_t LOOKUP_MASK = LOOKUP_SIZE - 1;

    static GlobalTracePool &Instance();

    int16_t AllocSlot(uint32_t seqNo, int fd, uint8_t path) noexcept;
    void EndSlot(int16_t slotIdx, uint32_t dataSize, uint32_t offset) noexcept;
    TraceSlot *FindSlot(uint32_t seqNo, uint8_t path, int fd) noexcept;
    TraceSlot &Slot(int16_t idx) noexcept { return slots_[static_cast<uint16_t>(idx)]; }
    void DrainAll(uint64_t now) noexcept;
    void ClearLookup(uint32_t seqNo, int fd) noexcept;

    bool LazyInit() noexcept;
    void DestroyPool() noexcept;
    bool IsReady() const noexcept { return slots_ != nullptr; }

    std::atomic<uint16_t> done_count_{0};

    static uint32_t MixLookupKey(uint32_t seqNo, int fd) noexcept
    {
        return seqNo ^ (static_cast<uint32_t>(fd) * 0x9E3779B9u);
    }

    static uint64_t PackLookup(uint32_t seqNo, int fd, uint16_t slotIdx) noexcept
    {
        return (static_cast<uint64_t>(seqNo))
             | (static_cast<uint64_t>(static_cast<uint32_t>(fd)) << 32)
             | (static_cast<uint64_t>(slotIdx) << 56);
    }

    static bool UnpackLookup(uint64_t packed, uint32_t seqNo, int fd, uint16_t &outSlot) noexcept
    {
        if (packed == 0) return false;
        if (static_cast<uint32_t>(packed) != seqNo) return false;
        if (static_cast<int32_t>((packed >> 32) & 0xFFFFFF) != fd) return false;
        outSlot = static_cast<uint16_t>(packed >> 56);
        return true;
    }

private:
    GlobalTracePool() = default;
    ~GlobalTracePool() { DestroyPool(); }

    TraceSlot *slots_{nullptr};
    std::atomic<uint16_t> next_hint_{0};
    std::atomic<uint64_t> *lookup_table_{nullptr};
};

class SplitTraceDrainThread {
public:
    static SplitTraceDrainThread &Instance();
    ~SplitTraceDrainThread();
    void Start();
    void Stop();
    static void EnsureSplitTraceDir() noexcept;
    static const char *PhaseName(uint8_t path, uint16_t idx) noexcept;
    static int FormatSlot(char *buf, int bufsize, const TraceSlot &slot) noexcept;

private:
    void Run();
    void DrainAll();

    std::thread thread_;
    std::atomic<bool> running_{false};
    uint64_t drain_count_{0};
};

bool SplitTraceTrySample(Socket *sock, int fd, uint32_t seqNo, uint8_t path);
void SplitTraceAdd(Socket *sock, uint8_t path, uint16_t idx,
                   uint32_t seqNo, uint64_t startTs, uint64_t endTs);
void SplitTraceEndSample(Socket *sock, uint8_t path, uint32_t seqNo,
                         uint32_t dataSize, uint32_t offset);
void SplitTraceAddSampled(int fd, uint8_t path, uint16_t idx,
                          uint32_t seqNo,
                          uint64_t startTs, uint64_t endTs);

#define STRACE_TRY_SAMPLE(sock, fd, seq_no, path) \
    ::ock::ubs::SplitTraceTrySample(sock, fd, seq_no, path)

#define STRACE_ADD(sock, path, idx, seq_no, start_ts, end_ts) \
    ::ock::ubs::SplitTraceAdd(sock, path, idx, seq_no, start_ts, end_ts)

#define STRACE_END(sock, path, seq_no, data_size, offset) \
    ::ock::ubs::SplitTraceEndSample(sock, path, seq_no, data_size, offset)

#define STRACE_SAMPLED(fd, path, idx, seq_no, start_ts, end_ts) \
    ::ock::ubs::SplitTraceAddSampled(fd, path, idx, seq_no, start_ts, end_ts)

struct SplitTraceInfo;
class TraceRegistry {
public:
    static Result RegisterRpcIdOps(u_external_rpc_id_ops_t *ops);

public:
    static u_external_rpc_id_ops_t RPC_ID_OPS;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_TRACE_H
