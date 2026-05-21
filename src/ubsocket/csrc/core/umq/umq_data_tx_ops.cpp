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
#include "core/ubsocket_socket_set.h"
#include "umq_buf_converter.h"

namespace ock {
namespace ubs {
namespace umq {

uintptr_t UmqTxOps::AllocTxBuf(uint32_t size, uint32_t count)
{
    umq_buf_t *tx_buf_list = UmqApi::umq_buf_alloc(size, count, UMQ_INVALID_HANDLE, nullptr);
    if (tx_buf_list == nullptr) {
        UBS_VLOG_ERR("umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                     static_cast<unsigned long long>(local_umqh_), tx_buf_list);
        return DpRearmTxInterrupt();
    }

    return reinterpret_cast<uintptr_t>(tx_buf_list);
}

int UmqTxOps::PostSend(const SocketPtr &sock, uintptr_t buf, uint32_t batch, const ConverterPtr &cvt)
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
    cvt->Reset();
    for (uint32_t i = 0; i < batch; ++i) {
        umq_buf_t *cur_wr_first = next_buf;
        uint32_t moved_total_len = 0;
        uint32_t wr_left_len = UmqSetting::GetIOBufSize();
        uint32_t sge_idx = 0;
        bool last = false;
        for (cur_buf = cur_wr_first; cur_buf && (next_buf = cur_buf->qbuf_next, 1); cur_buf = next_buf) {
            last = cvt->MemCopy(wr_left_len, reinterpret_cast<uintptr_t>(cur_buf));
            cur_buf->io_direction = UMQ_IO_TX;
            /* rpc adapter has replace brpc iobuf::blockmem_allocate() & iobuf::blockmem_deallocate()
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
    int ret = UmqApi::umq_post(local_umqh_, tx_buf_list, UMQ_IO_TX, &bad_qbuf);
    if (ret == UMQ_SUCCESS) {
        tx_queue_avail_num_ -= batch;
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            //UpdateTraceStats(StatsMgr::TX_PACKET_COUNT, 1);
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
            UmqApi::umq_buf_free(bad_qbuf);
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
        PollUmqTx(sock, false);
    }
    return tx_total_len;
}

int UmqTxOps::PollTx(const SocketPtr &sock)
{
    if (get_and_ack_event_) {
        // handle tx epollin epoll event
        do {
            if (GetAndAckEvent() < 0) {
                errno = EIO;
                UBS_VLOG_ERR("WriteV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                             Func::Error2Str(errno));
                return -1;
            }
            // set poll_to_empty, means poll at least m_tx.m_retrieve_threshold TX CQE
            PollUmqTx(sock, true);
            /* m_tx.epoll_event_num_ not equals to m_tx.m_expect_epoll_event_num means
             * another epoll event is reportedduring readv processing procedure */
        } while (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                           std::memory_order_acquire));

        get_and_ack_event_ = false;
    } else if (tx_queue_avail_num_ == 0) {
        PollUmqTx(sock, false);
        if (tx_queue_avail_num_ == 0) {
            return DpRearmTxInterrupt();
        }
    }

