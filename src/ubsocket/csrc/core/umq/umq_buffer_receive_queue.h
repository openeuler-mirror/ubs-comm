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
#ifndef UBS_COMM_UMQ_BUFFER_RECEIVE_QUEUE_H
#define UBS_COMM_UMQ_BUFFER_RECEIVE_QUEUE_H

#include "core/umq/umq_bounded_seq.h"
#include "core/umq/umq_intrusive_buf_queue.h"
#include "core/umq/umq_setting.h"
#include "csrc/common/ubsocket_fast_heap.h"

namespace ock {
namespace ubs {
namespace umq {

using UmqSeqTraits =
    UmqBoundedSeqTraits<UmqSetting::UMQ_SOCKET_SEQ_NUM_BIT_WIDTH, uint32_t, UmqSetting::UMQ_SOCKET_SEQ_NUM_MAX>;

class UmqBufferReceiveQueue {
public:
    enum class OpResult : int {
        OK = 0,
        ERROR = -EPERM,
        QUEUE_FULL = -ENOBUFS,
        MELTDOWN_TRIGGERED = -EPIPE ,
    };

    explicit UmqBufferReceiveQueue();
    ~UmqBufferReceiveQueue();

    // 禁止拷贝和赋值
    UmqBufferReceiveQueue(const UmqBufferReceiveQueue &) = delete;
    UmqBufferReceiveQueue &operator=(const UmqBufferReceiveQueue &) = delete;

    bool IsInitialized() const;
    bool Empty() const;
    OpResult Enqueue(umq_buf_t *buffer);
    OpResult DequeueBatch(umq_buf_t **buffers, uint32_t max_count, uint32_t *dequeued_count);
    void Shutdown();

private:
    static ALWAYS_INLINE uint32_t GetSn(umq_buf_t *buffer)
    {
        umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buffer->qbuf_ext;
        return buf_pro->imm.user_data;
    }

    struct O3QueueComparator {
        inline bool operator()(umq_buf_t *a, umq_buf_t *b) noexcept
        {
            return UmqSeqTraits::CompareLessInCircularOrder(GetSn(a), GetSn(b));
        }
    };

    void ClearAllocations();
    OpResult EnqueueInOrder(umq_buf_t *buffer);
    bool EnsureOooQueue();
    void FlushOooQueueInternal() const;
    void FlushReceiveQueueInternal() const;
    OpResult ProcessNormalInOrder(uint64_t now, umq_buf_t *buffer);
    OpResult CheckAndTriggerMeltdown(uint64_t now, uint32_t gap);

private:
    /*
     * 接收队列采用侵入式 SPSC 队列：每链路仅 O(1) 固定开销，排队元素存储在 qbuf 自身，
     * 总内存受缓冲区数量约束，与链路数量无关（原 SPSCRingQueue 每链路预留
     * next_pow2(1.2 * UBS_RX_DEPTH) * 8B ≈ 32KB，4 万链路即 1.2GB+）。
     */
    UmqIntrusiveBufQueue receive_queue_;
    /*
     * 乱序堆懒创建：常态在序链路（及 RM_TP 模式）从不分配，首个乱序包到达时才 new。
     * nullptr 语义上等价于空堆。
     */
    FastHeap<umq_buf_t *, O3QueueComparator> *out_of_order_queue = nullptr;

    // 期望接收的序列号
    uint32_t m_expect_sn{0};
    O3QueueComparator comp;

    bool use_o3_{false};
    bool is_shutdown_{false};
    // 乱序堆容量上限（构造时确定，懒创建时使用）
    uint32_t o3_max_depth_{0};
    volatile OpResult pending_error_ = OpResult::OK;

    // 应用层配置：最大允许乱序度距离（不超过rx_depth）
    uint32_t m_max_ooo_gap = GlobalSetting::UBS_RX_DEPTH;
    // 应用层配置：断链最大等待超时（纳秒）
    uint64_t m_ooo_timeout_ns = (UmqSetting::UMQ_O3_TIMEOUT_MS != 0 ? UmqSetting::UMQ_O3_TIMEOUT_MS : 5) * 1000000ULL;
    // 首次发生断链（主槽位出现空洞）的时间戳
    uint64_t m_ooo_start_time_ns = 0;

    static constexpr double QUEUE_DEPTH_FACTOR = 1.2;
    /* 乱序堆初始容量：按需增长至 rx_depth 上限，清空后回缩，内存随实际乱序缓冲区数量走。
     * 取 FastHeap 最小容量（4）：常态无乱序，堆常驻内存从 (16+1)*8B 降至 (4+1)*8B */
    static constexpr uint32_t O3_QUEUE_INIT_DEPTH = 4;

    static uint64_t ComputeQueueDepth();
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_BUFFER_RECEIVE_QUEUE_H
