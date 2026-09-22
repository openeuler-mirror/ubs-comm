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
#include <algorithm>
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

uint64_t UmqBufferReceiveQueue::ComputeQueueDepth()
{
    // rx_depth * 队列系数
    uint64_t queue_depth = static_cast<uint64_t>(std::ceil(GlobalSetting::UBS_RX_DEPTH * QUEUE_DEPTH_FACTOR));
    return (queue_depth <= 1) ? 1 : 1ULL << (64 - __builtin_clzll(queue_depth - 1));
}

UmqBufferReceiveQueue::UmqBufferReceiveQueue() : receive_queue_(ComputeQueueDepth())
{
    /*
     * 接收队列为侵入式 SPSC 队列，构造不做任何容量预留（原实现在此处 new 一个
     * next_pow2(1.2 * rx_depth) 深度的指针环，每链路固定消耗约 32KB）。
     * queue_depth 仅作为 QUEUE_FULL 背压上限保留，与内存占用解耦。
     */
    use_o3_ = (UmqSetting::UMQ_UB_TRANS_MODE == RM_CTP);
    if (use_o3_) {
        uint64_t queue_depth = receive_queue_.Capacity();
        uint32_t o3_queue_depth = GlobalSetting::UBS_RX_DEPTH;
        if (o3_queue_depth > queue_depth) {
            o3_queue_depth = static_cast<uint32_t>(queue_depth);
            UBS_VLOG_WARN("O3 queue depth exceeds shared jfr rx queue depth; use share jfr rx depth instead.\n");
        }
        /* 乱序堆懒创建：此处仅记录容量上限，首个乱序包到达时才分配（见 EnsureOooQueue）。
         * 常态在序链路每链路省下 (O3_QUEUE_INIT_DEPTH+1)*8B 堆数组 + FastHeap 对象本身。 */
        o3_max_depth_ = o3_queue_depth;
    }
}

/* 仅在收包线程（共享 JFR poller）调用，无并发创建问题 */
bool UmqBufferReceiveQueue::EnsureOooQueue()
{
    if (out_of_order_queue != nullptr) {
        return true;
    }
    uint32_t o3_init_depth = std::min(static_cast<uint32_t>(O3_QUEUE_INIT_DEPTH), o3_max_depth_);
    out_of_order_queue = new (std::nothrow)
        FastHeap<umq_buf_t *, O3QueueComparator>(o3_init_depth, o3_max_depth_);
    return out_of_order_queue != nullptr;
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
    /* 乱序堆已改为懒创建（nullptr = 空堆），构造不再有可失败的分配 */
    return true;
}

