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
#include <memory>
#include <thread>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "include/ubsocket_def.h"

namespace ock {
namespace ubs {

struct SplitTraceInfo {
    int raw_socket = -1;
    int peer_socket = -1;
    uint64_t rpc_id = -1;
    uint32_t seq_no = 0;
    uint32_t data_size = 0;
    uint32_t offset = 0;
    ProfilingTPId type = CORE_WRITE;
    uint32_t poll_num = 0;
    bool is_first = false;
    int tid = 0;
    uint64_t start_timestamp = 0;
    uint64_t end_timestamp = 0;
};

class TraceRegistry {
public:
    static Result RegisterRpcIdOps(u_external_rpc_id_ops_t *ops);

public:
    static u_external_rpc_id_ops_t RPC_ID_OPS;
};

class SplitTrace {
    static constexpr uint32_t DEFAULT_BUF_CAPACITY = 65535;
    static constexpr uint32_t DEFAULT_DRAIN_INTERVAL_MS = 10;

    struct TraceBuffer {
        std::unique_ptr<SplitTraceInfo[]> data;
        uint32_t capacity{0};
        uint32_t count{0};
        uint32_t dropped_count{0};
        alignas(64) std::atomic<bool> frozen{false};

        TraceBuffer() = default;
        explicit TraceBuffer(uint32_t cap) : data(std::make_unique<SplitTraceInfo[]>(cap)), capacity(cap) {}
        TraceBuffer(TraceBuffer &&other) noexcept
            : data(std::move(other.data)),
              capacity(other.capacity),
              count(other.count),
              dropped_count(other.dropped_count),
              frozen(other.frozen.load(std::memory_order_relaxed))
        {
        }
        TraceBuffer &operator=(TraceBuffer &&other) noexcept
        {
            if (this != &other) {
                data = std::move(other.data);
                capacity = other.capacity;
                count = other.count;
                dropped_count = other.dropped_count;
                frozen.store(other.frozen.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }
        TraceBuffer(const TraceBuffer &) = delete;
        TraceBuffer &operator=(const TraceBuffer &) = delete;

        void Reset()
        {
            count = 0;
            dropped_count = 0;
        }
    };

    uint32_t buf_capacity_{DEFAULT_BUF_CAPACITY};
    uint32_t swap_threshold_{DEFAULT_BUF_CAPACITY / 2};

    alignas(64) std::atomic<uint32_t> write_active_idx_{0};
    TraceBuffer write_bufs_[2];

    alignas(64) std::atomic<uint32_t> read_active_idx_{0};
    TraceBuffer read_bufs_[2];

    alignas(64) std::atomic<uint32_t> epoll_active_idx_{0};
    TraceBuffer epoll_bufs_[2];

    alignas(64) std::atomic<uint32_t> pending_rx_seq_no_{0};

public:
    static bool &SuppressTrace()
    {
        static thread_local bool value = false;
        return value;
    }

    void NotifyJfrRxSeqNo(uint32_t seq_no)
    {
        uint32_t expected = 0;
        pending_rx_seq_no_.compare_exchange_strong(expected, seq_no, std::memory_order_release,
                                                   std::memory_order_relaxed);
    }
    uint32_t ClaimJfrRxSeqNo()
    {
        return pending_rx_seq_no_.exchange(0, std::memory_order_acquire);
    }

    SplitTrace()
    {
        buf_capacity_ = GlobalSetting::UBS_SPLIT_TRACE_BUF_CAPACITY;
        swap_threshold_ = buf_capacity_ / 2;
        write_bufs_[0] = TraceBuffer(buf_capacity_);
        write_bufs_[1] = TraceBuffer(buf_capacity_);
        read_bufs_[0] = TraceBuffer(buf_capacity_);
        read_bufs_[1] = TraceBuffer(buf_capacity_);
        epoll_bufs_[0] = TraceBuffer(buf_capacity_);
        epoll_bufs_[1] = TraceBuffer(buf_capacity_);
    }

    void AddBrpcWriteTrace(ProfilingTPId type, int raw_socket)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        auto rpc_id = TraceRegistry::RPC_ID_OPS.get_rpc_id ? TraceRegistry::RPC_ID_OPS.get_rpc_id() : nullptr;
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.raw_socket = raw_socket;
        trace_info.rpc_id = reinterpret_cast<uint64_t>(rpc_id);
        trace_info.seq_no = 0;
        trace_info.type = type;

        auto brpc_call_timestamp = TraceRegistry::RPC_ID_OPS.get_rpc_call_timestamp ?
                                       TraceRegistry::RPC_ID_OPS.get_rpc_call_timestamp() :
                                       nullptr;
        trace_info.start_timestamp = reinterpret_cast<uint64_t>(brpc_call_timestamp);
        buf.count++;
    }

