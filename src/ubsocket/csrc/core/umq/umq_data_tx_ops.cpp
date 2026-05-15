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
#include "umq_data_tx_ops.h"
#include "ubsocket_iobuf_adapter.h"

namespace ock {
namespace ubs {
namespace umq {

uintptr_t UmqTxOps::AllocTxBuf(uint32_t count)
{
    umq_buf_t *tx_buf_list = umq_buf_alloc(0, count, UMQ_INVALID_HANDLE, nullptr);
    if (tx_buf_list == nullptr) {
        UBS_VLOG_ERR("umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                     static_cast<unsigned long long>(local_umqh_), tx_buf_list);
        return DpRearmTxInterrupt();
    }

    return reinterpret_cast<uintptr_t>(tx_buf_list);
}

int UmqTxOps::PostSend(uintptr_t buf, uint32_t batch, IovConverter cvt)
{
    umq_buf_t *tx_buf_list = reinterpret_cast<umq_buf_t *>(buf);
    int flagEIO = -1;
    umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&head_buf_);
    umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&tail_buf_);
    uint16_t _unsolicited_wr_num = unsignaled_wr_num_;
    uint32_t _unsolicited_bytes = unsolicited_bytes_;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num_;

    umq_buf_t *cur_buf = tx_buf_list;
    umq_buf_t *next_buf = cur_buf;
    if (QBUF_LIST_EMPTY(&head_buf_)) {
        QBUF_LIST_FIRST(&head_buf_) = cur_buf;
    } else {
        QBUF_LIST_NEXT(QBUF_LIST_FIRST(&tail_buf_)) = cur_buf;
    }
    uint32_t tx_total_len = 0;
    cvt.Reset();
    for (uint32_t i = 0; i < batch; ++i) {
        umq_buf_t *cur_wr_first = next_buf;
        uint32_t moved_total_len = 0;
        uint32_t wr_left_len = UmqSetting::GetIOBufSize();
        uint32_t sge_idx = 0;
        bool last = false;
        for (cur_buf = cur_wr_first; cur_buf && (next_buf = cur_buf->qbuf_next, 1); cur_buf = next_buf) {
            last = CutLast(cvt, wr_left_len, cur_buf);
            cur_buf->io_direction = UMQ_IO_TX;
            /* rpc adapter has replace brpc butil::iobuf::blockmem_allocate() & butil::iof::blockmem_deallocate()
             * and ensures that the starting address of the Block is aligned to an 8k boundary. */
            ((Block *)PtrFloorToBoundary(cur_buf->buf_data))->IncRef();
            wr_left_len -= cur_buf->data_size;
            moved_total_len += cur_buf->data_size;
            if (last || ++sge_idx >= TX_SGE_MAX || moved_total_len >= UmqSetting::GetIOBufSize()) {
                break;
            }
        }

        tx_total_len += moved_total_len;
        cur_wr_first->total_data_size = moved_total_len;
        umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)cur_wr_first->qbuf_ext;
        buf_pro->opcode = UMQ_OPC_SEND;
        buf_pro->flag.value = 0;
        buf_pro->user_ctx = 0;
        if (tx_queue_avail_num_ == 1 || i + 1 == batch) {
            buf_pro->flag.bs.solicited_enable = 1;
        } else {
            if (unsignaled_wr_num_ > TX_REPORT_THRESHOLD || unsolicited_bytes_ > TX_UNSOLICITED_BYTES_MAX) {
                buf_pro->flag.bs.solicited_enable = 1;
            } else {
                ++unsignaled_wr_num_;
                unsolicited_bytes_ += moved_total_len;
            }
        }

        if (buf_pro->flag.bs.solicited_enable == 1) {
            unsolicited_wr_num_ = 0;
            unsolicited_bytes_ = 0;
        }

        if (++unsignaled_wr_num_ >= TX_REPORT_THRESHOLD) {
            buf_pro->flag.bs.complete_enable = 1;
            buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&head_buf_);
            QBUF_LIST_FIRST(&head_buf_) = QBUF_LIST_NEXT(cur_buf);
            unsignaled_wr_num_ = 0;
        }
    }

    QBUF_LIST_FIRST(&tail_buf_) = cur_buf;

