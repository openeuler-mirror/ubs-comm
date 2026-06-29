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

#include "umq_data_rx_ops.h"
#include "common/ubsocket_common_includes.h"
#include "profiling/probe/probe_manager.h"
#include "umq_errno_converter.h"
#include "umq_socket.h"
#include "umq_tp_wait_queue.h"

namespace ock {
namespace ubs {
namespace umq {
int UmqRxOps::PollRx(const SocketPtr &sock)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR && get_and_ack_event_) {
        PROF_START(CORE_READ_REARM);
        if (GetAndAckEvent() < 0) {
            PROF_END(CORE_READ_REARM, false);
            UBS_VLOG_ERR("ReadV GetAndAckEvent() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return -1;
        }
        PROF_END(CORE_READ_REARM, true);
        get_and_ack_event_ = false;
    }
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    umq_buf_t *buf[POLL_BATCH_MAX];
    int poll_num = 0;
    if (poll_) {
        poll_num = GetQbuf(sock, buf, POLL_BATCH_MAX);
        if (poll_num < 0) {
            UBS_VLOG_ERR("ReadV GetQbuf() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return -1;
        } else if (poll_num == 0) {
            /* might be useful for qps performance by
             * (1) avoid redundant poll operations when handing cache;
             * (2) aggregating RX requests; */
            poll_ = false;
        }
    }

    PROF_START(CORE_READ_HANDLE_BUF);
    uint32_t polled_size = 0;
    auto *trace = sock->split_trace_;
    for (int i = 0; i < poll_num; ++i) {
        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf[i]->qbuf_ext);
        if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
            // 处理探测包
            Statistics::ProbeManager::GetInstance().HandleReceivedPacket(fd_, buf[i]);
            if (QBUF_LIST_NEXT(buf[i]) != nullptr) {
                UBS_VLOG_WARN("probe buf next not null\n");
            }
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }
        // currently, umq over IB return IB cr status directly, successful = 0
        if (buf[i]->status != 0) {
            if (buf[i]->status != UMQ_FAKE_BUF_FC_UPDATE && buf[i]->status != UMQ_FAKE_BUF_FC_MSG) {
                if (buf[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                    flow_control_failed_ = true;
                }
                HandleErrorRxCqe(buf[i]);

                // 异步关闭. 当前处于 readv 中，等到下次 EPOLLIN 事件到来时会触发关闭
                sock->State(SOCK_STAT_CLOSE);
            } else if (buf[i]->status == UMQ_FAKE_BUF_FC_MSG) {
                UmqApi::umq_buf_free(buf[i]);
                if (!PollSubUmqRx(buf, i)) {
                    continue;
                }

                if (buf[i]->status == UMQ_FAKE_BUF_FC_ERR) {
                    flow_control_failed_ = true;
                    HandleErrorRxCqe(buf[i]);
                    UmqApi::umq_buf_free(buf[i]);
                    continue;
                } else if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE) {
                    rx_queue_avail_num_ += 1;
                    // try to wake up tx if necessary
                    /* convert to umq socket */
                    UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);
                    bool need_fc_awake =
                        umqSock->GetTx()->GetTxOps()->need_fc_awake_.exchange(false, std::memory_order_relaxed);
                    if (need_fc_awake && sockBase->NotifyReadable() == -1) {
                        UBS_VLOG_ERR("eventfd_write() failed, errno: %d, errmsg: %s\n", errno, Func::Error2Str(errno));
                    }
                } else if (buf[i]->status == UMQ_FAKE_BUF_FC_EMLINK) {
                    // 流控请求获取jetty资源失败，加入等待队列
                    UBS_VLOG_DEBUG("[UMQ_API] fc post suspended: resource exhausted. Queued for automatic retry.\n");
                    UmqTpWaitQueue::Instance().Enqueue(sock);
                }
            } else {
                UBS_VLOG_WARN("Unknown buffer status: %d", static_cast<int>(buf[i]->status));
            }

            QBUF_LIST_NEXT(buf[i]) = nullptr;
            UmqApi::umq_buf_free(buf[i]);
            continue;
        }
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::RX_PACKET_COUNT, 1);
        }
        block_cache_.Insert((char *)(buf[i]->buf_data), buf[i]->data_size);
        polled_size += buf[i]->data_size;
        // due to the flowcontrl buf, real buf may start with i = 1
        if (i == 0 || i == 1 || i == poll_num - 2 || i == poll_num - 1) {
            TRACE_ADD_READ_DETAIL(trace, CORE_READ_HANDLE_BUF, sock->raw_socket_,
                                  static_cast<uint32_t>(buf_pro->imm.user_data), buf[i]->data_size, polled_size);
        }
    }
    PROF_END(CORE_READ_HANDLE_BUF, true);
    return 0;
}

