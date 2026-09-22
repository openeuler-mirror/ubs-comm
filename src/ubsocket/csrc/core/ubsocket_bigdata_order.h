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

/*
 * Ordered-completion helpers for the bigdata READ path (design §6.3, §7).
 *
 * UMQ READ completions may arrive out of order, but the application requires
 * delivery in send order. These helpers configure a chain of READ WRs so that
 * each WR produces its own TX CQE (complete_enable=1, comp_order=0), letting
 * the hardware reclaim SQ slots independently per WR. The software layer
 * finalizes the whole message only when all WRs complete (MarkDataWrComplete
 * waits for completed == wr_total), then links the destination buffers in
 * send order (LinkReadQbufsInOrder) and delivers via SN-ordered enqueue.
 *
 * Previously only the terminal WR produced a CQE (complete_enable=1 on the
 * last, comp_order chained on the rest). This caused SQ-full deadlocks under
 * high load: non-terminal WRs occupied SQ slots without producing CQEs, so
 * if any WR stalled (e.g. waiting for remote RQ space), the entire chain
 * stalled and no SQ slots were reclaimed.
 */
#ifndef UBS_COMM_UBSOCKET_BIGDATA_ORDER_H
#define UBS_COMM_UBSOCKET_BIGDATA_ORDER_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "umq_pro_types.h"
#include "umq_types.h"

namespace ock {
namespace ubs {
namespace detail {

inline bool BigdataOrderDebugEnabled()
{
    static const bool enabled = []() {
        const char *value = std::getenv("UBS_BIGDATA_ORDER_DEBUG");
        return value != nullptr &&
               (value[0] == '1' || std::strcmp(value, "true") == 0 || std::strcmp(value, "on") == 0);
    }();
    return enabled;
}

inline uint64_t BigdataOrderPrefix(const void *data, size_t len)
{
    uint64_t prefix = 0;
    if (data != nullptr && len != 0) {
        const size_t sample_len = len < sizeof(prefix) ? len : sizeof(prefix);
        std::memcpy(&prefix, data, sample_len);
    }
    return prefix;
}

inline void PrepareSyntheticReadQbuf(umq_buf_t *qbuf, uint32_t data_size)
{
    if (qbuf == nullptr) {
        return;
    }

    qbuf->data_size = data_size;
    qbuf->total_data_size = data_size;
    qbuf->qbuf_next = nullptr;
    // Pool reuse does not clear CQE metadata, and this software-only qbuf never receives a CQE.
    qbuf->status = 0;
    qbuf->io_direction = UMQ_IO_RX;
    std::memset(qbuf->qbuf_ext, 0, sizeof(qbuf->qbuf_ext));
}

inline umq_buf_t *FindReadWrTail(umq_buf_t *head)
{
    if (head == nullptr || head->total_data_size == 0) {
        return nullptr;
    }

    uint64_t remaining = head->total_data_size;
    for (umq_buf_t *buf = head; buf != nullptr; buf = buf->qbuf_next) {
        if (buf->data_size == 0 || buf->data_size > remaining) {
            return nullptr;
        }
        remaining -= buf->data_size;
        if (remaining == 0) {
            return buf;
        }
    }
    return nullptr;
}

inline bool ValidateStandaloneReadQbufChain(umq_buf_t *head, uint64_t expected_total, umq_buf_t **tail_out)
{
    if (tail_out != nullptr) {
        *tail_out = nullptr;
    }
    if (head == nullptr || head->total_data_size != expected_total) {
        return false;
    }

    umq_buf_t *tail = FindReadWrTail(head);
    if (tail == nullptr || tail->qbuf_next != nullptr) {
        return false;
    }
    if (tail_out != nullptr) {
        *tail_out = tail;
    }
    return true;
}

// One stable slot per data WR. READ keeps both the original qbuf and its
// ubsocket-only completion bookkeeping here until all WRs complete.
struct UbsBigQbufSlot {
    void *owner{nullptr};
    umq_buf_t *pending{nullptr};
    uint32_t completion_span{0};
};

inline bool ConfigureOrderedReadCompletions(UbsBigQbufSlot *slots, size_t count, bool ordered_with_previous = false)
{
    (void)ordered_with_previous;
    if (slots == nullptr || count == 0 || count > UINT32_MAX) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (slots[i].pending == nullptr) {
            return false;
        }
        auto *pro = reinterpret_cast<umq_buf_pro_t *>(slots[i].pending->qbuf_ext);
        if (pro->opcode != UMQ_OPC_READ) {
            return false;
        }
        /* Every WR produces its own CQE so SQ slots are reclaimed
         * independently as each READ completes. This prevents SQ-full
         * deadlocks that occurred when only the terminal WR generated
         * a CQE and a stalled non-terminal WR blocked the entire chain. */
        pro->flag.bs.complete_enable = 1;
        pro->flag.bs.comp_order = 0;
        slots[i].completion_span = 1;
    }
    return true;
}

inline bool LinkReadQbufsInOrder(UbsBigQbufSlot *slots, size_t count, umq_buf_t **head, umq_buf_t **tail)
{
    if (head == nullptr || tail == nullptr) {
        return false;
    }
    *head = nullptr;
    *tail = nullptr;
    if (slots == nullptr || count == 0) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        if (slots[i].pending == nullptr || FindReadWrTail(slots[i].pending) == nullptr) {
            return false;
        }
    }

    *head = slots[0].pending;
    for (size_t i = 0; i < count; ++i) {
        umq_buf_t *wr_tail = FindReadWrTail(slots[i].pending);
        wr_tail->qbuf_next = (i + 1 < count) ? slots[i + 1].pending : nullptr;
    }
    *tail = FindReadWrTail(slots[count - 1].pending);
    return true;
}

inline void PrependReadQbuf(umq_buf_t *prefix, umq_buf_t **head, umq_buf_t **tail)
{
    if (prefix == nullptr || head == nullptr || tail == nullptr) {
        return;
    }
    prefix->qbuf_next = *head;
    *head = prefix;
    if (*tail == nullptr) {
        *tail = prefix;
    }
}

} // namespace detail
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_BIGDATA_ORDER_H
