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

#ifndef UBS_COMM_UBSOCKET_QBUF_QUEUE_H
#define UBS_COMM_UBSOCKET_QBUF_QUEUE_H

#include <malloc.h>
#include <cstdint>
#include "../../include/ubsocket_def.h"
#include "../common/ubsocket_lock.h"
#include "../common/ubsocket_logger.h"

namespace ock {
namespace ubs {
template <class T>
struct QbufQueueT {
    uint32_t head;
    uint32_t tail;
    uint32_t itemNb;
    T q[0];
};

template <typename T>
class QbufQueue {
public:
    explicit QbufQueue(uint32_t itemNb) : isMalloc_(false), isExit_(false), init_cap_(itemNb)
    {
        if (InitQueue(itemNb) != 0) {
            UBS_VLOG_ERR("Init qbuf queue failed. \n");
            isExit_ = true;
            return;
        }
    }

    ~QbufQueue()
    {
        if (isMalloc_) {
            isExit_ = true;
            if (queue_) {
                free(queue_);
                queue_ = nullptr;
            }
        }
    }

    inline bool IsEmpty()
    {
        return queue_ && queue_->head == queue_->tail;
    }

    inline bool IsFull()
    {
        return queue_ && queue_->head == (queue_->tail + 1) % queue_->itemNb;
    }

    inline uint32_t Size() const
    {
        if (!queue_) {
            return 0;
        }

        uint32_t h = queue_->head;
        uint32_t t = queue_->tail;
        return (t >= h) ? (t - h) : (queue_->itemNb + t - h);
    }

    int Enqueue(T data)
    {
        if (isExit_) {
            UBS_VLOG_WARN("Enqueue qbuf queue failed, reason: queue already exit\n");
            return -1;
        }

        if (queue_ == nullptr) {
            UBS_VLOG_ERR("Enqueue qbuf queue failed, reason: queue is null\n");
            return -1;
        }
        if (IsFull()) {
            if (!isMalloc_) {
                UBS_VLOG_ERR("Enqueue qbuf queue failed, reason: queue is full, head: %u, tail: %u, itemNb: %u\n",
                             queue_->head, queue_->tail, queue_->itemNb);
                return -1;
            }

            uint32_t old_cap = queue_->itemNb - 1;
            uint32_t new_cap = (old_cap * 2 > MAX_CAPACITY) ? MAX_CAPACITY : old_cap * 2;
            if (Resize(new_cap) != 0) {
                UBS_VLOG_ERR("Enqueue qbuf queue failed, reason: queue is full and resize failed, head: %u, tail: "
                             "%u, itemNb: %u\n",
                             queue_->head, queue_->tail, queue_->itemNb);
                return -1;
            }
        }

        queue_->q[queue_->tail] = data;
        queue_->tail = (queue_->tail == queue_->itemNb - 1) ? 0 : queue_->tail + 1;
        return 0;
    }

    int Dequeue(T *data)
    {
        if (data == nullptr) {
            UBS_VLOG_ERR("Dequeue qbuf queue failed, reason: data is null\n");
            return -1;
        }

        if (isExit_) {
            UBS_VLOG_WARN("Dequeue qbuf queue failed, reason: queue already exit\n");
            return -1;
        }

        if (queue_ == nullptr) {
            UBS_VLOG_ERR("Dequeue qbuf queue failed, reason: queue is null\n");
            return -1;
        }

        if (IsEmpty()) {
            UBS_VLOG_WARN("Dequeue qbuf queue failed, reason: queue is empty, head: %u, tail: %u, itemNb: %u\n",
                          queue_->head, queue_->tail, queue_->itemNb);
            return -1;
        }

        *data = queue_->q[queue_->head];
        queue_->head = (queue_->head == queue_->itemNb - 1) ? 0 : queue_->head + 1;

        if (isMalloc_) {
            uint32_t cur_cap = queue_->itemNb - 1;
            uint32_t count = Size();
            // 缩容条件：使用率 <= 25% 且 当前容量 > 初始容量 * 2
            // 25% 与扩容的100% 形成75%的滞回区间，彻底消除临界点抖动
            if (count <= (cur_cap >> 2) && cur_cap > (init_cap_ << 1)) {
                // 缩容至当前容量的50%
                uint32_t new_cap = (cur_cap >> 1);
                if (new_cap < init_cap_) {
                    new_cap = init_cap_;
                }
                Resize(new_cap);
            }
        }
        return 0;
    }

    struct QbufQueueT<T> *queue_;

private:
    bool isMalloc_ = false;
    volatile bool isExit_ = false;
    uint32_t init_cap_;
    static constexpr uint32_t MAX_CAPACITY = 0x3FFFFFFF;

    size_t RoundUp(size_t size, size_t align)
    {
        return (size + align - 1) - ((size + align - 1) % align);
    }

    int InitQueue(size_t itemNb)
    {
        size_t pageSize = sysconf(_SC_PAGESIZE);
        size_t headLen = sizeof(struct QbufQueueT<T>) + (itemNb + 1) * sizeof(T);
        headLen = RoundUp(headLen, pageSize);
        queue_ = reinterpret_cast<struct QbufQueueT<T> *>(memalign(pageSize, headLen));
        if (queue_ == nullptr) {
            UBS_VLOG_ERR("Init qbuf queue memalign failed, pageSize: %zu, headLen: %zu, errno: %d\n", pageSize, headLen,
                         errno);
            return -1;
        }

        isMalloc_ = true;
        memset(queue_, 0, sizeof(struct QbufQueueT<T>));
        queue_->itemNb = itemNb + 1;
        return 0;
    }

    int Resize(uint32_t new_cap)
    {
        uint32_t old_itemNb = queue_->itemNb;
        uint32_t new_itemNb = new_cap + 1;
        if (new_itemNb == old_itemNb) {
            UBS_VLOG_ERR("Resize qbuf queue failed, new capacity equals old capacity: %d\n", new_cap);
            return -1;
        }

        size_t pageSize = sysconf(_SC_PAGESIZE);
        size_t newHeadLen = sizeof(struct QbufQueueT<T>) + new_itemNb * sizeof(T);
        newHeadLen = RoundUp(newHeadLen, pageSize);
        struct QbufQueueT<T> *new_q = reinterpret_cast<struct QbufQueueT<T> *>(aligned_alloc(pageSize, newHeadLen));
        if (new_q == nullptr) {
            UBS_VLOG_ERR("Resize qbuf queue aligned_alloc failed, pageSize: %zu, headLen: %zu, errno: %d\n", pageSize,
                         newHeadLen, errno);
            return -1;
        }

        uint32_t count = Size();
        for (uint32_t i = 0; i < count; ++i) {
            new_q->q[i] = queue_->q[(queue_->head + i) % old_itemNb];
        }

        new_q->head = 0;
        new_q->tail = count;
        new_q->itemNb = new_itemNb;

        struct QbufQueueT<T> *old_q = queue_;
        queue_ = new_q;
        if (isMalloc_) {
            free(old_q);
        }

        UBS_VLOG_DEBUG("Resize qbuf queue success, old capacity: %d, new capacity: %d \n", old_itemNb - 1, new_cap);
        return 0;
    }
};
} // namespace ubs
} // namespace ock

#endif
