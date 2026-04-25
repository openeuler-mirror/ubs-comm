/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-01-10
 * Note:
 * History: 2026-01-10
*/

#ifndef QBUF_QUEUE_H
#define QBUF_QUEUE_H

#include <malloc.h>
#include "umq_types.h"

namespace Brpc {
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
    explicit QbufQueue(struct QbufQueueT<T> *q) : m_q(q), m_isMalloc(false)
    {
        mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
        if (mutex == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Create qbuf queue lock failed\n");
        }
    }

    explicit QbufQueue(uint32_t itemNb) : m_isMalloc(false), m_isExit(false), m_init_cap(itemNb)
    {
        mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
        if (mutex == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Create qbuf queue lock failed\n");
            return;
        }

        if (InitQueue(itemNb) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Init qbuf queue failed. \n");
            m_isExit = true;
            return;
        }
    }

    ~QbufQueue()
    {
        if (m_isMalloc) {
            m_isExit = true;
            if (m_q) {
                free(m_q);
                m_q = nullptr;
            }
        }
        if (mutex != nullptr) {
            g_external_lock_ops.destroy(mutex);
        }
    }

    inline bool IsEmpty()
    {
        return m_q && m_q->head == m_q->tail;
    }

    inline bool IsFull()
    {
        return m_q && m_q->head == (m_q->tail + 1) % m_q->itemNb;
    }

    inline uint32_t Size() const
    {
        if (!m_q) {
            return 0;
        }

        uint32_t h = m_q->head;
        uint32_t t = m_q->tail;
        return (t >= h) ? (t - h) : (m_q->itemNb + t - h);
    }

    int Enqueue(T data)
    {
        if (m_isExit) {
            RPC_ADPT_VLOG_WARN("Enqueue qbuf queue failed, reason: queue already exit\n");
            return -1;
        }

        if (m_q == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Enqueue qbuf queue failed, reason: queue is null\n");
            return -1;
        }

        if (mutex == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Enqueue qbuf queue failed, reason: lock is null\n");
            return -1;
        }

        ScopedUbExclusiveLocker sLock(mutex);
        if (IsFull()) {
            if (!m_isMalloc) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                                  "Enqueue qbuf queue failed, reason: queue is full, head: %u, tail: %u, itemNb: %u\n",
                                  m_q->head, m_q->tail, m_q->itemNb);
                return -1;
            }

            uint32_t old_cap = m_q->itemNb - 1;
            uint32_t new_cap = (old_cap * 2 > MAX_CAPACITY) ? MAX_CAPACITY : old_cap * 2;
            if (Resize(new_cap) != 0) {
                RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                                  "Enqueue qbuf queue failed, reason: queue is full and resize failed, head: %u, tail: "
                                  "%u, itemNb: %u\n",
                                  m_q->head, m_q->tail, m_q->itemNb);
                return -1;
            }
        }

        m_q->q[m_q->tail] = data;
        m_q->tail = (m_q->tail == m_q->itemNb - 1) ? 0 : m_q->tail + 1;
        return 0;
    }

    int Dequeue(T *data)
    {
        if (data == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Dequeue qbuf queue failed, reason: data is null\n");
            return -1;
        }

        if (m_isExit) {
            RPC_ADPT_VLOG_WARN("Dequeue qbuf queue failed, reason: queue already exit\n");
            return -1;
        }

        if (m_q == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Dequeue qbuf queue failed, reason: queue is null\n");
            return -1;
        }

        if (mutex == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Dequeue qbuf queue failed, reason: lock is null\n");
            return -1;
        }

        if (IsEmpty()) {
            RPC_ADPT_VLOG_WARN(
                "Dequeue qbuf queue failed, reason: queue is empty, head: %u, tail: %u, itemNb: %u\n",
                m_q->head, m_q->tail, m_q->itemNb);
            return -1;
        }

        ScopedUbExclusiveLocker sLock(mutex);
        *data = m_q->q[m_q->head];
        m_q->head = (m_q->head == m_q->itemNb - 1) ? 0 : m_q->head + 1;

        if (m_isMalloc) {
            uint32_t cur_cap = m_q->itemNb - 1;
            uint32_t count = Size();
            // 缩容条件：使用率 <= 25% 且 当前容量 > 初始容量 * 2
            // 25% 与扩容的100% 形成75%的滞回区间，彻底消除临界点抖动
            if (count <= (cur_cap >> 2) && cur_cap > (m_init_cap << 1)) {
                // 缩容至当前容量的50%
                uint32_t new_cap = (cur_cap >> 1);
                if (new_cap < m_init_cap) {
                    new_cap = m_init_cap;
                }
                Resize(new_cap);
            }
        }
        return 0;
    }

    struct QbufQueueT<T> *m_q;

private:
    bool m_isMalloc = false;
    volatile bool m_isExit = false;
    uint32_t m_init_cap;
    u_external_mutex_t *mutex = nullptr;
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
        m_q = reinterpret_cast<struct QbufQueueT<T> *>(memalign(pageSize, headLen));
        if (m_q == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Init qbuf queue memalign failed, pageSize: %zu, headLen: %zu, errno: %d\n",
                pageSize, headLen, errno);
            return -1;
        }

        m_isMalloc = true;
        memset_s(m_q, sizeof(struct QbufQueueT<T>), 0, sizeof(struct QbufQueueT<T>));
        m_q->itemNb = itemNb + 1;
        return 0;
    }

    int Resize(uint32_t new_cap)
    {
        uint32_t old_itemNb = m_q->itemNb;
        uint32_t new_itemNb = new_cap + 1;
        if (new_itemNb == old_itemNb) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Resize qbuf queue failed, new capacity equals old capacity: %d\n",
                              new_cap);
            return -1;
        }

        size_t pageSize = sysconf(_SC_PAGESIZE);
        size_t newHeadLen = sizeof(struct QbufQueueT<T>) + new_itemNb * sizeof(T);
        newHeadLen = RoundUp(newHeadLen, pageSize);
        struct QbufQueueT<T> *new_q = reinterpret_cast<struct QbufQueueT<T> *>(aligned_alloc(pageSize, newHeadLen));
        if (new_q == nullptr) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                "Resize qbuf queue aligned_alloc failed, pageSize: %zu, headLen: %zu, errno: %d\n",
                pageSize, newHeadLen, errno);
            return -1;
        }

        uint32_t count = Size();
        for (uint32_t i = 0; i < count; ++i) {
            new_q->q[i] = m_q->q[(m_q->head + i) % old_itemNb];
        }

        new_q->head = 0;
        new_q->tail = count;
        new_q->itemNb = new_itemNb;

        struct QbufQueueT<T>* old_q = m_q;
        m_q = new_q;
        if (m_isMalloc) {
            free(old_q);
        }

        RPC_ADPT_VLOG_DEBUG("Resize qbuf queue success, old capacity: %d, new capacity: %d \n", old_itemNb - 1,
                            new_cap);
        return 0;
    }
};

}

#endif