    void AddWriteTrace(ProfilingTPId type, int raw_socket)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        auto rpc_id = TraceRegistry::RPC_ID_OPS.get_rpc_id ? TraceRegistry::RPC_ID_OPS.get_rpc_id() : nullptr;
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.raw_socket = raw_socket;
        trace_info.rpc_id = reinterpret_cast<uint64_t>(rpc_id);
        trace_info.seq_no = 0;
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
        buf.count++;
    }

    void AddWriteTrace(ProfilingTPId type, int raw_socket, uint32_t seq_no, uint32_t data_size, uint32_t offset,
                       bool is_first)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        auto rpc_id = TraceRegistry::RPC_ID_OPS.get_rpc_id ? TraceRegistry::RPC_ID_OPS.get_rpc_id() : nullptr;
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = reinterpret_cast<uint64_t>(rpc_id);
        trace_info.raw_socket = raw_socket;
        trace_info.seq_no = seq_no;
        trace_info.data_size = data_size;
        trace_info.offset = offset;
        trace_info.type = type;
        trace_info.is_first = is_first;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
        buf.count++;
    }

    void UpdateWriteFirstTrace(ProfilingTPId type, uint32_t seq_no, uint32_t data_size, uint32_t offset, bool is_first,
                               uint32_t expected_backfill_count = 1)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count == 0) {
            return;
        }
        uint32_t pos = buf.count;
        int current_tid = static_cast<int>(syscall(SYS_gettid));
        for (uint32_t i = buf.count; i-- > 0;) {
            if (buf.data[i].type == type && buf.data[i].tid == current_tid) {
                pos = i;
                break;
            }
        }
        if (pos >= buf.count) {
            return;
        }
        uint32_t backfill_count = 0;
        for (uint32_t i = pos; i < buf.count; ++i) {
            if (buf.data[i].tid != current_tid) {
                continue;
            }
            if (buf.data[i].seq_no != 0) {
                break;
            }
            buf.data[i].seq_no = seq_no;
            buf.data[i].data_size = data_size;
            buf.data[i].offset = offset;
            buf.data[i].is_first = is_first;
            ++backfill_count;
        }
    }

    void UpdateWriteLastTrace(ProfilingTPId type, uint32_t data_size, uint32_t offset)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count == 0) {
            return;
        }
        auto &trace_info = buf.data[buf.count - 1];
        trace_info.data_size = data_size;
        trace_info.offset = offset;
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
    }

    void UpdateWriteLastTraceEndTime(ProfilingTPId type)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count == 0) {
            return;
        }
        auto &trace_info = buf.data[buf.count - 1];
        if (trace_info.type != type) {
            return;
        }
        trace_info.end_timestamp = ubsocket_get_timeNs_compile();
    }

    void AddReadTrace(ProfilingTPId type, int raw_socket, uint32_t seq_no, uint32_t data_size, uint32_t offset)
    {
        auto idx = read_active_idx_.load(std::memory_order_acquire);
        auto &buf = read_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = 0;
        trace_info.raw_socket = raw_socket;
        trace_info.seq_no = seq_no;
        trace_info.data_size = data_size;
        trace_info.offset = offset;
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
        buf.count++;
    }

    void AddWriteTrace(ProfilingTPId type, int raw_socket, uint64_t start_time, uint64_t end_time,
                       uint32_t poll_num = 0)
    {
        auto idx = write_active_idx_.load(std::memory_order_acquire);
        auto &buf = write_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.raw_socket = raw_socket;
        if (buf.count >= 1) {
            auto &last_trace = buf.data[buf.count - 1];
            trace_info.rpc_id = last_trace.rpc_id;
            trace_info.seq_no = last_trace.seq_no;
            trace_info.data_size = last_trace.data_size;
            trace_info.offset = last_trace.offset;
        }
        trace_info.type = type;
        trace_info.poll_num = poll_num;
        trace_info.start_timestamp = start_time;
        trace_info.end_timestamp = end_time;
        buf.count++;
    }

    void AddReadTrace(ProfilingTPId type, int raw_socket, uint64_t start_time, uint64_t end_time, uint32_t poll_num = 0)
    {
        auto idx = read_active_idx_.load(std::memory_order_acquire);
        auto &buf = read_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = 0;
        trace_info.raw_socket = raw_socket;
        if (buf.count >= 1) {
            auto &last_trace = buf.data[buf.count - 1];
            trace_info.seq_no = last_trace.seq_no;
            trace_info.data_size = last_trace.data_size;
            trace_info.offset = last_trace.offset;
        }
        uint32_t fresh = pending_rx_seq_no_.exchange(0, std::memory_order_acquire);
        if (fresh != 0) {
            trace_info.seq_no = fresh;
        }
        trace_info.type = type;
        trace_info.poll_num = poll_num;
        trace_info.start_timestamp = start_time;
        trace_info.end_timestamp = end_time;
        buf.count++;
    }

    void UpdateLastReadTrace(ProfilingTPId type)
    {
        auto idx = read_active_idx_.load(std::memory_order_acquire);
        auto &buf = read_bufs_[idx];
        if (buf.count == 0) {
            return;
        }
        auto &trace_info = buf.data[buf.count - 1];
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
    }

    void AddEpollTrace(ProfilingTPId type, int raw_socket, uint32_t seq_no, uint32_t data_size, uint32_t offset)
    {
        auto idx = epoll_active_idx_.load(std::memory_order_acquire);
        auto &buf = epoll_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = 0;
        trace_info.raw_socket = raw_socket;
        trace_info.seq_no = seq_no;
        trace_info.data_size = data_size;
        trace_info.offset = offset;
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
        buf.count++;
    }

    void AddEpollTrace(ProfilingTPId type, int raw_socket, uint32_t seq_no, uint32_t data_size, uint32_t offset,
                       uint64_t start_timestamp, uint64_t end_timestamp)
    {
        auto idx = epoll_active_idx_.load(std::memory_order_acquire);
        auto &buf = epoll_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = 0;
        trace_info.raw_socket = raw_socket;
        trace_info.seq_no = seq_no;
        trace_info.data_size = data_size;
        trace_info.offset = offset;
        trace_info.type = type;
        trace_info.start_timestamp = start_timestamp;
        trace_info.end_timestamp = end_timestamp;
        buf.count++;
    }

    void AddEpollTrace(ProfilingTPId type)
    {
        auto idx = epoll_active_idx_.load(std::memory_order_acquire);
        auto &buf = epoll_bufs_[idx];
        if (buf.count >= buf_capacity_) {
            buf.dropped_count++;
            return;
        }
        if (buf.count < 1) {
            return;
        }
        auto &last_trace = buf.data[buf.count - 1];
        buf.data[buf.count] = SplitTraceInfo{};
        auto &trace_info = buf.data[buf.count];
        trace_info.tid = static_cast<int>(syscall(SYS_gettid));
        trace_info.rpc_id = 0;
        trace_info.raw_socket = last_trace.raw_socket;
        trace_info.seq_no = last_trace.seq_no;
        trace_info.data_size = last_trace.data_size;
        trace_info.offset = last_trace.offset;
        trace_info.type = type;
        trace_info.start_timestamp = ubsocket_get_timeNs_compile();
        buf.count++;
    }

    void TrySwap()
    {
        TrySwapBuffer(write_active_idx_, write_bufs_);
        TrySwapBuffer(read_active_idx_, read_bufs_);
    }

    void TrySwapEpoll()
    {
        TrySwapBuffer(epoll_active_idx_, epoll_bufs_);
    }

    void Flush()
    {
        FlushBuffer(write_active_idx_, write_bufs_, "Write");
        FlushBuffer(read_active_idx_, read_bufs_, "Read");
        FlushBuffer(epoll_active_idx_, epoll_bufs_, "Epoll");
    }

    void DrainAndPrint()
    {
        DrainBuffer(write_active_idx_, write_bufs_, "Write");
        DrainBuffer(read_active_idx_, read_bufs_, "Read");
        DrainBuffer(epoll_active_idx_, epoll_bufs_, "Epoll");
    }
    uint32_t pack_size{0};
    std::queue<uint32_t> pack_size_list;

