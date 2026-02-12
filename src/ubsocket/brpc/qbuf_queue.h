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
    explicit QbufQueue(struct QbufQueueT<T> *q) : m_q(q), m_isMalloc(false) {}

    explicit QbufQueue(uint32_t itemNb)
    {
        if (InitQueue(itemNb) != 0) {
            RPC_ADPT_VLOG_ERR("Init qbuf queue failed. \n");
            return;
        }
    }

    ~QbufQueue()
    {
        if (m_isMalloc) {
            m_isExit = true;
            free(m_q);
            m_q = nullptr;
        }
    }

    inline bool IsEmpty()
    {
        return m_q->head == m_q->tail;
    }

    inline bool IsFull()
    {
        return m_q->head == (m_q->tail + 1) % m_q->itemNb;
    }

    int Enqueue(T data)
    {
        if (m_isExit || IsFull()) {
            return -1;
        }

        std::lock_guard<std::mutex> lock(mutex);
        m_q->q[m_q->tail] = data;
        m_q->tail = (m_q->tail == m_q->itemNb - 1) ? 0 : m_q->tail + 1;
        return 0;
    }

    int Dequeue(T *data)
    {
        if (m_isExit || IsEmpty()) {
            return -1;
        }

        std::lock_guard<std::mutex> lock(mutex);
        *data = m_q->q[m_q->head];
        m_q->head = (m_q->head == m_q->itemNb - 1) ? 0 : m_q->head + 1;
        return 0;
    }

    uint32_t UsedNb()
    {
        return m_q->tail + m_q->itemNb - m_q->head;
    }

    uint32_t FreeNb()
    {
        if (m_q->head > m_q->tail) {
            return m_q->head - m_q->tail;
        }

        return m_q->head + m_q->itemNb - 1 - m_q->tail;
    }

    struct QbufQueueT<T> *m_q;

private:
    bool m_isMalloc;
    volatile bool m_isExit = false;
    mutable std::mutex mutex;

    size_t RoundUp(size_t size, size_t align)
    {
        return (size + align - 1) - ((size + align - 1) % align);
    }

    int InitQueue(size_t itemNb)
    {
        size_t pageSize = sysconf(_SC_PAGESIZE);
        size_t headLen = sizeof(struct QbufQueueT<T>) + (itemNb + 1) * sizeof(umq_buf_t);
        headLen = RoundUp(headLen, pageSize);
        m_q = reinterpret_cast<struct QbufQueueT<T> *>(memalign(pageSize, headLen));
        if (m_q == nullptr) {
            RPC_ADPT_VLOG_ERR("Init qbuf queue memalgin failed, pageSize %u, headLen %u, errno %d \n", pageSize,
                              headLen, errno);
            return -1;
        }

        m_isMalloc = true;
        memset_s(m_q, sizeof(struct QbufQueueT<T>), 0, sizeof(struct QbufQueueT<T>));
        m_q->itemNb = itemNb + 1;
        return 0;
    }
};

}

#endif
