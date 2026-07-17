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

#include "common/ubsocket_spsc_ring_queue.h"
#include "core/umq/umq_bounded_seq.h"
#include "core/umq/umq_setting.h"
#include "csrc/common/ubsocket_fast_heap.h"

namespace ock {
namespace ubs {
namespace umq {

using UmqSeqTraits =
    UmqBoundedSeqTraits<UmqSetting::UMQ_SOCKET_SEQ_NUM_BIT_WIDTH, uint32_t, UmqSetting::UMQ_SOCKET_SEQ_NUM_MAX>;

class UmqBufferReceiveQueue {
public:
    enum class OpResult : int
    {
        OK = 0,
        ERROR = -1,
        QUEUE_EMPTY = 1
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
    void FlushOooQueueInternal() const;
    void FlushReceiveQueueInternal() const;
    void ProcessNormalInOrder(uint64_t now, umq_buf_t *buffer);
    void CheckAndTriggerMeltdown(uint32_t sn, uint64_t now, uint32_t gap);
    void FlushOooQueueToReceiveQueueInternal();

private:
    SPSCRingQueue<umq_buf_t *> *receive_queue = nullptr;
    FastHeap<umq_buf_t *, O3QueueComparator> *out_of_order_queue = nullptr;

    // 期望接收的序列号
    uint32_t m_expect_sn{0};
    O3QueueComparator comp;

    bool use_o3_{false};
    bool is_shutdown_{false};

    // 应用层配置：最大允许乱序度距离（不超过rx_depth，如为0则默认为rx_depth的75%）
    uint32_t m_max_ooo_gap = UmqSetting::UMQ_MAX_O3_GAP != 0 ?
                                 std::min(UmqSetting::UMQ_MAX_O3_GAP, GlobalSetting::UBS_RX_DEPTH) :
                                 (GlobalSetting::UBS_RX_DEPTH >> 1) + (GlobalSetting::UBS_RX_DEPTH >> 2);
    ;
    // 应用层配置：断链最大等待超时（纳秒）
    uint64_t m_ooo_timeout_ns = (UmqSetting::UMQ_O3_TIMEOUT_MS != 0 ? UmqSetting::UMQ_O3_TIMEOUT_MS : 5) * 1000000ULL;
    // 首次发生断链（主槽位出现空洞）的时间戳
    uint64_t m_ooo_start_time_ns = 0;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_BUFFER_RECEIVE_QUEUE_H