private:
    static void PrintSplitTraceInfo(const SplitTraceInfo &trace_info, const char *label)
    {
        uint64_t duration = trace_info.end_timestamp > 0 ? trace_info.end_timestamp - trace_info.start_timestamp : 0;
        UBS_VLOG_INFO("[%s] raw_socket: %d rpcid: %llu is_first: %d seq: %u data_size: %u offset: %u type: %u "
                      "poll_num: %u tid: %d start_timestamp: %lu end_timestamp: %lu%s\n",
                      label, trace_info.raw_socket, trace_info.rpc_id, trace_info.is_first, trace_info.seq_no,
                      trace_info.data_size, trace_info.offset, static_cast<uint32_t>(trace_info.type),
                      trace_info.poll_num, trace_info.tid, trace_info.start_timestamp, trace_info.end_timestamp,
                      trace_info.end_timestamp > 0 ? (" duration: " + std::to_string(duration) + " ns").c_str() : "");
    }

    void TrySwapBuffer(std::atomic<uint32_t> &active_idx, TraceBuffer bufs[2])
    {
        auto idx = active_idx.load(std::memory_order_acquire);
        if (bufs[idx].count < swap_threshold_) {
            return;
        }
        auto other = 1 - idx;
        if (bufs[other].frozen.load(std::memory_order_acquire)) {
            return;
        }
        if (bufs[other].count > 0) {
            return;
        }
        bufs[idx].frozen.store(true, std::memory_order_release);
        active_idx.store(other, std::memory_order_release);
    }

    void FlushBuffer(std::atomic<uint32_t> &active_idx, TraceBuffer bufs[2], const char *label)
    {
        auto idx = active_idx.load(std::memory_order_acquire);
        auto other = 1 - idx;
        PrintBuffer(bufs[other], label);
        bufs[other].Reset();
        bufs[other].frozen.store(false, std::memory_order_release);
        PrintBuffer(bufs[idx], label);
        bufs[idx].Reset();
    }

    void PrintBuffer(const TraceBuffer &buf, const char *label) const
    {
        if (buf.count == 0) {
            return;
        }
        if (buf.dropped_count > 0) {
            UBS_VLOG_WARN("=== %s Trace (dropped: %u) ===\n", label, buf.dropped_count);
        } else {
            UBS_VLOG_DEBUG("=== %s Trace ===\n", label);
        }
        for (uint32_t j = 0; j < buf.count; j++) {
            PrintSplitTraceInfo(buf.data[j], label);
        }
    }

    void DrainBuffer(std::atomic<uint32_t> &active_idx, TraceBuffer bufs[2], const char *label)
    {
        auto idx = active_idx.load(std::memory_order_acquire);
        auto drain_idx = 1 - idx;
        auto &buf = bufs[drain_idx];
        if (!buf.frozen.load(std::memory_order_acquire)) {
            return;
        }
        PrintBuffer(buf, label);
        buf.Reset();
        buf.frozen.store(false, std::memory_order_release);
    }
};

