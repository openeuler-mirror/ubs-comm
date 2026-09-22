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
 * UBSocket UMQ data-plane API entry points (design §2.3).
 *
 * ubs_post routes a data_list through the adaptive bigdata engine; segments the
 * engine declines fall back to the normal SEND path. ubs_poll drains the
 * connection's SN-ordered receive queue. Both are scaffold-grade here: post
 * delegates small/all-declined batches to the existing SEND path, poll returns
 * EAGAIN while the ordered-delivery path is being built.
 */
#include <cerrno>

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_bigdata.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_socket.h"
#include "iobuf/ubsocket_iobuf.h"
#include "include/ubsocket_data.h"
#include "profiling/trace/ubsocket_trace.h"
#include "profiling/trace/ubs_pkt_trace.h"
#include "profiling/ubsocket_prof.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using ock::ubs::umq::UmqSocket;

namespace {

/* fd -> UmqSocket lookup matching the rest of the ubsocket data path. The
 * out-param holder keeps the SocketPtr alive across the call. */
ALWAYS_INLINE UmqSocket *GetUmqSocket(int fd, SocketPtr &holder)
{
    holder = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (holder == nullptr) {
        return nullptr;
    }
    return RefConvert<Socket, UmqSocket>(holder).Get();
}

/* RX zero-copy contract (mirrors brpc AdoptUbsPollSegment): placement-construct
 * an IOBuf::Block in the qbuf headroom at buf_data - sizeof(Block) so
 * Block::DecRef → ubsocket_iobuf_deallocate → umq_data_to_head(block->data)
 * can recover and umq_buf_free the owning qbuf. */
ALWAYS_INLINE Block *AdoptQbufAsPollBlock(umq_buf_t *qbuf)
{
    if (qbuf == nullptr || qbuf->buf_data == nullptr || qbuf->data_size == 0) {
        return nullptr;
    }
    if (qbuf->headroom_size < sizeof(Block)) {
        return nullptr;
    }
    char *data = qbuf->buf_data;
    return new (data - sizeof(Block)) Block(data, qbuf->data_size);
}

} // namespace