void *UmqRxOps::PtrFloorToBoundary(void *ptr)
{
    return (void *)((uint64_t)ptr & ~UmqSetting::FloorMask());
}

int UmqRxOps::GetQbuf(const SocketPtr &sock, umq_buf_t **buf, int max_num)
{
    if (!GlobalSetting::UBS_ENABLE_SHARE_JFR) {
        return UmqPollAndRefillRx(buf, max_num);
    }
    auto umqSock = dynamic_cast<UmqSocket *>(sock.Get());
    int poll_num = umqSock->GetAndPopQbuf(buf, max_num);
    if (poll_num < 0) {
        UBS_VLOG_ERR("GetQbuf failed, fd: %d, ret: %d\n", fd_, poll_num);
        return -1;
    }
    return poll_num;
}

int UmqRxOps::UmqPollAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size)
{
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int poll_num = UmqApi::umq_poll(local_umqh_, &poll_option, buf, max_buf_size);
    if (poll_num < 0 || (poll_num == 0 && rx_queue_avail_num_ == 0)) {
        if (poll_num < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::READV, poll_num, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed, local umq: %llu, ret: %d, mapped: %d(%s), original: %d\n",
                         static_cast<unsigned long long>(local_umqh_), poll_num, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, poll_num), savedErrno);
        }
        return -1;
    }
    rx_queue_avail_num_ -= static_cast<uint16_t>(poll_num);
    if (static_cast<uint16_t>(GlobalSetting::UBS_RX_DEPTH - rx_queue_avail_num_) > TX_REFILL_THRESHOLD) {
        umq_alloc_option_t option = {UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, sizeof(Block)};
        umq_buf_t *rx_buf_list =
            UmqApi::umq_buf_alloc(UmqSetting::GetIOBufSize(), TX_REFILL_THRESHOLD, UMQ_INVALID_HANDLE, &option);
        /* do nothing when failure occurs during refilling RX,
             * try to switch to tcp/ip until poll_num & m_rx.m_window_size both equal to zero */
        if (rx_buf_list != nullptr) {
            umq_buf_t *bad_qbuf = nullptr;
            umq_io_option_t io_rx_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                            UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
            int umq_ret = UmqApi::umq_post(local_umqh_, rx_buf_list, &io_rx_option, &bad_qbuf);
            if (umq_ret == UMQ_SUCCESS) {
                rx_queue_avail_num_ += TX_REFILL_THRESHOLD;
            } else if ((rx_queue_avail_num_ += HandleBadQBuf(rx_buf_list, bad_qbuf)) == 0) {
                int savedErrno = errno;
                errno = UmqErrnoConverter::Convert(UmqOperation::READV, umq_ret, savedErrno);
                UBS_VLOG_ERR("[UMQ_API] umq_post() prefill failed, ret: %d, mapped errno: %d(%s), original errno: %d\n",
                             umq_ret, errno, UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, umq_ret),
                             savedErrno);
                return -1;
            }
        }
    }
    return poll_num;
}