class TracePrintThread {
public:
    static TracePrintThread &Instance();
    void Start();
    void Stop();

private:
    void Run();
    void DrainAllSockets();

    std::thread thread_;
    std::atomic<bool> running_{false};
};

/* Trace macros controlled by compile-time flag - completely removed when disabled */
#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
#define TRACE_ADD_BRPC_WRITE(trace, type, raw_socket)         \
    do {                                                      \
        if ((trace) != nullptr) {                             \
            (trace)->AddBrpcWriteTrace((type), (raw_socket)); \
        }                                                     \
    } while (0)

#define TRACE_ADD_READ(trace, type, raw_socket, start_time, end_time)              \
    do {                                                                           \
        if ((trace) != nullptr) {                                                  \
            (trace)->AddReadTrace((type), (raw_socket), (start_time), (end_time)); \
        }                                                                          \
    } while (0)

#define TRACE_ADD_READ_DETAIL(trace, type, raw_socket, seq_no, data_size, offset)         \
    do {                                                                                  \
        if ((trace) != nullptr) {                                                         \
            (trace)->AddReadTrace((type), (raw_socket), (seq_no), (data_size), (offset)); \
        }                                                                                 \
    } while (0)

#define TRACE_UPDATE_LAST_READ(trace, type)       \
    do {                                          \
        if ((trace) != nullptr) {                 \
            (trace)->UpdateLastReadTrace((type)); \
        }                                         \
    } while (0)

