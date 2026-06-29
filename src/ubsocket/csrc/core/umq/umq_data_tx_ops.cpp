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
#include "common/ubsocket_common_includes.h"
#include "umq_buf_converter.h"
#include "umq_errno_converter.h"
#include "umq_socket.h"
#include "umq_tp_wait_queue.h"
#include "umq_tx_helper.h"

namespace ock {
namespace ubs {
namespace umq {

uintptr_t UmqTxOps::AllocTxBuf(uint32_t size, uint32_t count)
{
    umq_buf_t *tx_buf_list = UmqApi::umq_buf_alloc(size, count, UMQ_INVALID_HANDLE, nullptr);
    if (tx_buf_list == nullptr) {
        UBS_VLOG_ERR("[UMQ_API] umq_buf_alloc() failed for TX, local umq: %llu, ret: %p\n",
                     static_cast<unsigned long long>(local_umqh_), tx_buf_list);
        DpRearmTxInterrupt();
    }

    return reinterpret_cast<uintptr_t>(tx_buf_list);
}

int UmqTxOps::PostSend(const SocketPtr &sock, uintptr_t buf, uint32_t batch, const ConverterPtr &cvt)
{
    umq_buf_t *tx_buf_list = reinterpret_cast<umq_buf_t *>(buf);
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    auto *trace = sock->split_trace_;
    int flagEIO = -1;
    umq_buf_t *head_qbuf = QBUF_LIST_FIRST(&head_buf_);
    umq_buf_t *tail_qbuf = QBUF_LIST_FIRST(&tail_buf_);
    uint16_t _unsolicited_wr_num = unsolicited_wr_num_;
    uint32_t _unsolicited_bytes = unsolicited_bytes_;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num_;

    PROF_START(CORE_WRITE_MEM_COPY);
    umq_buf_t *cur_buf = tx_buf_list;
    umq_buf_t *next_buf = cur_buf;
    if (QBUF_LIST_EMPTY(&head_buf_)) {
        QBUF_LIST_FIRST(&head_buf_) = cur_buf;
    } else {
        QBUF_LIST_NEXT(QBUF_LIST_FIRST(&tail_buf_)) = cur_buf;
    }
    uint32_t tx_total_len = 0;
    uint32_t sn_allocated = 0;
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
        buf_pro->opcode = UMQ_OPC_SEND_IMM;
        buf_pro->flag.value = 0;
        buf_pro->user_ctx = 0;
        auto seq_no = umq_socket->FetchAddSeqNum(1);
        buf_pro->imm.user_data = seq_no;
        if (trace != nullptr) {
            if (i == 0) {
                // first write record has no seqno
                TRACE_UPDATE_WRITE_FIRST(trace, BRPC_CLIENT_CALL, seq_no, cur_buf->data_size, tx_total_len);
            } else if (i > batch - 3) {
                TRACE_ADD_WRITE_DETAIL(trace, CORE_WRITE_MEM_COPY, sock->raw_socket_, seq_no, cur_buf->data_size,
                                       tx_total_len);
            }
        }
        ++sn_allocated;

        if (tx_queue_avail_num_.load(std::memory_order_acq_rel) == 1 || i + 1 == batch) {
            buf_pro->flag.bs.solicited_enable = 1;
        } else {
            if (unsolicited_wr_num_ > TX_REPORT_THRESHOLD || unsolicited_bytes_ > TX_UNSOLICITED_BYTES_MAX) {
                buf_pro->flag.bs.solicited_enable = 1;
            } else {
                ++unsolicited_wr_num_;
                unsolicited_bytes_ += moved_total_len;
            }
        }

        if (buf_pro->flag.bs.solicited_enable == 1) {
            unsolicited_wr_num_ = 0;
            unsolicited_bytes_ = 0;
        }

        // bonding 下, complete_enable 必须置为 1, 原判断和阈值失效
        if (++unsignaled_wr_num_ >= TX_REPORT_THRESHOLD) {
            buf_pro->flag.bs.complete_enable = 1;
            buf_pro->user_ctx = (uint64_t)QBUF_LIST_FIRST(&head_buf_);
            QBUF_LIST_FIRST(&head_buf_) = QBUF_LIST_NEXT(cur_buf);
            unsignaled_wr_num_ = 0;
        }
    }