uint32_t UmqRxOps::HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf)
{
    umq_buf_t *cur_qbuf = head_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    uint32_t wr_cnt = 0;
    while (cur_qbuf != bad_qbuf) {
        int64_t rest_size = cur_qbuf->total_data_size;
        /* WriteV ensure total_data_size equals to the sum of all data_size, thus, do not consider
             * the situation that rest_size would not reduced to zero */
        while (cur_qbuf && rest_size > 0) {
            rest_size -= (int64_t)cur_qbuf->data_size;
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        }
        wr_cnt++;
    }
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }
    UmqApi::umq_buf_free(bad_qbuf);
    return wr_cnt;
}

int UmqRxOps::GetAndAckEvent()
{
    umq_interrupt_option_t option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO};
    int events = UmqApi::umq_get_cq_event(local_umqh_, &option);
    if (events == 0) {
        return 0;
    } else if (events < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, events, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_get_cq_event() failed, local umq: %llu, ret: %d, mapped: %d(%s), original: %d\n",
                     static_cast<unsigned long long>(local_umqh_), events, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, events), savedErrno);
        return -1;
    }
    if ((ack_event_num_ += events) >= GET_PER_ACK) {
        UmqApi::umq_ack_interrupt(local_umqh_, ack_event_num_, &option);
        ack_event_num_ = 0;
    }
    return 0;
}

void UmqRxOps::HandleErrorRxCqe(umq_buf_t *buf)
{
    auto bufStatus = static_cast<umq_buf_status_t>(buf->status);
    int mappedErrno = UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, bufStatus, errno);
    const char *desc = UmqErrnoConverter::GetBufStatusDescription(UmqOperation::READV, bufStatus);
    UBS_VLOG_ERR("cqe error: buf status %lu, mapped errno: %d, desc: %s\n", buf->status, mappedErrno, desc);

    switch (buf->status) {
        case UMQ_BUF_SUCCESS:
            return;

        case UMQ_FAKE_BUF_FC_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: flow control failed\n");
            break;

        case UMQ_BUF_UNSUPPORTED_OPCODE_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: unsupported opcode\n");
            break;

        case UMQ_BUF_LOC_LEN_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: local length too long\n");
            break;

        case UMQ_BUF_LOC_OPERATION_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: local op err\n");
            break;

        case UMQ_BUF_LOC_ACCESS_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: access to local memory error\n");
            break;

        case UMQ_BUF_REM_RESP_LEN_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote rx buffer length error\n");
            break;

        case UMQ_BUF_REM_UNSUPPORTED_REQ_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote does not support req\n");
            break;

        case UMQ_BUF_REM_OPERATION_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote jetty can not complete op\n");
            break;

        case UMQ_BUF_REM_ACCESS_ABORT_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote jetty access memory error\n");
            break;

        case UMQ_BUF_ACK_TIMEOUT_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote jetty does not send ack\n");
            break;

        case UMQ_BUF_RNR_RETRY_CNT_EXC_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: remote jetty has no enough RQE\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR:
            break;

        case UMQ_BUF_WR_SUSPEND_DONE:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: suspend done\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR_DONE:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: flush err done\n");
            break;

        case UMQ_BUF_WR_UNHANDLED:
            // See umq_ub_flush_seq
            UBS_VLOG_ERR("[UMQ_CQE] It wont be here.\n");
            break;

        case UMQ_BUF_LOC_DATA_POISON:
        case UMQ_BUF_REM_DATA_POISON:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: not supported yet\n");
            break;

        case UMQ_FAKE_BUF_FC_UPDATE:
            UBS_VLOG_ERR("[UMQ_CQE] You should handle flow control message manually\n");
            break;

        case UMQ_MEMPOOL_UPDATE_SUCCESS:
        case UMQ_MEMPOOL_UPDATE_FAILED:
            UBS_VLOG_ERR("[UMQ_CQE] Something went wrong. brpc-adaptor ONLY uses UB send/recv\n");
            break;

        default:
            UBS_VLOG_ERR("[UMQ_CQE] unreachable! status=%d\n", buf->status);
            break;
    }
    // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
    // TODO: 快速退出, 如果 brpc-adapter 正好在 readv/writev 中可以不经过一次 epoll_wait.
    // m_closed.store(true, std::memory_order_relaxed);

    // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
    // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
    LibcApi::shutdown(fd_, SHUT_RD);
    UBS_VLOG_DEBUG("closing socket fd=%d in RX CQE error\n", fd_);
}