#define TRACE_CLAIM_JFR_RX_SEQ_NO(trace)      \
    do {                                      \
        if ((trace) != nullptr) {             \
            (void)(trace)->ClaimJfrRxSeqNo(); \
        }                                     \
    } while (0)

#define TRACE_ADD_WRITE(trace, type, raw_socket, start_time, end_time, poll_num)                \
    do {                                                                                        \
        if ((trace) != nullptr) {                                                               \
            (trace)->AddWriteTrace((type), (raw_socket), (start_time), (end_time), (poll_num)); \
        }                                                                                       \
    } while (0)

#define TRACE_ADD_WRITE_SIMPLE(trace, type, raw_socket)   \
    do {                                                  \
        if ((trace) != nullptr) {                         \
            (trace)->AddWriteTrace((type), (raw_socket)); \
        }                                                 \
    } while (0)

#define TRACE_ADD_WRITE_DETAIL(trace, type, raw_socket, seq_no, data_size, offset, is_first)           \
    do {                                                                                               \
        if ((trace) != nullptr) {                                                                      \
            (trace)->AddWriteTrace((type), (raw_socket), (seq_no), (data_size), (offset), (is_first)); \
        }                                                                                              \
    } while (0)

#define TRACE_UPDATE_WRITE_FIRST(trace, type, seq_no, data_size, offset, is_first)               \
    do {                                                                                         \
        if ((trace) != nullptr) {                                                                \
            (trace)->UpdateWriteFirstTrace((type), (seq_no), (data_size), (offset), (is_first)); \
        }                                                                                        \
    } while (0)

#define TRACE_UPDATE_WRITE_LAST(trace, type, data_size, offset)           \
    do {                                                                  \
        if ((trace) != nullptr) {                                         \
            (trace)->UpdateWriteLastTrace((type), (data_size), (offset)); \
        }                                                                 \
    } while (0)

#define TRACE_UPDATE_WRITE_LAST_END(trace, type)          \
    do {                                                  \
        if ((trace) != nullptr) {                         \
            (trace)->UpdateWriteLastTraceEndTime((type)); \
        }                                                 \
    } while (0)

#define TRACE_ADD_EPOLL(trace, type)        \
    do {                                    \
        if ((trace) != nullptr) {           \
            (trace)->AddEpollTrace((type)); \
        }                                   \
    } while (0)

#define TRACE_ADD_EPOLL_DETAIL(trace, type, raw_socket, seq_no, data_size, offset)         \
    do {                                                                                   \
        if ((trace) != nullptr) {                                                          \
            (trace)->AddEpollTrace((type), (raw_socket), (seq_no), (data_size), (offset)); \
        }                                                                                  \
    } while (0)

