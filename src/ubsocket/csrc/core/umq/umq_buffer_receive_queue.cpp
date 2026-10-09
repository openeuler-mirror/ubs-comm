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
#include "umq_buffer_receive_queue.h"
#include <cmath>

static ALWAYS_INLINE uint64_t GetCurrentTimeNs()
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

namespace ock {
namespace ubs {
namespace umq {

UmqBufferReceiveQueue::UmqBufferReceiveQueue()
{
    // rx_depth * 队列系数
    uint64_t queue_depth = static_cast<uint64_t>(std::ceil(GlobalSetting::UBS_RX_DEPTH * QUEUE_DEPTH_FACTOR));
    queue_depth = (queue_depth <= 1) ? 1 : 1ULL << (64 - __builtin_clzll(queue_depth - 1));
    receive_queue = new (std::nothrow) SPSCRingQueue<umq_buf_t *>(queue_depth);
    if (receive_queue == nullptr) {
        ClearAllocations();
        return;
    }

    use_o3_ = (UmqSetting::UMQ_UB_TRANS_MODE == RM_CTP);
    if (use_o3_) {
        uint32_t o3_queue_depth = GlobalSetting::UBS_RX_DEPTH;
        if (o3_queue_depth > queue_depth) {
            o3_queue_depth = queue_depth;
            UBS_VLOG_WARN("O3 queue depth exceeds shared jfr rx queue depth; use share jfr rx depth instead.\n");
        }
        out_of_order_queue = new (std::nothrow)
            FastHeap<umq_buf_t *, O3QueueComparator>(o3_queue_depth, o3_queue_depth);
        if (out_of_order_queue == nullptr) {
            ClearAllocations();
            return;
        }
    }
}

UmqBufferReceiveQueue::~UmqBufferReceiveQueue()
{
    Shutdown();
    ClearAllocations();
}

void UmqBufferReceiveQueue::Shutdown()
{
    is_shutdown_ = true;
}

bool UmqBufferReceiveQueue::IsInitialized() const
{
    if (receive_queue == nullptr) {
        return false;
    }
    if (use_o3_ && out_of_order_queue == nullptr) {
        return false;
    }
    return true;
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::Enqueue(umq_buf_t *buffer)
{
    if (buffer == nullptr) {
        UBS_VLOG_ERR("Failed to enqueue umq buffer, reason: output buffer is null.\n");
        return OpResult::ERROR;
    }
    if (is_shutdown_) {
        UBS_VLOG_WARN("Reject enqueue. Queue is already shutdown.\n");
        UmqApi::umq_buf_free(buffer);
        return OpResult::ERROR;
    }
    if (!use_o3_) {
        if (!receive_queue->Push(buffer)) {
            UBS_VLOG_ERR("Receive queue is full (No buffer space available).\n");
            UmqApi::umq_buf_free(buffer);
            pending_error_ = OpResult::QUEUE_FULL;
            return OpResult::QUEUE_FULL;
        }
        return OpResult::OK;
    }
    return EnqueueInOrder(buffer);
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::DequeueBatch(umq_buf_t **buffers, uint32_t max_count,
                                                                    uint32_t *dequeued_count)
{
    if (buffers == nullptr || max_count == 0 || dequeued_count == nullptr) {
        UBS_VLOG_ERR("Failed to dequeue batch umq buffer, reason: invalid parameters.\n");
        return OpResult::ERROR;
    }

    if (is_shutdown_) {
        UBS_VLOG_WARN("Reject dequeue batch. Queue is already shutdown.\n");
        return OpResult::ERROR;
    }

    if (pending_error_ != OpResult::OK) {
        return pending_error_;
    }

    *dequeued_count = 0;
    if (receive_queue->Empty()) {
        return OpResult::QUEUE_EMPTY;
    }
    *dequeued_count = receive_queue->MultiPop(buffers, max_count);
    return OpResult::OK;
}

void UmqBufferReceiveQueue::ClearAllocations()
{
    FlushReceiveQueueInternal();
    if (use_o3_) {
        FlushOooQueueInternal();
    }
    if (receive_queue) {
        delete receive_queue;
        receive_queue = nullptr;
    }
    if (out_of_order_queue) {
        delete out_of_order_queue;
        out_of_order_queue = nullptr;
    }
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::EnqueueInOrder(umq_buf_t *buffer)
{
    umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buffer->qbuf_ext;
    uint32_t raw_sn = buf_pro->imm.user_data;

    if (buffer->status >= UMQ_FAKE_BUF_FC_UPDATE || raw_sn == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
        if (!receive_queue->Push(buffer)) {
            UBS_VLOG_ERR("Receive queue is full (No buffer space available).\n");
            UmqApi::umq_buf_free(buffer);
            pending_error_ = OpResult::QUEUE_FULL;
            return OpResult::QUEUE_FULL;
        }
        return OpResult::OK;
    }

    uint32_t sn = UmqSeqTraits::Normalize(raw_sn);
    buf_pro->imm.user_data = sn;
    uint32_t current_expect = m_expect_sn;

    uint32_t gap = UmqSeqTraits::Distance(current_expect, sn);
    if (gap > UmqSeqTraits::MAX_WINDOW) {
        UBS_VLOG_WARN("Validate sn ahead failed, expect_sn: %u, sn: %u.\n", current_expect, sn);
        UmqApi::umq_buf_free(buffer);
        return OpResult::OK;
    }

    uint64_t now = GetCurrentTimeNs();
    if (CheckAndTriggerMeltdown(now, gap) != OpResult::OK) {
        UmqApi::umq_buf_free(buffer);
        return pending_error_ != OpResult::OK ? pending_error_ : OpResult::ERROR;
    }

    if (current_expect == sn) {
        return ProcessNormalInOrder(now, buffer);
    } else {
        if (out_of_order_queue->IsEmpty()) {
            m_ooo_start_time_ns = now;
        }
        if (out_of_order_queue->Push(buffer) != UBS_OK) {
            UBS_VLOG_ERR("Out-Of-Order receive queue is full (No buffer space available).\n");
            UmqApi::umq_buf_free(buffer);
            pending_error_ = OpResult::QUEUE_FULL;
            return OpResult::QUEUE_FULL;
        }
        return OpResult::OK;
    }
}

void UmqBufferReceiveQueue::FlushOooQueueInternal() const
{
    if (out_of_order_queue == nullptr) {
        return;
    }
    while (!out_of_order_queue->IsEmpty()) {
        umq_buf_t *buffer = out_of_order_queue->Top();
        out_of_order_queue->Pop();
        if (buffer) {
            UmqApi::umq_buf_free(buffer);
        }
    }
}

void UmqBufferReceiveQueue::FlushReceiveQueueInternal() const
{
    if (receive_queue == nullptr) {
        return;
    }
    if (!receive_queue->Empty()) {
        uint64_t count = receive_queue->Size();
        std::vector<umq_buf_t *> buf_vec(count);
        uint64_t pop_num = receive_queue->MultiPop(buf_vec.begin(), receive_queue->Size());
        for (uint64_t i = 0; i < pop_num; ++i) {
            UmqApi::umq_buf_free(buf_vec[i]);
        }
    }
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::ProcessNormalInOrder(uint64_t now, umq_buf_t *buffer)
{
    if (!receive_queue->Push(buffer)) {
        UmqApi::umq_buf_free(buffer);
        pending_error_ = OpResult::QUEUE_FULL;
        return OpResult::QUEUE_FULL;
    }
    uint32_t current_expect = UmqSeqTraits::Next(m_expect_sn);
    while (!out_of_order_queue->IsEmpty()) {
        umq_buf_t *top_buf = out_of_order_queue->Top();
        uint32_t top_buf_sn = GetSn(top_buf);

        if (current_expect == top_buf_sn) {
            out_of_order_queue->Pop();
            if (!receive_queue->Push(top_buf)) {
                UmqApi::umq_buf_free(top_buf);
                pending_error_ = OpResult::QUEUE_FULL;
                return OpResult::QUEUE_FULL;
            }
            current_expect = UmqSeqTraits::Next(current_expect);
        } else if (UmqSeqTraits::CompareLessInCircularOrder(current_expect, top_buf_sn)) {
            break;
        } else {
            out_of_order_queue->Pop();
            UmqApi::umq_buf_free(top_buf);
        }
    }

    m_expect_sn = current_expect;
    m_ooo_start_time_ns = out_of_order_queue->IsEmpty() ? 0 : now;
    return OpResult::OK;
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::CheckAndTriggerMeltdown(uint64_t now, uint32_t gap)
{
    if (pending_error_ != OpResult::OK) {
        return pending_error_;
    }
    if (gap > m_max_ooo_gap) {
        UBS_VLOG_ERR("Out-of-order gap exceeded threshold! Gap: %u, Max: %u. Forcing forward migration.\n", gap,
                     m_max_ooo_gap);
        return FlushOooQueueToReceiveQueueInternal();
    }
    if (!out_of_order_queue->IsEmpty() && (now - m_ooo_start_time_ns > m_ooo_timeout_ns)) {
        UBS_VLOG_ERR("Packet loss timeout! Expect SN: %u stalled for %lu ns. Forcing forward migration.\n", m_expect_sn,
                     (now - m_ooo_start_time_ns));
        return FlushOooQueueToReceiveQueueInternal();
    }
    return OpResult::OK;
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::FlushOooQueueToReceiveQueueInternal()
{
    if (out_of_order_queue == nullptr || out_of_order_queue->IsEmpty()) {
        return OpResult::OK;
    }

    UBS_VLOG_WARN("Intervention triggered: Flushing OOO queue to receive queue. Old expect_sn: %u\n", m_expect_sn);
    while (!out_of_order_queue->IsEmpty()) {
        umq_buf_t *top_buf = out_of_order_queue->Top();
        if (top_buf == nullptr) {
            out_of_order_queue->Pop();
            continue;
        }
        if (receive_queue->Push(top_buf)) {
            m_expect_sn = UmqSeqTraits::Add(GetSn(top_buf), 1);
            out_of_order_queue->Pop();
        } else {
            UBS_VLOG_ERR("Receive queue overflow during meltdown flush! Dropping remaining OOO packets.\n");
            while (!out_of_order_queue->IsEmpty()) {
                umq_buf_t *remain_buf = out_of_order_queue->Top();
                out_of_order_queue->Pop();
                if (remain_buf) {
                    UmqApi::umq_buf_free(remain_buf);
                }
            }
            m_ooo_start_time_ns = 0;
            pending_error_ = OpResult::QUEUE_FULL;
            return OpResult::QUEUE_FULL;
        }
    }
    m_ooo_start_time_ns = 0;
    return OpResult::OK;
}

bool UmqBufferReceiveQueue::Empty() const
{
    return receive_queue->Empty();
}

} // namespace umq
} // namespace ubs
} // namespace ock