static void PushChainToRing(UmqIntrusiveBufQueue *ring, umq_buf_t *head,
                            UmqBufferReceiveQueue::OpResult *error)
{
    umq_buf_t *cur = head;
    while (cur != nullptr) {
        umq_buf_t *next = cur->qbuf_next;
        cur->qbuf_next = nullptr;
        if (!ring->Push(cur)) {
            UmqApi::umq_buf_free(cur);
            while (next != nullptr) {
                umq_buf_t *tmp = next->qbuf_next;
                next->qbuf_next = nullptr;
                UmqApi::umq_buf_free(next);
                next = tmp;
            }
            *error = UmqBufferReceiveQueue::OpResult::QUEUE_FULL;
            return;
        }
        cur = next;
    }
    *error = UmqBufferReceiveQueue::OpResult::OK;
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
        OpResult push_err = OpResult::OK;
        PushChainToRing(&receive_queue_, buffer, &push_err);
        if (push_err != OpResult::OK) {
            UBS_VLOG_ERR("Receive queue is full (No buffer space available).\n");
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

    *dequeued_count = static_cast<uint32_t>(receive_queue_.PopBatch(buffers, max_count));
    return OpResult::OK;
}

void UmqBufferReceiveQueue::ClearAllocations()
{
    FlushReceiveQueueInternal();
    if (use_o3_) {
        FlushOooQueueInternal();
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
        if (!receive_queue_.Push(buffer)) {
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
    }
    /* 首个乱序包：懒创建乱序堆；创建失败按堆满同等处理 */
    if (!EnsureOooQueue()) {
        UBS_VLOG_ERR("Failed to create Out-Of-Order receive queue (No memory available).\n");
        UmqApi::umq_buf_free(buffer);
        pending_error_ = OpResult::QUEUE_FULL;
        return OpResult::QUEUE_FULL;
    }
    if (out_of_order_queue->IsEmpty()) {
        m_ooo_start_time_ns = now;
    }
    if (out_of_order_queue->Push(buffer) != UBS_OK) {
        UBS_VLOG_ERR("Out-Of-Order receive queue is full (No buffer space available).\n");
        UmqApi::umq_buf_free(buffer);
        if (out_of_order_queue->IsEmpty()) {
            m_ooo_start_time_ns = 0;
        }
        pending_error_ = OpResult::QUEUE_FULL;
        return OpResult::QUEUE_FULL;
    }
    return OpResult::OK;
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
    /* 析构/关闭路径，无并发访问；分批弹出并释放，避免一次性按队列长度分配临时数组 */
    constexpr uint64_t FLUSH_BATCH = 64;
    umq_buf_t *buf_vec[FLUSH_BATCH];
    uint64_t pop_num;
    auto &queue = const_cast<UmqIntrusiveBufQueue &>(receive_queue_);
    while ((pop_num = queue.PopBatch(buf_vec, FLUSH_BATCH)) != 0) {
        for (uint64_t i = 0; i < pop_num; ++i) {
            UmqApi::umq_buf_free(buf_vec[i]);
        }
    }
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::ProcessNormalInOrder(uint64_t now, umq_buf_t *buffer)
{
    /* buffer may be a chain head (shared-SN bigdata READ) — push each
     * fragment individually so ring stores only single qbufs. */
    {
        OpResult push_err = OpResult::OK;
        PushChainToRing(&receive_queue_, buffer, &push_err);
        if (push_err != OpResult::OK) {
            pending_error_ = OpResult::QUEUE_FULL;
            return OpResult::QUEUE_FULL;
        }
    }
    uint32_t current_expect = UmqSeqTraits::Next(m_expect_sn);
    /* 懒创建：堆尚未分配 == 空堆，直接走无乱序快路径 */
    if (out_of_order_queue == nullptr) {
        m_expect_sn = current_expect;
        return OpResult::OK;
    }
    while (!out_of_order_queue->IsEmpty()) {
        umq_buf_t *top_buf = out_of_order_queue->Top();
        uint32_t top_buf_sn = GetSn(top_buf);

        if (current_expect == top_buf_sn) {
            out_of_order_queue->Pop();
            /* top_buf may also be a chain head — break it for the ring. */
            OpResult push_err = OpResult::OK;
            PushChainToRing(&receive_queue_, top_buf, &push_err);
            if (push_err != OpResult::OK) {
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
    if (out_of_order_queue->IsEmpty()) {
        m_ooo_start_time_ns = 0;
        /* 乱序段结束，堆容量回缩到初始值，内存随实际乱序缓冲区数量走 */
        out_of_order_queue->TryShrink();
    } else {
        m_ooo_start_time_ns = now;
    }
    return OpResult::OK;
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::CheckAndTriggerMeltdown(uint64_t now, uint32_t gap)
{
    if (pending_error_ != OpResult::OK) {
        return pending_error_;
    }

    const bool is_gap_exceeded = gap > m_max_ooo_gap;
    const bool is_ooo_timed_out = out_of_order_queue != nullptr && !out_of_order_queue->IsEmpty() &&
                            m_ooo_start_time_ns != 0 && now - m_ooo_start_time_ns > m_ooo_timeout_ns;

    if (!is_gap_exceeded && !is_ooo_timed_out) {
        return OpResult::OK;
    }

    UBS_VLOG_WARN("Intervention triggered: Flushing OOO queue to receive queue. Old expect_sn: %u\n", m_expect_sn);
    if (out_of_order_queue == nullptr) {
        /* 懒创建：堆从未分配（首包即超 gap 上限触发熔断），无乱序包可回灌 */
        pending_error_ = OpResult::MELTDOWN_TRIGGERED;
        m_ooo_start_time_ns = 0;
        return pending_error_;
    }
    while (!out_of_order_queue->IsEmpty()) {
        umq_buf_t *top_buf = out_of_order_queue->Top();
        if (top_buf == nullptr) {
            out_of_order_queue->Pop();
            continue;
        }
        out_of_order_queue->Pop();
        uint32_t sn = GetSn(top_buf);
        OpResult push_err = OpResult::OK;
        /* top_buf may be a chain head — break and push each individually. */
        PushChainToRing(&receive_queue_, top_buf, &push_err);
        if (push_err == OpResult::OK) {
            m_expect_sn = UmqSeqTraits::Add(sn, 1);
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
    pending_error_ = OpResult::MELTDOWN_TRIGGERED;
    // 释放缓存
    FlushOooQueueInternal();
    out_of_order_queue->TryShrink();
    m_ooo_start_time_ns = 0;
    return pending_error_;
}

bool UmqBufferReceiveQueue::Empty() const
{
    return receive_queue_.Empty();
}

} // namespace umq
} // namespace ubs
} // namespace ock