int UmqRxOps::RearmRxInterrupt()
{
    if (UmqSetting::UMQ_TP_TYPE == POOL) {
        return UBS_OK;
    }
    PROF_START(CORE_READ_REARM);
    umq_interrupt_option_t rx_option = {UMQ_INTERRUPT_FLAG_IO_DIRECTION, UMQ_IO_RX, UMQ_FD_IO,
                                        UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int ret = UmqApi::umq_rearm_interrupt(local_umqh_, false, &rx_option);
    if (ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::READV, ret, savedErrno);
        UBS_VLOG_ERR("[UMQ_API] umq_rearm_interrupt() failed for RX, local umq: %llu, "
                     "ret: %d, mapped errno: %d(%s), original errno: %d\n",
                     static_cast<unsigned long long>(local_umqh_), ret, errno,
                     UmqErrnoConverter::GetErrorDescription(UmqOperation::READV, ret), savedErrno);
    }
    PROF_END(CORE_READ_REARM, ret >= 0);
    return ret;
}

bool UmqRxOps::PollSubUmqRx(umq_buf_t *buf[], int i) const
{
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    int ret = UmqApi::umq_poll(local_umqh_, &poll_option, &buf[i], 1);
    bool pollRxSuccess = ret > 0;
    if (ret < 0) {
        UBS_VLOG_ERR("Failed to poll fc rx, local umq: %llu, ret: %d\n", static_cast<unsigned long long>(local_umqh_),
                     ret);
    }
    return pollRxSuccess;
}

void UmqRxOps::FlushRx(const SocketPtr &sock, uint32_t timeout_ms)
{
    block_cache_.Flush();
    if (rx_queue_avail_num_ <= 0) {
        return;
    }
    auto umq_socket = RefConvert<Socket, UmqSocket>(sock);
    umq_buf_t *buf[POLL_BATCH_MAX];
    uint32_t poll_total_cnt = 0;
    int poll_cnt = 0;
    auto start = std::chrono::high_resolution_clock::now();
    do {
        if (SocketConnHelper::IsTimeout(start, timeout_ms)) {
            UBS_VLOG_DEBUG("Flush RX operation exceeded timeout period(%u ms)\n", timeout_ms);
            break;
        }

        umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_RX,
                                       UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
        poll_cnt = UmqApi::umq_poll(local_umqh_, &poll_option, buf, POLL_BATCH_MAX);
        if (poll_cnt < 0) {
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for RX flush, local umq: %llu, ret: %d\n",
                         static_cast<unsigned long long>(local_umqh_), poll_cnt);
            break;
        }

        for (int i = 0; i < poll_cnt; i++) {
            if (buf[i]->status == UMQ_FAKE_BUF_FC_UPDATE) {
                if (umq_socket->NotifyReadable() == -1) {
                    UBS_VLOG_ERR("eventfd_write() failed, event fd: %d, errno: %d, errmsg: %s\n", umq_socket->event_fd_,
                                 errno, Func::Error2Str(errno));
                }
            }
            UmqApi::umq_buf_free(buf[i]);
        }

        poll_total_cnt += static_cast<uint32_t>(poll_cnt);
    } while (sock->Type() != SocketType::SOCK_TYPE_COUNT && poll_total_cnt < rx_queue_avail_num_);

    if ((rx_queue_avail_num_ -= poll_total_cnt) > 0) {
        UBS_VLOG_DEBUG("Failed to flush umq(RX), leak %u piece(s) of buffer\n", rx_queue_avail_num_);
    }
}

} // namespace umq
} // namespace ubs
} // namespace ock