    QBUF_LIST_FIRST(&tail_buf_) = cur_buf;
    PROF_END(CORE_WRITE_MEM_COPY, true);

    // update last seqno type to CORE_WRITE_UMQ_POST, and add the timestamp
    PROF_START(CORE_WRITE_UMQ_POST);
    umq_buf_t *bad_qbuf = nullptr;
    TRACE_UPDATE_WRITE_LAST(trace, CORE_WRITE_UMQ_POST, cur_buf->data_size, tx_total_len);
    umq_io_option_t option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX, UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int ret = UmqApi::umq_post(local_umqh_, tx_buf_list, &option, &bad_qbuf);

    TRACE_UPDATE_WRITE_LAST_END(trace, CORE_WRITE_UMQ_POST);
    if (ret == UMQ_SUCCESS) {
        // 全部 post成功
        tx_queue_avail_num_.fetch_sub(batch, std::memory_order_acq_rel);
        successful_post_count_.fetch_add(batch, std::memory_order_acq_rel);
        PROF_END(CORE_WRITE_UMQ_POST, true);
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
            sockptr->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::TX_PACKET_COUNT, batch);
        }
    } else if (bad_qbuf != nullptr) {
        PROF_END(CORE_WRITE_UMQ_POST, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
        if (errno == EAGAIN) {
            need_fc_awake_.store(true, std::memory_order_relaxed);
        } else if (ret == -UMQ_ERR_EFLOWCTL) {
            errno = EIO;
            return -1;
        } else if (errno == EMLINK) {
            // optimize: [Jetty池化] ENOBUFS是否需要在此线程尝试pollTx释放资源后重试
            UBS_VLOG_DEBUG("[UMQ_API] umq_post() suspended: resource exhausted. Queued for automatic retry.\n");
            UmqTpWaitQueue::Instance().Enqueue(sock);
            errno = EAGAIN;
        } else if (errno == ENOBUFS) {
            // optimize: [Jetty池化] ENOBUFS是否需要在此线程尝试pollTx释放资源后重试
            PollUmqTx(sock, true);
            UmqTpWaitQueue::Instance().Enqueue(sock);
            errno = EAGAIN;
        } else {
            UBS_VLOG_ERR("[UMQ_API] umq_post() failed for TX, local umq: %llu, ret: %d, "
                         "mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(local_umqh_), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
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
            // 全部 post 失败, 恢复状态
            unsolicited_wr_num_ = _unsolicited_wr_num;
            unsolicited_bytes_ = _unsolicited_bytes;
            unsignaled_wr_num_ = _unsignaled_wr_num;
            QBUF_LIST_FIRST(&head_buf_) = head_qbuf;
            QBUF_LIST_FIRST(&tail_buf_) = tail_qbuf;
            UmqApi::umq_buf_free(bad_qbuf);
            tx_total_len = 0;
            umq_socket->FetchSubSeqNum(sn_allocated);
        } else {
            uint32_t buf_num = 0;
            tx_total_len -= HandleBadQBuf(tx_buf_list, bad_qbuf, head_qbuf, batch, _unsolicited_wr_num,
                                          _unsolicited_bytes, _unsignaled_wr_num, &buf_num);
            umq_socket->FetchSubSeqNum(sn_allocated - buf_num);
        }
        if (flagEIO == 1) {
            UBS_VLOG_ERR("write failed, destroy UB\n");
            return -1;
        }
    } else {
        PROF_END(CORE_WRITE_UMQ_POST, false);
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_post() failed for TX without bad_qbuf, "
                     "local umq: %llu, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(local_umqh_), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
    }

    return tx_total_len;
}

int UmqTxOps::PollTx(const SocketPtr &sock)
{
    if (get_and_ack_event_) {
        // handle tx epollin epoll event
        do {
            PROF_START(CORE_WRITE_REARM);
            if (GetAndAckEvent() < 0) {
                PROF_END(CORE_WRITE_REARM, false);
                UBS_VLOG_ERR("WriteV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                             Func::Error2Str(errno));
                return -1;
            }
            PROF_END(CORE_WRITE_REARM, true);
            PROF_START(CORE_WRITE_POLL_TX_FIRST);
            // set poll_to_empty, means poll at least m_tx.m_retrieve_threshold TX CQE
            PollUmqTx(sock, true);
            /* m_tx.epoll_event_num_ not equals to m_tx.m_expect_epoll_event_num means
             * another epoll event is reportedduring readv processing procedure */
            PROF_END(CORE_WRITE_POLL_TX_FIRST, true);
        } while (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                           std::memory_order_acquire));

        get_and_ack_event_ = false;
    } else if (tx_queue_avail_num_.load(std::memory_order_acq_rel) == 0) {
        PROF_START(CORE_WRITE_POLL_TX_SECOND);
        PollUmqTx(sock, false);
        if (tx_queue_avail_num_.load(std::memory_order_acq_rel) == 0) {
            // 暂时禁用，否则会关闭 solicited mode.
            // return DpRearmTxInterrupt();
        }
        PROF_END(CORE_WRITE_POLL_TX_SECOND, true);
    } else {
        // Tx CQE poller 大概率会在这里运行
        PROF_START(CORE_WRITE_POLL_TX_THIRD);
        PollUmqTxOnce(sock);
        PROF_END(CORE_WRITE_POLL_TX_THIRD, true);
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
    int ret = 0;
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int events = UmqApi::umq_get_cq_event(local_umqh_, &option);
    if (events == 0) {
        return 0;
    } else if (events < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, events, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed, local umq: %llu, ret: %d, mapped: %d(%s), original: %d\n",
                     static_cast<unsigned long long>(local_umqh_), events, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, events), savedErrno);
        return -1;
    }
    if ((ack_event_num_ += events) >= 1) {
        UmqApi::umq_ack_interrupt(local_umqh_, ack_event_num_, &option);
        ack_event_num_ = 0;
        // TODO: 返回值判断
        ret = UmqApi::umq_rearm_interrupt(local_umqh_, false, &option);
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
        PROF_START(CORE_WRITE_DO_TX_POLL);
        poll_cnt = DoUmqTxPoll(sock, err_code);
        PROF_END(CORE_WRITE_DO_TX_POLL, poll_cnt >= 0);
        if (poll_cnt < 0) {
            break;
        } else if (poll_cnt == 0) {
            poll_zero_cnt++;
        } else if (poll_zero_cnt != 0) {
            // reset poll_zero_cnt when actually get tx cqe(s)
            poll_zero_cnt = 0;
        }
        poll_total_cnt += (uint32_t)poll_cnt;
    } while ((poll_total_cnt < TX_RETRIEVE_THRESHOLD || (poll_to_empty && poll_cnt > 0)) &&
             poll_zero_cnt < POLL_TX_RETRY_MAX_CNT && err_code == ops_error_code::OK);
    tx_queue_avail_num_.fetch_add(poll_total_cnt, std::memory_order_acq_rel);
    return 0;
}

int UmqTxOps::PollUmqTxOnce(const SocketPtr &sock)
{
    PROF_START(CORE_WRITE_DO_TX_POLL);
    ops_error_code err_code = OK;
    int poll_cnt = DoUmqTxPoll(sock, err_code);
    PROF_END(CORE_WRITE_DO_TX_POLL, poll_cnt >= 0);

    if (poll_cnt > 0) {
        tx_queue_avail_num_.fetch_add(poll_cnt, std::memory_order_acq_rel);
        successful_post_count_.fetch_sub(poll_cnt, std::memory_order_acq_rel);
    }
    return 0;
}

void UmqTxOps::WakeUpTx(Socket *sock)
{
    bool need_fc_awake = need_fc_awake_.exchange(false, std::memory_order_acq_rel);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (need_fc_awake && eventfd_write(sockBase->event_fd_, 1) == -1) {
        UBS_VLOG_ERR("eventfd_write() failed, event fd: %d, raw sock fd %d: errno: %d, errmsg: %s\n",
                     sockBase->event_fd_, sockBase->raw_socket_, errno, Func::Error2Str(errno));
    }
}

bool UmqTxOps::Writable(const SocketPtr &sock)
{
    UmqTpWaitQueue &queue = UmqTpWaitQueue::Instance();
    if (queue.Empty()) {
        return true;
    }
    /**
     * Jetty资源等待队列不为空：
     * 1. 已经在等待队列的连接，继续等待，等待有空闲资源后按照先入先出顺序唤醒
     * 2. 已唤醒的连接，状态为可写，往下执行重试Write
     * 3. 新来的连接，由于已有连接在等待，默认无法获取资源，直接进入就等待队列
     */
    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
    if (umqSock->GetJettyAllocState() == JettyAllocState::WAITING) {
        return false;
    } else if (umqSock->GetJettyAllocState() == JettyAllocState::IDLE) {
        return true;
    } else {
        queue.Enqueue(sock);
        return false;
    }
}

int UmqTxOps::DoUmqTxPoll(const SocketPtr &sock, ops_error_code &err_code)
{
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    return UmqTxHelper::PollUmqTx(
        local_umqh_, poll_option, err_code,
        [this, sock](umq_buf_t *qbuf) {
            // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
            // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
            // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
            LibcApi::shutdown(fd_, SHUT_RD);
            UBS_VLOG_DEBUG("closing socket fd=%d\n in TX CQE error", fd_);
            sock->State(SOCK_STAT_CLOSE);
        },
        sock);
}

int UmqTxOps::DpRearmTxInterrupt()
{
    PROF_START(CORE_WRITE_REARM);
    umq_interrupt_option_t tx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_TX, UMQ_FD_IO};
    int ret = UmqApi::umq_rearm_interrupt(local_umqh_, false, &tx_option);
    if (ret == 0) {
        PROF_END(CORE_WRITE_REARM, true);
        errno = EAGAIN;
        return -1;
    }