#define TRACE_ADD_EPOLL_FULL(trace, type, raw_socket, seq_no, data_size, offset, start, end)               \
    do {                                                                                                   \
        if ((trace) != nullptr) {                                                                          \
            (trace)->AddEpollTrace((type), (raw_socket), (seq_no), (data_size), (offset), (start), (end)); \
        }                                                                                                  \
    } while (0)

#define TRACE_NOTIFY_JFR_RX_SEQ_NO(trace, seq_no) \
    do {                                          \
        if ((trace) != nullptr) {                 \
            (trace)->NotifyJfrRxSeqNo((seq_no));  \
        }                                         \
    } while (0)

#define TRACE_TRY_SWAP(trace)     \
    do {                          \
        if ((trace) != nullptr) { \
            (trace)->TrySwap();   \
        }                         \
    } while (0)

#define TRACE_TRY_SWAP_EPOLL(trace)  \
    do {                             \
        if ((trace) != nullptr) {    \
            (trace)->TrySwapEpoll(); \
        }                            \
    } while (0)

#define TRACE_FLUSH(trace)        \
    do {                          \
        if ((trace) != nullptr) { \
            (trace)->Flush();     \
        }                         \
    } while (0)

#define TRACE_DRAIN_AND_PRINT(trace)  \
    do {                              \
        if ((trace) != nullptr) {     \
            (trace)->DrainAndPrint(); \
        }                             \
    } while (0)
#else
#define TRACE_ADD_BRPC_WRITE(trace, type, raw_socket) \
    do {                                              \
    } while (0)
#define TRACE_ADD_READ(trace, type, raw_socket, start_time, end_time) \
    do {                                                              \
    } while (0)
#define TRACE_ADD_READ_DETAIL(trace, type, raw_socket, seq_no, data_size, offset) \
    do {                                                                                    \
    } while (0)
#define TRACE_UPDATE_LAST_READ(trace, type) \
    do {                                    \
    } while (0)
#define TRACE_CLAIM_JFR_RX_SEQ_NO(trace) \
    do {                                 \
    } while (0)
#define TRACE_ADD_WRITE(trace, type, raw_socket, start_time, end_time, poll_num) \
    do {                                                                         \
    } while (0)
#define TRACE_ADD_WRITE_SIMPLE(trace, type, raw_socket) \
    do {                                                \
    } while (0)
#define TRACE_ADD_WRITE_DETAIL(trace, type, raw_socket, seq_no, data_size, offset, is_first) \
    do {                                                                                     \
    } while (0)
#define TRACE_UPDATE_WRITE_FIRST(trace, type, seq_no, data_size, offset, is_first) \
    do {                                                                           \
    } while (0)
#define TRACE_UPDATE_WRITE_LAST(trace, type, data_size, offset) \
    do {                                                        \
    } while (0)
#define TRACE_UPDATE_WRITE_LAST_END(trace, type) \
    do {                                         \
    } while (0)
#define TRACE_ADD_EPOLL(trace, type) \
    do {                             \
    } while (0)
#define TRACE_ADD_EPOLL_DETAIL(trace, type, raw_socket, seq_no, data_size, offset) \
    do {                                                                           \
    } while (0)
#define TRACE_ADD_EPOLL_FULL(trace, type, raw_socket, seq_no, data_size, offset, start, end) \
    do {                                                                                     \
    } while (0)
#define TRACE_NOTIFY_JFR_RX_SEQ_NO(trace, seq_no) \
    do {                                          \
    } while (0)
#define TRACE_TRY_SWAP(trace) \
    do {                      \
    } while (0)
#define TRACE_TRY_SWAP_EPOLL(trace) \
    do {                            \
    } while (0)
#define TRACE_FLUSH(trace) \
    do {                   \
    } while (0)
#define TRACE_DRAIN_AND_PRINT(trace) \
    do {                             \
    } while (0)
#endif

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_TRACE_H