    umq_buf_t *bad_qbuf = nullptr;
    int ret = umq_post(local_umqh_, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
    if (ret == UMQ_SUCCESS) {
        tx_queue_avail_num_ -= batch;
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            //    UpdateTraceStats(StatsMgr::TX_PACKET_COUNT, 1);
        }
    } else if (bad_qbuf != nullptr) {
        // Handle partial failure
        if (ret == -UMQ_ERR_EAGAIN) {
            // Operation would block, UMQ queue might be temporarily full despite window check
            errno = EAGAIN;
            need_fc_awake_.store(true, std::memory_order_relaxed);
        } else if (ret == -UMQ_ERR_EFLOWCTL) {
            errno = EIO;
            return -1;
        } else {
            UBS_VLOG_ERR("umq_post() failed for TX, local umq: %llu, ret: %d\n",
                         static_cast<unsigned long long>(local_umqh_), ret);
            errno = EIO;
            flagEIO = 1;
        }
        umq_buf_list_t head = {bad_qbuf};
        umq_buf_t *cur = nullptr;
        QBUF_LIST_FOR_EACH(cur, &head)
        {
            /* rpc adapter has replace brpc butil::iobuf::blockmem_allocate() &
             * butil::iof::blockmem_deallocate()
             * and ensures that the starting address of the Block is aligned to an 8k boundary. */
            ((Block *)PtrFloorToBoundary(cur->buf_data))->DecRef();
        }

        if (bad_qbuf == tx_buf_list) {
            unsolicited_wr_num_ = _unsolicited_wr_num;
            unsolicited_bytes_ = _unsolicited_bytes;
            unsignaled_wr_num_ = _unsignaled_wr_num;
            QBUF_LIST_FIRST(&head_buf_) = head_qbuf;
            QBUF_LIST_FIRST(&tail_buf_) = tail_qbuf;
            umq_buf_free(bad_qbuf);
            tx_total_len = 0;
        } else {
            tx_total_len = HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf, _unsolicited_wr_num, _unsolicited_bytes,
                                         _unsignaled_wr_num);
        }
        if (flagEIO == 1) {
            UBS_VLOG_ERR("write failed, destroy UB\n");
            return -1;
        }
    } else {
        UBS_VLOG_ERR("umq_post() failed for TX without bad_qbuf, local umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(local_umqh_), ret);
    }

    // After posting and before polling, the time for updating the count cna be concealed within the waiting period
    // for polling.
    if ((GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_) >= TX_HANDLE_THRESHOLD) {
        PollUmqTx(false);
    }
    return tx_total_len;
}

int UmqTxOps::PollTx()
{
    if (get_and_ack_event_) {
        // handle tx epollin epoll event
        do {
            if (GetAndAckEvent(UMQ_IO_TX) < 0) {
                errno = EIO;
                UBS_VLOG_ERR("WriteV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                             Func::Error2Str(errno));
                return -1;
            }
            // set poll_to_empty, means poll at least m_tx.m_retrieve_threshold TX CQE
            PollUmqTx(true);
            /* m_tx.epoll_event_num_ not equals to m_tx.m_expect_epoll_event_num means
             * another epoll event is reportedduring readv processing procedure */
        } while (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                           std::memory_order_acquire));

        get_and_ack_event_ = false;
    } else if (tx_queue_avail_num_ == 0) {
        PollUmqTx(false);
        if (tx_queue_avail_num_ == 0) {
            return DpRearmTxInterrupt();
        }
    }

    return 0;
}

bool UmqTxOps::CutLast(IovConverter cvt, uint32_t len, umq_buf_t *buf)
{
    uint32_t moved_len = 0;
    if (cvt.iov_idx_ < cvt.iovcnt_) {
        if (cvt.iov_offset_ + len >= cvt.iov_[cvt.iov_idx_].iov_len) {
            while (cvt.iov_idx_ < cvt.iovcnt_ && cvt.iov_[cvt.iov_idx_].iov_len == 0) {
                cvt.iov_idx_++;
            }
            if (cvt.iov_idx_ >= cvt.iovcnt_) {
                return moved_len;
            }
            moved_len = cvt.iov_[cvt.iov_idx_].iov_len - cvt.iov_offset_;
            buf->buf_data = (char *)cvt.iov_[cvt.iov_idx_].iov_base + cvt.iov_offset_;
            buf->data_size = moved_len;

            cvt.iov_offset_ = 0;
            /* Avoid core dump caused by brpc passing in memory with a length of 0,
             * directly skip IOVs with a length of 0. */
            do {
                cvt.iov_idx_++;
            } while (cvt.iov_idx_ < cvt.iovcnt_ && cvt.iov_[cvt.iov_idx_].iov_len == 0);
        } else {
            moved_len = len;
            buf->buf_data = (char *)cvt.iov_[cvt.iov_idx_].iov_base + cvt.iov_offset_;
            buf->data_size = moved_len;

            cvt.iov_offset_ += moved_len;
        }
    }

    return cvt.iov_idx_ < cvt.iovcnt_ ? false : true;
}
} // namespace umq
} // namespace ubs
} // namespace ock