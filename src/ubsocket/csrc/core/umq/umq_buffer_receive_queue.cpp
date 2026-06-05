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

namespace ock {
namespace ubs {
namespace umq {

UmqBufferReceiveQueue::UmqBufferReceiveQueue()
{
    uint64_t queue_depth = GlobalSetting::UBS_SHARE_JFR_RX_QUEUE_DEPTH != 0 ?
                               GlobalSetting::UBS_SHARE_JFR_RX_QUEUE_DEPTH :
                               UmqSetting::UMQ_SHARE_JFR_RX_QUEUE_DEPTH;
    receive_queue = new (std::nothrow) QbufQueue<umq_buf_t *>(queue_depth);
    if (receive_queue == nullptr) {
        ClearAllocations();
        return;
    }

    use_o3_ = (UmqSetting::UMQ_UB_TRANS_MODE == RM_CTP);
    if (use_o3_) {
        uint32_t o3_queue_depth = GlobalSetting::UBS_SHARE_JFR_RX_O3_QUEUE_DEPTH != 0 ?
                                      GlobalSetting::UBS_SHARE_JFR_RX_O3_QUEUE_DEPTH :
                                      UmqSetting::UMQ_SHARE_JFR_RX_O3_QUEUE_DEPTH;
        if (o3_queue_depth > queue_depth) {
            o3_queue_depth = queue_depth;
            UBS_VLOG_WARN("O3 queue depth exceeds shared jfr rx queue depth; use share jfr rx depth instead.\n");
        }
        uint32_t max_depth = std::max<uint32_t>(o3_queue_depth, queue_depth);
        out_of_order_queue = new (std::nothrow) FastHeap<umq_buf_t *, O3QueueComparator>(o3_queue_depth, max_depth);
        if (out_of_order_queue == nullptr) {
            ClearAllocations();
            return;
        }
    }

    mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    if (mutex_ == nullptr) {
        UBS_VLOG_ERR("Failed to create fast heap lock.\n");
        ClearAllocations();
    }
}

UmqBufferReceiveQueue::~UmqBufferReceiveQueue()
{
    {
        Shutdown();
    }
    ClearAllocations();

    if (mutex_) {
        LockRegistry::LOCK_OPS.destroy(mutex_);
        mutex_ = nullptr;
    }
}

void UmqBufferReceiveQueue::Shutdown()
{
    Locker sLock(mutex_);
    if (is_shutdown_) {
        return;
    }
    is_shutdown_ = true;

    // 先 Flush 一次，提前释放内存，让后面排队的线程彻底死心
    FlushReceiveQueueInternal();
    if (use_o3_) {
        FlushOooQueueInternal();
    }
}

bool UmqBufferReceiveQueue::IsInitialized() const
{
    if (receive_queue == nullptr || mutex_ == nullptr) {
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
    if (!IsInitialized()) {
        UBS_VLOG_ERR("Failed to enqueue umq buffer, reason: queue not initialized.\n");
        return OpResult::ERROR;
    }
    if (is_shutdown_) {
        UBS_VLOG_WARN("Reject enqueue. Queue is already shutdown.\n");
        return OpResult::ERROR;
    }

    Locker sLock(mutex_);
    if (!use_o3_) {
        receive_queue->Enqueue(buffer);
        return OpResult::OK;
    }
    return EnqueueInOrder(buffer);
}

UmqBufferReceiveQueue::OpResult UmqBufferReceiveQueue::Dequeue(umq_buf_t **buffer)
{
    if (buffer == nullptr) {
        UBS_VLOG_ERR("Failed to dequeue umq buffer, reason: output buffer is null.\n");
        return OpResult::ERROR;
    }
    if (!IsInitialized()) {
        UBS_VLOG_ERR("Failed to dequeue umq buffer, reason: queue not initialized.\n");
        return OpResult::ERROR;
    }
    if (is_shutdown_) {
        UBS_VLOG_WARN("Reject dequeue. Queue is already shutdown.\n");
        return OpResult::ERROR;
    }
    Locker sLock(mutex_);
    if (receive_queue->IsEmpty()) {
        return OpResult::QUEUE_EMPTY;
    }
    if (receive_queue->Dequeue(buffer) != 0) {
        return OpResult::ERROR;
    }
    return OpResult::OK;
}

void UmqBufferReceiveQueue::ClearAllocations()
{
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
    if (buffer->status >= UMQ_FAKE_BUF_FC_UPDATE) {
        // 流控报文默认保序
        receive_queue->Enqueue(buffer);
        return OpResult::OK;
    }

    uint32_t sn = buf_pro->imm.user_data;
    if (sn == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
        // 探测包直接入队
        receive_queue->Enqueue(buffer);
        return OpResult::OK;
    }
    if (!UmqSeqTraits::ValidateAhead(m_expect_sn, sn)) {
        UBS_VLOG_WARN("Validate sn ahead failed, expect_sn: %u, sn: %u.\n", m_expect_sn, sn);
        UmqApi::umq_buf_free(buffer);
        return OpResult::ERROR;
    }

    if (UmqSeqTraits::Normalize(m_expect_sn) == UmqSeqTraits::Normalize(sn)) {
        receive_queue->Enqueue(buffer);
        m_expect_sn = UmqSeqTraits::Add(m_expect_sn, 1);

        // 乱序缓存队列检查
        while (!out_of_order_queue->IsEmpty()) {
            umq_buf_t *top_buf = out_of_order_queue->Top();
            uint32_t top_buf_sn = GetSn(top_buf);
            if (UmqSeqTraits::Normalize(m_expect_sn) == UmqSeqTraits::Normalize(top_buf_sn)) {
                receive_queue->Enqueue(top_buf);
                out_of_order_queue->Pop();
                m_expect_sn = UmqSeqTraits::Add(m_expect_sn, 1);
            } else if (UmqSeqTraits::CompareLessInCircularOrder(m_expect_sn, top_buf_sn)) {
                break; // 序列号不连续，中断迁移
            } else {
                // 重复/无效包
                out_of_order_queue->Pop();
                UmqApi::umq_buf_free(top_buf);
            }
        }
    } else {
        int ret = out_of_order_queue->Push(buffer);
        return ret == UBS_OK ? OpResult::OK : OpResult::ERROR;
    }
    return OpResult::OK;
}

void UmqBufferReceiveQueue::FlushOooQueueInternal()
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

void UmqBufferReceiveQueue::FlushReceiveQueueInternal()
{
    if (receive_queue == nullptr) {
        return;
    }
    while (!receive_queue->IsEmpty()) {
        umq_buf_t *buf[1];
        if (receive_queue->Dequeue(buf) != 0) {
            return;
        }
        if (buf[0]) {
            UmqApi::umq_buf_free(buf[0]);
        }
    }
}
} // namespace umq
} // namespace ubs
} // namespace ock