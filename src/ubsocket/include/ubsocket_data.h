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
 * UBSocket UMQ data-plane API (design doc §2).
 *
 * The send path expresses a bRPC byte-stream fragment as one or more segments,
 * each identified by (block + offset + len). The UBSocket engine routes every
 * segment adaptively: small segments go inline as SMALL_DATA SEND, large ones
 * are pulled by the peer via RDMA READ through a READ_OFFER. The receive path
 * returns already completed, SN-ordered segments through a caller-provided
 * out list. Control messages (READ_OFFER / READ_DONE / READ_ABORT) are
 * internal to UBSocket and never surface through ubs_poll.
 */
#ifndef UBS_COMM_UBSOCKET_DATA_H
#define UBS_COMM_UBSOCKET_DATA_H

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h> /* ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One contiguous data range of a message.
 *
 * @ingroup ubsocket_data
 *
 * block is opaque to UBSocket; on the C++ side it is a butil::IOBuf::Block*
 * passed as void* across the C boundary. [offset, offset+len) must lie within
 * the block's valid data range. data is an optional address cache equal to
 * block->data + offset; when NULL the engine computes the address. user_ctx
 * is carried with the segment lifetime for completion callbacks / brpc-side
 * object association and is never interpreted by UBSocket.
 */
typedef struct {
    void *block;       /* butil::IOBuf::Block* across the C boundary */
    uint32_t offset;   /* real data position relative to block->data */
    uint32_t len;      /* valid length of this segment */
    void *start_pos;   /* optional cache: block->data; NULL → computed */
    uint64_t user_ctx; /* caller-defined; UBSocket never interprets it */
} ubs_segment_t;

/**
 * @brief A batch of segments submitted as one atomic unit.
 *
 * @ingroup ubsocket_data
 *
 * nsegs > 0 requires segments != NULL; nsegs == 0 is an empty batch. ubs_post
 * only reads segments[0..nsegs) and never takes ownership of the array. The
 * batch's total length is computed inside ubs_post as the checked sum of
 * segment lengths; it is not stored here.
 */
typedef struct {
    ubs_segment_t *segments;
    uint16_t nsegs;
} ubs_data_list_t;

/**
 * @brief Batch-submit data_list->nsegs segments.
 *
 * @ingroup ubsocket_data
 *
 * Each segment is routed adaptively (design §5): segments with len <=
 * UBS_SMALL_DATA_MAX go inline as SMALL_DATA (pure payload, kind carried by
 * imm_data bit 20 = 0); the rest are pulled by the peer through READ_OFFER.
 * A data_list is an indivisible submit unit; segment-level partial success is
 * never reported.
 *
 * @param fd        [in] ubsocket file descriptor.
 * @param data_list [in] segments to submit; the array is read-only here.
 * @return accepted segment count on success;
 *         -1 on failure with errno set:
 *           EINVAL  - nsegs > 0 but segments == NULL, segment out of range,
 *                     or batch length overflow;
 *           EAGAIN  - TX credit not available;
 *           EPIPE  - connection closed;
 *           EIO    - UMQ / CQ error.
 *         An empty batch (nsegs == 0) returns 0 without allocating an SN.
 */
ssize_t ubs_post(int fd, const ubs_data_list_t *data_list);

/**
 * @brief Drain a batch of SN-ordered, already-completed segments.
 *
 * @ingroup ubsocket_data
 *
 * The caller MUST provide the segment array: set out->segments to a buffer
 * of at least out->nsegs (capacity) entries before calling. On success
 * out->nsegs is updated to the actual number of segments filled (≤ capacity).
 * The segments array is caller-owned throughout; no internal allocation
 * or ownership transfer occurs.
 *
 * Each call returns up to capacity segments. Bigdata READ completions chain
 * multiple fragments sharing one SN; if the chain exceeds capacity the surplus
 * is buffered internally and returned by subsequent poll calls.
 *
 * @param fd  [in]  ubsocket file descriptor.
 * @param out [inout] caller-provided; on success segments[0..nsegs) filled.
 * @return >0 - number of segments retrieved (also stored in out->nsegs);
 *          0 - peer gracefully closed (TCP EOF); out left untouched;
 *         -1 - failure or nothing ready, with errno set:
 *                EAGAIN      - nothing ready right now (out left untouched);
 *                ECONNRESET  - transport error (error CQE / qbuf->status != 0);
 *                EPIPE       - fd not found / not a umq socket;
 *                EINVAL      - invalid out parameter.
 *         No half-filled out is ever published on 0 or -1.
 */
int ubs_poll(int fd, ubs_data_list_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UBS_COMM_UBSOCKET_DATA_H */