    int savedErrno = errno;
    errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
    UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for TX, local umq: %llu, "
                 "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                 static_cast<unsigned long long>(local_umqh_), ret, errno,
                 UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
    PROF_END(CORE_WRITE_REARM, false);
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

uint32_t UmqTxOps::HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf, uint32_t batch,
                                 uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num,
                                 uint32_t *buf_num)
{
    umq_buf_t *cur_qbuf = head_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *head_qbuf_ = last_head_qbuf;
    umq_buf_t *bad_qbuf_ori = bad_qbuf;
    uint32_t wr_cnt = 0;
    uint16_t _unsolicited_wr_num = unsolicited_wr_num;
    uint32_t _unsolicited_bytes = unsolicited_bytes;
    uint16_t _unsignaled_wr_num = unsignaled_wr_num;
    uint32_t total_size = 0;

    while (bad_qbuf != nullptr) {
        cur_qbuf = bad_qbuf;
        total_size += cur_qbuf->data_size;
        bad_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        wr_cnt++;
    }
    wr_cnt = batch - wr_cnt;

    unsolicited_wr_num_ = _unsolicited_wr_num;
    unsolicited_bytes_ = _unsolicited_bytes;
    unsignaled_wr_num_ = _unsignaled_wr_num;
    tx_queue_avail_num_.fetch_sub(wr_cnt, std::memory_order_acq_rel);
    successful_post_count_.fetch_add(wr_cnt, std::memory_order_acq_rel);
    *buf_num = wr_cnt;

    QBUF_LIST_FIRST(&head_buf_) = head_qbuf_;
    if (last_qbuf != nullptr) {
        /* If head set to nullptr, it means no need to cache the posted qbuf list anymore, reset head
             * to nullptr as well */
        QBUF_LIST_FIRST(&tail_buf_) = (head_qbuf_ == nullptr) ? nullptr : last_qbuf;
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }

    UmqApi::umq_buf_free(bad_qbuf_ori);
    return total_size;
}

