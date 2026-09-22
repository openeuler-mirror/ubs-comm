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
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "common/ubsocket_global_setting.h"
#include "core/ubsocket_core_types.h"
#include "profiling/ubsocket_prof.h"
#include "ubsocket_trace.h"

namespace ock {
namespace ubs {

u_external_rpc_id_ops_t TraceRegistry::RPC_ID_OPS;

Result TraceRegistry::RegisterRpcIdOps(u_external_rpc_id_ops_t *ops)
{
    if (!ops || !ops->get_rpc_id || !ops->get_rpc_call_timestamp) {
        return UBS_INVALID_PARAM;
    }
    RPC_ID_OPS = *ops;
    return 0;
}

void TraceSlot::Reset() noexcept
{
    state.store(0, std::memory_order_relaxed);
    path = 0;
    seq_no = 0;
    fd = -1;
    io_start_ts = 0;
    io_end_ts = 0;
    data_size = 0;
    offset = 0;
    phases.tx_writev.ResetBitmap();
}

void TraceSlot::RecordPhase(uint8_t p, uint16_t idx, uint64_t s, uint64_t e) noexcept
{
    switch (p) {
        case PATH_TX_WRITEV: phases.tx_writev.RecordPhase(idx, s, e); break;
        case PATH_TX_POST:   phases.tx_post.RecordPhase(idx, s, e);   break;
        case PATH_RX_READV:  phases.rx_readv.RecordPhase(idx, s, e);  break;
        case PATH_RX_POLL:   phases.rx_poll.RecordPhase(idx, s, e);   break;
        default: break;
    }
}

GlobalTracePool &GlobalTracePool::Instance()
{
    static GlobalTracePool instance;
    return instance;
}

bool GlobalTracePool::LazyInit() noexcept
{
    if (slots_ != nullptr) {
        return true;
    }
    slots_ = new (std::nothrow) TraceSlot[MAX_SLOTS];
    if (slots_ == nullptr) {
        return false;
    }
    lookup_table_ = new (std::nothrow) std::atomic<uint64_t>[LOOKUP_SIZE];
    if (lookup_table_ == nullptr) {
        delete[] slots_;
        slots_ = nullptr;
        return false;
    }
    for (uint16_t i = 0; i < MAX_SLOTS; i++) {
        slots_[i].Reset();
    }
    for (uint32_t i = 0; i < LOOKUP_SIZE; i++) {
        lookup_table_[i].store(0, std::memory_order_relaxed);
    }
    next_hint_.store(0, std::memory_order_relaxed);
    done_count_.store(0, std::memory_order_relaxed);
    return true;
}

void GlobalTracePool::DestroyPool() noexcept
{
    delete[] slots_;
    slots_ = nullptr;
    delete[] lookup_table_;
    lookup_table_ = nullptr;
}

int16_t GlobalTracePool::AllocSlot(uint32_t seqNo, int fd, uint8_t path) noexcept
{
    if (slots_ == nullptr || lookup_table_ == nullptr) {
        return -1;
    }
    uint16_t start = next_hint_.load(std::memory_order_relaxed);

    for (uint16_t k = 0; k < MAX_SLOTS; k++) {
        uint16_t i = (start + k) % MAX_SLOTS;

        uint8_t expected = TraceSlot::STATE_IDLE;
        if (slots_[i].state.compare_exchange_strong(
                expected, TraceSlot::STATE_TRACING, std::memory_order_acq_rel)) {
            next_hint_.store(static_cast<uint16_t>(i + 1), std::memory_order_relaxed);

            auto &slot = slots_[i];
            slot.path = path;
            slot.seq_no = seqNo;
            slot.fd = fd;
            slot.io_start_ts = ubsocket_get_timeNs_compile();
            slot.io_end_ts = 0;
            slot.data_size = 0;
            slot.offset = 0;
            switch (path) {
                case PATH_TX_WRITEV: slot.phases.tx_writev.ResetBitmap(); break;
                case PATH_TX_POST:   slot.phases.tx_post.ResetBitmap();   break;
                case PATH_RX_READV:  slot.phases.rx_readv.ResetBitmap();  break;
                case PATH_RX_POLL:   slot.phases.rx_poll.ResetBitmap();   break;
                default: break;
            }

            lookup_table_[MixLookupKey(seqNo, fd) & LOOKUP_MASK].store(
                PackLookup(seqNo, fd, static_cast<uint16_t>(i)), std::memory_order_release);

            return static_cast<int16_t>(i);
        }
    }
    return -1;
}

void GlobalTracePool::EndSlot(int16_t slotIdx, uint32_t dataSize, uint32_t offset) noexcept
{
    if (slots_ == nullptr || slotIdx < 0 || static_cast<uint16_t>(slotIdx) >= MAX_SLOTS) {
        return;
    }
    auto &slot = slots_[slotIdx];
    slot.io_end_ts = ubsocket_get_timeNs_compile();
    slot.data_size = dataSize;
    slot.offset = offset;
    slot.state.store(TraceSlot::STATE_DONE, std::memory_order_release);
    done_count_.fetch_add(1, std::memory_order_relaxed);
}

TraceSlot *GlobalTracePool::FindSlot(uint32_t seqNo, uint8_t path, int fd) noexcept
{
    if (slots_ == nullptr || lookup_table_ == nullptr) {
        return nullptr;
    }
    /* Fast path: O(1) direct-mapped lookup */
    uint64_t packed = lookup_table_[MixLookupKey(seqNo, fd) & LOOKUP_MASK].load(std::memory_order_acquire);
    uint16_t slotIdx;
    if (UnpackLookup(packed, seqNo, fd, slotIdx)) {
        uint8_t s = slots_[slotIdx].state.load(std::memory_order_acquire);
        if (s == TraceSlot::STATE_TRACING || s == TraceSlot::STATE_DONE) {
            return &slots_[slotIdx];
        }
    }

    /* Slow path: collision or miss — linear scan */
    bool isTx = IsTxPath(path);
    for (uint16_t i = 0; i < MAX_SLOTS; i++) {
        uint8_t s = slots_[i].state.load(std::memory_order_acquire);
        if ((s == TraceSlot::STATE_TRACING || s == TraceSlot::STATE_DONE) &&
            slots_[i].seq_no == seqNo && slots_[i].fd == fd &&
            IsTxPath(slots_[i].path) == isTx) {
            return &slots_[i];
        }
    }
    return nullptr;
}

void GlobalTracePool::ClearLookup(uint32_t seqNo, int fd) noexcept
{
    if (lookup_table_ == nullptr) {
        return;
    }
    uint32_t idx = MixLookupKey(seqNo, fd) & LOOKUP_MASK;
    uint64_t packed = lookup_table_[idx].load(std::memory_order_relaxed);
    if (packed != 0 && static_cast<uint32_t>(packed) == seqNo &&
        static_cast<int32_t>((packed >> 32) & 0xFFFFFF) == fd) {
        lookup_table_[idx].store(0, std::memory_order_relaxed);
    }
}

void GlobalTracePool::DrainAll(uint64_t now) noexcept
{
    if (slots_ == nullptr || lookup_table_ == nullptr) {
        return;
    }
    static constexpr uint64_t GRACE_PERIOD_NS = 2000000;
    static thread_local char batch_buf[256 * 1024];
    int batch_pos = 0;
    int batch_count = 0;

    for (uint16_t i = 0; i < MAX_SLOTS; i++) {
        if (slots_[i].state.load(std::memory_order_acquire) != TraceSlot::STATE_DONE) {
            continue;
        }
        if (now < slots_[i].io_end_ts || now - slots_[i].io_end_ts < GRACE_PERIOD_NS) {
            continue;
        }
        if (batch_pos < static_cast<int>(sizeof(batch_buf)) - 2048) {
            int n = SplitTraceDrainThread::FormatSlot(batch_buf + batch_pos,
                                                       sizeof(batch_buf) - batch_pos,
                                                       slots_[i]);
            batch_pos += n;
            batch_count++;
        }
        ClearLookup(slots_[i].seq_no, slots_[i].fd);
        slots_[i].Reset();
        done_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    if (batch_count > 0) {
        UBS_SLOG_INFO(std::string(batch_buf, static_cast<size_t>(batch_pos)));
    }
}

bool SplitTraceTrySample(Socket *sock, int fd, uint32_t seqNo, uint8_t path)
{
    if (sock == nullptr || !GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        return false;
    }

    if (seqNo == 0) {
        return false;
    }
    uint32_t rate = GlobalSetting::UBS_SPLIT_TRACE_SAMPLE_RATE;
    if (rate == 0) {
        rate = 100;
    }
    if (seqNo % rate != 0) {
        return false;
    }

    int16_t slot_idx = GlobalTracePool::Instance().AllocSlot(seqNo, fd, path);
    return slot_idx >= 0;
}

void SplitTraceAdd(Socket *sock, uint8_t path, uint16_t idx,
                   uint32_t seqNo, uint64_t startTs, uint64_t endTs)
{
    if (sock == nullptr || !GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        return;
    }
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, sock->raw_socket_);
    if (slot != nullptr) {
        slot->RecordPhase(path, idx, startTs, endTs);
    }
}

void SplitTraceEndSample(Socket *sock, uint8_t path, uint32_t seqNo,
                         uint32_t dataSize, uint32_t offset)
{
    if (sock == nullptr || !GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        return;
    }
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, sock->raw_socket_);
    if (slot != nullptr) {
        slot->io_end_ts = ubsocket_get_timeNs_compile();
        slot->data_size = dataSize;
        slot->offset = offset;
        slot->state.store(TraceSlot::STATE_DONE, std::memory_order_release);
        GlobalTracePool::Instance().done_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

void SplitTraceAddSampled(int fd, uint8_t path, uint16_t idx,
                          uint32_t seqNo, uint64_t startTs, uint64_t endTs)
{
    if (!GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        return;
    }
    TraceSlot *slot = GlobalTracePool::Instance().FindSlot(seqNo, path, fd);
    if (slot != nullptr) {
        slot->RecordPhase(path, idx, startTs, endTs);
    }
}

static const char *TX_WV_PHASE_NAMES[TX_WV_PHASE_COUNT] = {
    "TX_WV_ENTRY", "TX_WV_BUILD_IOV", "TX_WV_ALLOC_BUF", "TX_WV_MEM_COPY",
    "TX_WV_UMQ_POST", "TX_WV_EXIT", "TX_WV_ASYNC_UMQ_POLL", "TX_WV_ASYNC_PROCESS_CQE",
    "TX_WV_ASYNC_BUF_FREE", "TX_WV_ASYNC_NOTIFY"
};

static const char *TX_POST_PHASE_NAMES[TX_POST_PHASE_COUNT] = {
    "TX_POST_ENTRY", "TX_POST_SENDER_POST", "TX_POST_HANDLE_SMALL", "TX_POST_HANDLE_LARGE",
    "TX_POST_UMQ_POST", "TX_POST_EXIT", "TX_POST_ASYNC_UMQ_POLL", "TX_POST_ASYNC_PROCESS_CQE",
    "TX_POST_ASYNC_BUF_FREE", "TX_POST_ASYNC_NOTIFY"
};

static const char *RX_RV_PHASE_NAMES[RX_RV_PHASE_COUNT] = {
    "RX_RV_ENTRY", "RX_RV_POLL_RX", "RX_RV_HANDLE_BUF", "RX_RV_DATA_SET",
    "RX_RV_REARM", "RX_RV_EXIT", "RX_RV_ASYNC_UMQ_POLL", "RX_RV_ASYNC_SIFT",
    "RX_RV_ASYNC_ENQUEUE", "RX_RV_ASYNC_NOTIFY", "RX_RV_ASYNC_ALLOC_BUF",
    "RX_RV_ASYNC_POST_RX", "RX_RV_ASYNC_REARM"
};

static const char *RX_POLL_PHASE_NAMES[RX_POLL_PHASE_COUNT] = {
    "RX_POLL_ENTRY", "RX_POLL_GET_AND_POP", "RX_POLL_BIG_CTRL", "RX_POLL_DELIVER_SEG",
    "RX_POLL_EXIT", "RX_POLL_ASYNC_UMQ_POLL", "RX_POLL_ASYNC_SIFT", "RX_POLL_ASYNC_ENQUEUE",
    "RX_POLL_ASYNC_NOTIFY", "RX_POLL_ASYNC_ALLOC_BUF", "RX_POLL_ASYNC_POST_RX",
    "RX_POLL_ASYNC_REARM"
};

const char *SplitTraceDrainThread::PhaseName(uint8_t path, uint16_t idx) noexcept
{
    switch (path) {
        case PATH_TX_WRITEV: return (idx < TX_WV_PHASE_COUNT) ? TX_WV_PHASE_NAMES[idx] : "UNKNOWN";
        case PATH_TX_POST:   return (idx < TX_POST_PHASE_COUNT) ? TX_POST_PHASE_NAMES[idx] : "UNKNOWN";
        case PATH_RX_READV:  return (idx < RX_RV_PHASE_COUNT) ? RX_RV_PHASE_NAMES[idx] : "UNKNOWN";
        case PATH_RX_POLL:   return (idx < RX_POLL_PHASE_COUNT) ? RX_POLL_PHASE_NAMES[idx] : "UNKNOWN";
        default: return "UNKNOWN";
    }
}

static const char *PATH_NAMES[4] = {"TX_WRITEV", "TX_POST", "RX_READV", "RX_POLL"};

SplitTraceDrainThread &SplitTraceDrainThread::Instance()
{
    static SplitTraceDrainThread instance;
    return instance;
}

SplitTraceDrainThread::~SplitTraceDrainThread()
{
    Stop();
}

void SplitTraceDrainThread::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    drain_count_ = 0;
    thread_ = std::thread(&SplitTraceDrainThread::Run, this);
}

void SplitTraceDrainThread::Stop()
{
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SplitTraceDrainThread::Run()
{
    pthread_setname_np(pthread_self(), "ubs_strace");
    while (running_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS));
        DrainAll();
    }
    DrainAll();
}

void SplitTraceDrainThread::EnsureSplitTraceDir() noexcept
{
    mkdir("/tmp/ubsocket", 0755);
    mkdir("/tmp/ubsocket/split_trace", 0755);
}

int SplitTraceDrainThread::FormatSlot(char *buf, int bufsize, const TraceSlot &slot) noexcept
{
    const char *path_name = (slot.path < 4) ? PATH_NAMES[slot.path] : "UNKNOWN";
    uint64_t dur = slot.io_end_ts > slot.io_start_ts ? slot.io_end_ts - slot.io_start_ts : 0;
    int pos = snprintf(buf, bufsize, "===%s fd=%d seq=%u s=%lu e=%lu d=%lu\n",
                       path_name, slot.fd, slot.seq_no,
                       static_cast<unsigned long>(slot.io_start_ts),
                       static_cast<unsigned long>(slot.io_end_ts),
                       static_cast<unsigned long>(dur));
    uint16_t max_phases = 0;
    switch (slot.path) {
        case PATH_TX_WRITEV: max_phases = TX_WV_PHASE_COUNT; break;
        case PATH_TX_POST:   max_phases = TX_POST_PHASE_COUNT; break;
        case PATH_RX_READV:  max_phases = RX_RV_PHASE_COUNT; break;
        case PATH_RX_POLL:   max_phases = RX_POLL_PHASE_COUNT; break;
        default: break;
    }
    for (uint16_t i = 0; i < max_phases && pos < bufsize - 128; i++) {
        bool has = false;
        switch (slot.path) {
            case PATH_TX_WRITEV: has = slot.phases.tx_writev.HasPhase(i); break;
            case PATH_TX_POST:   has = slot.phases.tx_post.HasPhase(i); break;
            case PATH_RX_READV:  has = slot.phases.rx_readv.HasPhase(i); break;
            case PATH_RX_POLL:   has = slot.phases.rx_poll.HasPhase(i); break;
            default: break;
        }
        if (!has) {
            continue;
        }
        uint64_t s = 0, e = 0;
        switch (slot.path) {
            case PATH_TX_WRITEV: s = slot.phases.tx_writev.phase_start[i]; e = slot.phases.tx_writev.phase_end[i]; break;
            case PATH_TX_POST:   s = slot.phases.tx_post.phase_start[i];   e = slot.phases.tx_post.phase_end[i]; break;
            case PATH_RX_READV:  s = slot.phases.rx_readv.phase_start[i];  e = slot.phases.rx_readv.phase_end[i]; break;
            case PATH_RX_POLL:   s = slot.phases.rx_poll.phase_start[i];   e = slot.phases.rx_poll.phase_end[i]; break;
            default: break;
        }
        uint64_t d = e > s ? e - s : 0;
        pos += snprintf(buf + pos, bufsize - pos, "%s,s=%lu,e=%lu,d=%lu\n",
                        PhaseName(slot.path, i),
                        static_cast<unsigned long>(s),
                        static_cast<unsigned long>(e),
                        static_cast<unsigned long>(d));
    }
    return pos;
}

void SplitTraceDrainThread::DrainAll()
{
    uint64_t now = ubsocket_get_timeNs_compile();
    drain_count_++;
    GlobalTracePool::Instance().DrainAll(now);
}

} // namespace ubs
} // namespace ock