    return 0;
}

ConverterPtr UmqTxOps::BuildIovConverter(const struct iovec *iov, int iovcnt)
{
    auto umqConverter = MakeRef<UmqIovConverter>(iov, iovcnt);
    return RefConvert<UmqIovConverter, UbSocketBufConverter>(umqConverter);
}

ConverterPtr UmqTxOps::BuildBufferConverter(const void *buf, size_t size)
{
    auto umqConverter = MakeRef<UmqBufferConverter>(buf, size);
    return RefConvert<UmqBufferConverter, UbSocketBufConverter>(umqConverter);
}

int UmqTxOps::GetAndAckEvent()
{
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int events = UmqApi::umq_get_cq_event(local_umqh_, &option);
    if (events == 0) {
        return 0;
    } else if (events < 0) {
        UBS_VLOG_ERR("umq_get_cq_event() failed, local umq: %llu, ret: %d\n",
                     static_cast<unsigned long long>(local_umqh_), events);
        return -1;
    }
    if ((ack_event_num_ += events) >= GET_PER_ACK) {
        UmqApi::umq_ack_interrupt(local_umqh_, ack_event_num_, &option);
        ack_event_num_ = 0;
    }
    return 0;
}

int UmqTxOps::PollUmqTx(const SocketPtr &sock, bool poll_to_empty)
{
    uint32_t poll_total_cnt = 0;
    int poll_cnt = 0;
    uint32_t poll_zero_cnt = 0;
    ops_error_code err_code = ops_error_code::OK;
    do {
        poll_cnt = DoUmqTxPoll(sock, err_code);
        if (poll_cnt < 0) {
            break;
        } else if (poll_cnt == 0) {
            poll_zero_cnt++;
        } else if (poll_zero_cnt != 0) {
            // reset poll_zero_cnt when actually get tx cqe(s)
            poll_zero_cnt = 0;
        }
        poll_total_cnt += (uint32_t)poll_cnt;
    } while ((poll_total_cnt < TX_RETRIEVE_THRESHOLD  || (poll_to_empty && poll_cnt > 0)) &&
             poll_zero_cnt < POLL_TX_RETRY_MAX_CNT && err_code == ops_error_code::OK);
    tx_queue_avail_num_ += poll_total_cnt;
    return 0;
}

int UmqTxOps::DoUmqTxPoll(const SocketPtr &sock, ops_error_code &err_code)
{
    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = UmqApi::umq_poll(local_umqh_, UMQ_IO_TX, buf, POLL_BATCH_MAX);
    if (poll_num <= 0) {
        if (poll_num < 0) {
            UBS_VLOG_ERR("umq_poll() failed for TX, local umq: %llu, ret: %d\n",
                         static_cast<unsigned long long>(local_umqh_), poll_num);
        }
        return poll_num;
    }

    int wr_cnt = 0;
    int cur_wr_cnt;
    umq_buf_t *first_qbuf = nullptr;
    for (int i = 0; i < poll_num; ++i) {
        if (buf[i] == nullptr || buf[i]->status != 0 ||
            (first_qbuf = (umq_buf_t *)((umq_buf_pro_t *)(buf[i]->qbuf_ext))->user_ctx) == nullptr) {
            // set err_code to true to force a quick exit from current function.
            err_code = ops_error_code::NORMAL_ERROR;

            if (buf[i] == nullptr) {
                UBS_VLOG_DEBUG("TX CQE is invalid, umq buffer is empty\n");
                continue;
            }

            if (buf[i]->status != 0) {
                HandleTxCqeError(buf[i], wr_cnt);
                // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
                sock->State(SOCK_STAT_CLOSE);
                continue;
            }

            UBS_VLOG_DEBUG("TX CQE is invalid, status: %d%s\n", buf[i]->status,
                           first_qbuf == nullptr ? ", and umq buffer list is empty" : "");
            continue;
        }
        // 探测包
        if (HandleProbePacket(buf[i])) {
            continue;
        }
        // 正常业务包
        cur_wr_cnt = ProcessTxCqe(first_qbuf, buf[i]);
        if (cur_wr_cnt < 0) {
            // set err_code to true to force a quick exit from current function.
            err_code = ops_error_code::FATAL_ERROR;
            return wr_cnt;
        }

        wr_cnt += cur_wr_cnt;
    }

    return wr_cnt;
}

void UmqTxOps::HandleTxCqeError(umq_buf_t *qbuf, int &wr_cnt)
{
    // 探测包错误处理
    if (HandleProbePacket(qbuf)) {
        return;
    }

    // 正常错误处理流程
    HandleErrorTxCqe(qbuf);
    ProcessErrorTxCqe(qbuf);
    wr_cnt++;

    if (GlobalSetting::UBS_TRACE_ENABLED) {
        if (qbuf->status == UMQ_BUF_ACK_TIMEOUT_ERR) {
            //UpdateTraceStats(StatsMgr::TX_LOST_PACKET_COUNT, 1);
        } else {
            //UpdateTraceStats(StatsMgr::TX_ERROR_PACKET_COUNT, 1);
        }
    }
}

bool UmqTxOps::HandleProbePacket(umq_buf_t *qbuf)
{
    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
    if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == 1) {
        UmqApi::umq_buf_free(qbuf);
        return true; // 已处理
    }
    return false; // 不是探测包
}

void UmqTxOps::HandleErrorTxCqe(umq_buf_t *buf)
{
    switch (buf->status) {
        case UMQ_BUF_SUCCESS:
            return;

        case UMQ_FAKE_BUF_FC_ERR:
            UBS_VLOG_ERR("cqe error: flow control failed\n");
            break;

        case UMQ_BUF_UNSUPPORTED_OPCODE_ERR:
            UBS_VLOG_ERR("cqe error: unsupported opcode\n");
            break;

        case UMQ_BUF_LOC_LEN_ERR:
            UBS_VLOG_ERR("cqe error: local length too long\n");
            break;

        case UMQ_BUF_LOC_OPERATION_ERR:
            UBS_VLOG_ERR("cqe error: local op err\n");
            break;

        case UMQ_BUF_LOC_ACCESS_ERR:
            UBS_VLOG_ERR("cqe error: access to local memory error\n");
            break;

        case UMQ_BUF_REM_RESP_LEN_ERR:
            UBS_VLOG_ERR("cqe error: remote rx buffer length error\n");
            break;

        case UMQ_BUF_REM_UNSUPPORTED_REQ_ERR:
            UBS_VLOG_ERR("cqe error: remote does not support req\n");
            break;

        case UMQ_BUF_REM_OPERATION_ERR:
            UBS_VLOG_ERR("cqe error: remote jetty can not complete op\n");
            break;

        case UMQ_BUF_REM_ACCESS_ABORT_ERR:
            UBS_VLOG_ERR("cqe error: remote jetty access memory error\n");
            break;

        case UMQ_BUF_ACK_TIMEOUT_ERR:
            UBS_VLOG_ERR("cqe error: remote jetty does not send ack\n");
            break;

        case UMQ_BUF_RNR_RETRY_CNT_EXC_ERR:
            UBS_VLOG_ERR("cqe error: remote jetty has no enough RQE\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR:
            break;

        case UMQ_BUF_WR_SUSPEND_DONE:
            UBS_VLOG_ERR("cqe error: suspend done\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR_DONE:
            UBS_VLOG_ERR("cqe error: flush err done\n");
            break;

        case UMQ_BUF_WR_UNHANDLED:
            // See umq_ub_flush_seq
            UBS_VLOG_ERR("It wont be here.\n");
            break;

        case UMQ_BUF_LOC_DATA_POISON:
        case UMQ_BUF_REM_DATA_POISON:
            UBS_VLOG_ERR("cqe error: not supported yet\n");
            break;

        default:
            UBS_VLOG_ERR("unreachable! status=%d\n", buf->status);
            break;
    }

}

void UmqTxOps::ProcessErrorTxCqe(umq_buf_t *first_qbuf)
{
    umq_buf_t *cur_qbuf = first_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    int64_t left_size = (int64_t)cur_qbuf->total_data_size;
    while (cur_qbuf != nullptr && left_size > 0) {
        left_size -= cur_qbuf->data_size;
        /* rpc adapter has replace brpc butil::iobuf::blockmeme_allocate() &
            * butil::iof::blockmem_deallocate() and ensures that the starting address
            * of the Block is aligned to an 8k boundary. */
        ((Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
        last_qbuf = cur_qbuf;
        cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
    }
    // 如果是一个 read OP, 那么它的 left_size=0.
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }
    UmqApi::umq_buf_free(first_qbuf);
}

int UmqTxOps::ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf)
{
    int wr_cnt = 0;
    umq_buf_t *cur_qbuf = start_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *wr_first_buf;
    do {
        wr_first_buf = cur_qbuf;
        int64_t left_size = (int64_t)wr_first_buf->total_data_size;
        while (cur_qbuf != nullptr && left_size > 0) {
            left_size -= cur_qbuf->data_size;
            /* rpc adapter has replace brpc butil::iobuf::blockmeme_allocate() &
                * butil::iof::blockmem_deallocate() and ensures that the starting address
                * of the Block is aligned to an 8k boundary. */
            ((Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        }
        wr_cnt++;
    } while (cur_qbuf != nullptr && wr_first_buf != end_qbuf);

    if (wr_first_buf == nullptr) {
        UBS_VLOG_ERR("TX umq buffer list is in error, TX user context does not contain the right list\n");
        return -1;
    }

    // 如果是一个 read OP, 那么它的 left_size=0.
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }
    UmqApi::umq_buf_free(start_qbuf);

    return wr_cnt;
}

int UmqTxOps::DpRearmTxInterrupt()
{
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int ret = UmqApi::umq_rearm_interrupt(local_umqh_, false, &tx_option);
    if (ret == 0) {
        errno = EAGAIN;
        return -1;
    }

    // 1. try to switch to tcp/ip
    // 2. use EIO for now
    UBS_VLOG_ERR("umq_rearm_interrupt() failed for TX, local umq: %llu, ret: %d\n",
                 static_cast<unsigned long long>(local_umqh_), ret);
    errno = EIO;
    return -1;
}

void *UmqTxOps::PtrFloorToBoundary(void *ptr)
{
    return (void *)((uint64_t)ptr & ~UmqSetting::FloorMask());
}

inline uint32_t UmqTxOps::IOBufSize()
{
    return UmqSetting::GetIOBufSize();
}

uint32_t UmqTxOps::HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf,
                                 uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num)
{
    umq_buf_t *cur_qbuf = head_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *head_qbuf_ = last_head_qbuf;
    uint32_t wr_cnt = 0;
    uint16_t _unsolicited_wr_num = unsolicited_wr_num;
    uint32_t _unsolicited_bytes = unsolicited_bytes;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num;
    uint32_t total_size = 0;

    while (cur_qbuf != bad_qbuf) {
        int64_t rest_size = cur_qbuf->total_data_size;
        umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)cur_qbuf->qbuf_ext;
        if (buf_pro->flag.bs.solicited_enable == 1) {
            _unsolicited_wr_num = 0;
            _unsolicited_bytes = 0;
        } else {
            _unsolicited_wr_num++;
            _unsolicited_bytes += cur_qbuf->total_data_size;
        }

        if (buf_pro->flag.bs.complete_enable == 1) {
            _unsignaled_wr_num = 0;
        } else {
            _unsignaled_wr_num++;
        }

        total_size += cur_qbuf->total_data_size;

        /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
             * the situation that rest_size would not reduced to zero */
        while (cur_qbuf && rest_size > 0) {
            rest_size -= (int64_t)cur_qbuf->data_size;
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        }

        if (buf_pro->flag.bs.complete_enable == 1) {
            /* If the last successfully posted wr has 'complete_enable' set, it means no need to cache
                 * the posted qbuf list anymore, then reset head to nullptr */
            head_qbuf_ = (cur_qbuf != bad_qbuf) ? cur_qbuf : nullptr;
        }

        wr_cnt++;
    }

    unsolicited_wr_num_ = _unsolicited_wr_num;
    unsolicited_bytes_ = _unsolicited_bytes;
    unsignaled_wr_num_ = _unsignaled_wr_num;
    tx_queue_avail_num_ -= wr_cnt;

    QBUF_LIST_FIRST(&head_buf_) = head_qbuf_;
    if (last_qbuf != nullptr) {
        /* If head set to nullptr, it means no need to cache the posted qbuf list anymore, reset head
             * to nullptr as well */
        QBUF_LIST_FIRST(&tail_buf_) = (head_qbuf_ == nullptr) ? nullptr : last_qbuf;
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }

    UmqApi::umq_buf_free(bad_qbuf);
    return total_size;
}
} // namespace umq
} // namespace ubs
} // namespace ock