void UmqTxOps::FlushTx(const SocketPtr &sock, uint32_t timeout_ms)
{
    uint16_t threshold = GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acq_rel);
    if (threshold <= 0) {
        return;
    }

    uint32_t poll_total_cnt = 0;
    int poll_cnt = 0;
    ops_error_code err_code = ops_error_code::OK;
    auto start = std::chrono::high_resolution_clock::now();
    do {
        if (SocketConnHelper::IsTimeout(start, timeout_ms)) {
            /* If a timeout is triggered here, it would indicate a memory leak.
                 * In this case, processing of unsignaled wr should not continue. */
            UBS_VLOG_DEBUG("Flush TX operation exceeded timeout period(%u ms)\n", timeout_ms);
            break;
        }

        poll_cnt = DoUmqTxPoll(sock, err_code);
        if (poll_cnt < 0) {
            break;
        }

        poll_total_cnt += static_cast<uint32_t>(poll_cnt);
    } while (sock->Type() != SocketType::SOCK_TYPE_COUNT && poll_total_cnt < threshold &&
             err_code != ops_error_code::FATAL_ERROR);
    tx_queue_avail_num_.fetch_add(poll_total_cnt, std::memory_order_relaxed);

    if (err_code != ops_error_code::FATAL_ERROR &&
        tx_queue_avail_num_.load(std::memory_order_relaxed) < GlobalSetting::UBS_TX_DEPTH && unsignaled_wr_num_ > 0) {
        uint32_t left_wr_num = GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acq_rel);
        umq_buf_t *cur_qbuf = QBUF_LIST_FIRST(&head_buf_);
        umq_buf_t *last_qbuf = nullptr;
        uint32_t cached_wr_cnt = 0;
        while (cached_wr_cnt < left_wr_num && cur_qbuf != nullptr) {
            /* unsignaled wr list:
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * |  0  |  1  |  2  |  3  |  4  |  5  |  6  | wr idx
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * |  S  |  S  |  S  |  F  |  F  |  F  |  F  | wr status: (1) S:successful; (2) F:Failed
                 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 * Since the successful wr(0~2) did not set the signaled flag, it will not generate a cqe
                 * Therefore, it is necessary to perform a release operation through the cache list.
                 * The unsuccessful(3 ~ 6) wrs have already been released and retried via(by tcp) an
                 * exceptional cqe within the DoUmqTxPoll() operation, so there is no need to handle these wrs
                 * again here. Consequently, only 0 ~ 2 wrs need to be processed. */
            int64_t rest_size = cur_qbuf->total_data_size;
            /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
                * the situation that rest_size would not reduced to zero */
            while (cur_qbuf && rest_size > 0) {
                rest_size -= (int64_t)cur_qbuf->data_size;
                last_qbuf = cur_qbuf;
                ((Block *)PtrFloorToBoundary(cur_qbuf->buf_data))->DecRef();
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            }

            cached_wr_cnt++;
        }

        if (last_qbuf != nullptr) {
            QBUF_LIST_NEXT(last_qbuf) = nullptr;
        }

        UmqApi::umq_buf_free(QBUF_LIST_FIRST(&head_buf_));
        tx_queue_avail_num_.fetch_add(cached_wr_cnt, std::memory_order_acq_rel);
    }

    if (tx_queue_avail_num_.load(std::memory_order_acq_rel) < GlobalSetting::UBS_TX_DEPTH) {
        UBS_VLOG_DEBUG("Failed to flush umq(TX), leak %u wr(s) of buffer\n",
                       GlobalSetting::UBS_TX_DEPTH - tx_queue_avail_num_.load(std::memory_order_acq_rel));
    }
}

} // namespace umq
} // namespace ubs
} // namespace ock