extern "C" {

ssize_t ubs_post(int fd, const ubs_data_list_t *data_list)
{
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    if (data_list == nullptr || (data_list->nsegs > 0 && data_list->segments == nullptr)) {
        UBS_VLOG_ERR("ubs_post data_list is invalid, fd: %d, nsegs: %u, segments: %p\n", fd, data_list->nsegs,
                     data_list->segments);
        errno = EINVAL;
        return -1;
    }
    SocketPtr holder;
    UmqSocket *umq_sock = GetUmqSocket(fd, holder);
    if (umq_sock == nullptr) {
        UBS_VLOG_ERR("ubs_post get umq socket failed, fd: %d\n", fd);
        errno = EPIPE;
        return -1;
    }
    if (holder->State() == SOCK_STAT_CLOSE) {
        UBS_VLOG_ERR("ubs_post socket is closed, fd: %d\n", fd);
        errno = EPIPE;
        return -1;
    }
    auto ts_entry_end = ubsocket_get_timeNs_compile();
    if (umq_sock->IsRnrBlocked()) {
        if (umq_sock->TryRnrBlockFatal()) {
            UBS_VLOG_WARN("RNR backpressure fatal timeout, fd: %d, disconnecting\n", fd);
            LibcApi::shutdown(fd, SHUT_RD);
            holder->State(SOCK_STAT_CLOSE);
            errno = EPIPE;
        } else {
            UBS_VLOG_DEBUG("ubs_post socket is rnr blocked, fd: %d\n", fd);
            errno = EAGAIN;
        }
        return -1;
    }
    if (data_list->nsegs == 0) {
        /* Empty batch: no SN, no UMQ call (design §2.3). */
        UBS_VLOG_WARN("ubs_post data_list empty, fd: %d, nsegs: %u\n", fd, data_list->nsegs);
        return 0;
    }

    /* SplitTrace: capture seq_no before/after TrySenderPost (approach A delayed write) */
    uint32_t pre_seq = umq_sock->LoadSeqNum();

    /* Adaptive bigdata path: per-segment routing by length. Small segments go
     * inline as SMALL_DATA SEND; large segments are pulled via READ_OFFER. */
    auto ts_sender_start = ubsocket_get_timeNs_compile();
    ssize_t big_ret = 0;
    bool sent = UbsBigdata::TrySenderPost(fd, data_list, &big_ret);
    auto ts_sender_end = ubsocket_get_timeNs_compile();

    if (sent) {
        /* SplitTrace: approach A — TrySenderPost succeeded, sample + batch write */
        uint32_t post_seq = umq_sock->LoadSeqNum();
        if (post_seq != pre_seq) {
            uint32_t seq_no = post_seq - 1;
            bool do_trace = STRACE_TRY_SAMPLE(holder.Get(), fd, seq_no, PATH_TX_POST);
            if (do_trace) {
                STRACE_ADD(holder.Get(), PATH_TX_POST, TX_POST_ENTRY, seq_no, ts_entry_start, ts_entry_end);
                STRACE_ADD(holder.Get(), PATH_TX_POST, TX_POST_SENDER_POST, seq_no, ts_sender_start, ts_sender_end);
                STRACE_ADD(holder.Get(), PATH_TX_POST, TX_POST_UMQ_POST, seq_no, ts_sender_start, ts_sender_end);
                STRACE_END(holder.Get(), PATH_TX_POST, seq_no, static_cast<uint32_t>(big_ret > 0 ? big_ret : 0), 0);
            }
        }
        if (GlobalSetting::UBS_MONITOR_ENABLE && big_ret > 0) {
            SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(holder);
            if (auto *mgr = sockptr->GetStatsMgr()) {
                mgr->UpdateTraceStats(Statistics::StatsMgr::TX_PACKET_COUNT, static_cast<uint32_t>(big_ret));
                uint32_t tx_bytes = 0;
                for (ssize_t i = 0; i < big_ret; ++i) {
                    tx_bytes += data_list->segments[i].len;
                }
                mgr->UpdateTraceStats(Statistics::StatsMgr::TX_BYTE_COUNT, tx_bytes);
            }
        }
        return big_ret;
    }

    /* Declined (e.g. not a umq socket) — no fallback path here. */
    errno = EPIPE;
    UBS_VLOG_ERR("ubs_post data_list declined, fd: %d, nsegs: %u, segments: %p\n", fd, data_list->nsegs,
                 data_list->segments);
    return -1;
}

int ubs_poll(int fd, ubs_data_list_t *out)
{
    auto ts_entry_start = ubsocket_get_timeNs_compile();
    if (out == nullptr || out->segments == nullptr || out->nsegs == 0) {
        UBS_VLOG_ERR("ubs_poll out parameter is invalid, fd: %d\n", fd);
        errno = EINVAL;
        return -1;
    }
    const uint16_t capacity = out->nsegs;
    out->nsegs = 0;

    SocketPtr holder;
    UmqSocket *umq_sock = GetUmqSocket(fd, holder);
    if (umq_sock == nullptr) {
        UBS_VLOG_ERR("ubs_poll get umq socket failed, fd: %d\n", fd);
        errno = EPIPE;
        return -1;
    }
    if (holder->State() == SOCK_STAT_CLOSE) {
        /* SOCK_STAT_CLOSE in ubs_poll is only reached when the TCP peer
         * gracefully closed the connection (EOF). Abnormal resets surface
         * through error CQE / GetAndPopQbuf paths below, not here.
         * Return 0 with errno=0 to signal normal close to the caller. */
        UBS_VLOG_INFO("ubs_poll socket peer closed (EOF), fd: %d\n", fd);
        errno = 0;
        return 0;
    }
    auto ts_entry_end = ubsocket_get_timeNs_compile();

    /* Peer-close probe at ENTRY, not only on the empty-queue path. The raw
     * TCP fd's FIN is a single edge-triggered EPOLLIN; if that one wakeup
     * happens to find the UMQ queue non-empty, the old filled==0-only probe
     * is skipped, the edge is consumed, and nothing ever re-checks the
     * socket — the server-side link leaks forever (reproduces readily under
     * mass teardown, e.g. 48 clients releasing warmup links at once). A
     * non-blocking MSG_PEEK costs one syscall and makes detection
     * independent of which branch this poll takes. rc > 0 (stale bytes in
     * the TCP buffer, e.g. negotiation residue) is NOT a close and is
     * ignored; only rc == 0 means EOF. */
    char probe;
    const bool peer_closed = (LibcApi::recv(fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT) == 0);

    /* Drain up to `capacity` SN-ordered buffers into the caller-provided array.
     * The receive queue's EnqueueInOrder guarantees only continuously-ready
     * segments reach the head, so every dequeue is the next in-order segment
     * (design §6.4). */
    uint16_t filled = 0;
    uint32_t first_seq_no = 0;
    while (filled < capacity) {
        umq_buf_t *qbuf = nullptr;
        int ret = umq_sock->GetAndPopQbuf(&qbuf, 1);
        if (ret < 0) {
            /* Queue-full or transient error: return what we have, or EAGAIN. */
            if (filled > 0) {
                break;
            }
            UBS_VLOG_ERR("ubs_poll GetAndPopQbuf failed, fd: %d, ret: %d, errno: %d\n", fd, ret, errno);
            /* 无队列且对端已关：按 EOF 收尾而非 EAGAIN——否则边沿事件被消费后
             * 再无唤醒，连接以"永远暂不可读"泄漏（issue#33 分析的伴生洞） */
            if (peer_closed) {
                errno = 0;
                return 0;
            }
            /* errno 契约兜底：下层任何 <0 都应带非零 errno（GetAndPopQbuf 已
             * 显式赋值）；此处再拦一道，0 一律视为暂不可读，绝不放行进
             * brpc Controller::SetFailed 的 CHECK(error_code != 0)（issue#33） */
            if (errno == 0) {
                errno = EAGAIN;
            }
            return -1;
        }
        if (ret == 0 || qbuf == nullptr) {
            break; /* nothing more ready */
        }
        /* Error buffer (status != 0): remote side unbound or transport error.
        * Must surface as ECONNRESET so the caller (DoUbsNativeRead →
        * OnUbNativeMessages) can detect the disconnect and tear down the
        * socket. Silently dropping it leaves the connection alive forever. */
        if (qbuf->status != 0) {
            UBS_VLOG_ERR("ubs_poll error buffer, fd: %d, status: %d\n", fd, qbuf->status);
            qbuf->qbuf_next = nullptr;
            UmqApi::umq_buf_free(qbuf);
            if (filled > 0) {
                break; /* return already-collected data first */
            }
            errno = ECONNRESET;
            return -1;
        }

        if (qbuf->data_size == 0 || qbuf->buf_data == nullptr) {
            qbuf->qbuf_next = nullptr;
            UmqApi::umq_buf_free(qbuf);
            continue;
        }
        /* Big-data control messages (READ_OFFER/READ_DONE/READ_ABORT) carry
         * imm_data bit 20 set (UBS_IMM_BIG_CTRL_BIT). Only packets so marked
         * are dispatched to the bigdata engine; ordinary SMALL_DATA falls
         * through to the caller. */
        if (qbuf->qbuf_ext != nullptr) {
            auto *rx_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
            const bool big_ctrl = proto::is_big_ctrl(rx_pro->imm_data);
            UBS_DATAPATH_LOG("[datapath] RX classify, fd: %d, imm_data: 0x%llx, big_ctrl: %d, sn: %u, len: %u\n", fd,
                             static_cast<unsigned long long>(rx_pro->imm_data), static_cast<int>(big_ctrl),
                             rx_pro->imm.user_data, qbuf->data_size);
            if (big_ctrl) {
                const bool consumed = UbsBigdata::HandleRxControl(holder, qbuf);
                UBS_DATAPATH_LOG("[datapath] RX big_ctrl dispatch, fd: %d, imm_data: 0x%llx, consumed: %d\n", fd,
                                 static_cast<unsigned long long>(rx_pro->imm_data), static_cast<int>(consumed));
                if (consumed) {
                    /* HandleRxControl consumed the buffer (DoReadOffer retained it
                     * for the transaction; other controls freed it). Don't return
                     * it to the caller and don't free it here. */
                    continue;
                }
            }
        }
        ubs_segment_t &seg = out->segments[filled];
        Block *block = AdoptQbufAsPollBlock(qbuf);
        if (block == nullptr) {
            UBS_VLOG_ERR("ubs_poll insufficient headroom for Block adoption, fd: %d, headroom: %u\n", fd,
                         static_cast<unsigned>(qbuf->headroom_size));
            qbuf->qbuf_next = nullptr;
            UmqApi::umq_buf_free(qbuf);
            continue;
        }
        seg.block = block;
        seg.offset = 0;
        seg.len = qbuf->data_size;
        seg.start_pos = qbuf->buf_data;
        seg.user_ctx = reinterpret_cast<uint64_t>(qbuf);
        uint32_t seg_sn = 0;
        if (qbuf->qbuf_ext != nullptr) {
            seg_sn = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext)->imm.user_data;
        }
        if (filled == 0 && qbuf->qbuf_ext != nullptr) {
            first_seq_no = seg_sn;
        }
        ++filled;
        /* Four-point-trace byte-offset bridge: advance the connection-level
         * delivered-bytes cursor by this segment and stamp (fd,sn,cursor,ts)
         * so the offline tool can align ubsocket SNs with brpc cids that share
         * the same byte stream. Cheap no-op unless UBS_PKT_TRACE_ENABLE. */
        if (UbsPktTraceEnabled()) {
            umq_sock->delivered_bytes_ += seg.len;
            /* UbsPktTraceNowNs (CLOCK_MONOTONIC), not the cntvct fast path:
             * must share brpc's clock domain for the offline T3-delta. */
            UbsPktTrace(fd, seg_sn, umq_sock->delivered_bytes_,
                        UbsPktTraceNowNs());
        }
        /* SN extraction lives inside the log args so it is only evaluated when
         * UBS_DATAPATH_DEBUG is on (the macro is a no-op otherwise). */
        UBS_DATAPATH_LOG(
            "[datapath] ubs_poll deliver segment, fd: %d, idx: %u, sn: %u, len: %u\n", fd, filled - 1,
            (qbuf->qbuf_ext != nullptr) ? reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext)->imm.user_data : 0u,
            seg.len);
    }

    if (filled == 0) {
        /* Nothing to deliver. If the entry probe saw TCP EOF (peer's
         * graceful close), return 0 to signal EOF to the caller
         * (DoUbsNativeRead → OnUbNativeMessages), mirroring readv's
         * RxDataSet EOF semantics; otherwise plain EAGAIN. */
        if (peer_closed) {
            UBS_DATAPATH_LOG("ubs_poll: TCP connection closed by peer, fd: %d\n", fd);
            errno = 0;
            return 0;
        }
        errno = EAGAIN;
        return -1;
    }
    /* Data collected AND peer closed: deliver the data first (never drop
     * in-order payload); the caller's edge-triggered loop polls again
     * immediately, and that next call finds an empty queue with the probe
     * still reporting EOF and reports the close then. */
    out->nsegs = filled;
    auto ts_exit = ubsocket_get_timeNs_compile();

    if (GlobalSetting::UBS_MONITOR_ENABLE) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(holder);
        if (auto *mgr = sockptr->GetStatsMgr()) {
            mgr->UpdateTraceStats(Statistics::StatsMgr::RX_PACKET_COUNT, filled);
            uint32_t rx_bytes = 0;
            for (uint16_t i = 0; i < filled; ++i) {
                rx_bytes += out->segments[i].len;
            }
            mgr->UpdateTraceStats(Statistics::StatsMgr::RX_BYTE_COUNT, rx_bytes);
        }
    }

    /* SplitTrace: ubs_poll path — RX side, seq_no from first delivered segment */
    bool do_trace = STRACE_TRY_SAMPLE(holder.Get(), fd, first_seq_no, PATH_RX_POLL);
    if (do_trace) {
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_ENTRY, first_seq_no, ts_entry_start, ts_entry_end);
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_GET_AND_POP, first_seq_no, ts_entry_end, ts_exit);
        STRACE_ADD(holder.Get(), PATH_RX_POLL, RX_POLL_DELIVER_SEG, first_seq_no, ts_entry_end, ts_exit);
        STRACE_END(holder.Get(), PATH_RX_POLL, first_seq_no, filled, 0);
    }

    UBS_DATAPATH_LOG("[datapath] ubs_poll return, fd: %d, nsegs: %u%s\n", fd, filled,
                     peer_closed ? " (peer closed, reported on next poll)" : "");
    return filled;
}

} // extern "C"
