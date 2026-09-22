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

#include "umq_tx_helper.h"
#include "common/ubsocket_port_cooldown.h"
#include "core/ubsocket_bigdata.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/tx_stat_block.h"
#include "profiling/trace/ubsocket_trace.h"
#include "umq_errno_converter.h"
#include "umq_qbuf_list.h"
#include "umq_setting.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqTxHelper::PollUmqTxInternal(PollArgs &poll_args, ICallback &error_cb)
{
    Socket *sock_raw = poll_args.sock;
    int raw_socket = poll_args.sock ? poll_args.sock->raw_socket_ : -1;
    umq_buf_t *buf[POLL_BATCH_MAX];
    uint64_t umq_poll_start = ubsocket_get_timeNs_compile();
    PROF_START(UMQ_POLL_WRITE);
    int poll_num = UmqApi::umq_poll(poll_args.umq_handle, &poll_args.poll_option, buf, POLL_BATCH_MAX);
    uint64_t umq_poll_end = ubsocket_get_timeNs_compile();
    if (poll_num > 0 && sock_raw != nullptr) {
        for (int i = 0; i < poll_num; i++) {
            umq_buf_pro_t *buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
            if (buf_pro == nullptr) {
                continue;
            }
            uint32_t seq_no = buf_pro->imm.user_data;
            STRACE_SAMPLED(raw_socket, PATH_TX_WRITEV, TX_WV_ASYNC_UMQ_POLL, seq_no,
                          umq_poll_start, umq_poll_end);
            STRACE_SAMPLED(raw_socket, PATH_TX_POST, TX_POST_ASYNC_UMQ_POLL, seq_no,
                          umq_poll_start, umq_poll_end);
        }
    }
    if (poll_num <= 0) {
        PROF_END(UMQ_POLL_WRITE, false);
        if (poll_args.silent_poll_err && poll_num < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, poll_num, savedErrno);
            // EMLINK indicates no available jetty; the periodic TX poll will handle this case, no error handling is needed here.
            if (errno == EMLINK) {
                UBS_VLOG_DEBUG("[Debug] tx umq_poll() suspended: no available jetty. Queued for automatic retry, "
                               "umq handle: %lu.\n",
                               poll_args.umq_handle);
            } else {
                UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for TX, local umq: %llu, ret: %d, "
                             "mapped errno: %d(%s), original errno: %d\n",
                             static_cast<unsigned long long>(poll_args.umq_handle), poll_num, errno,
                             UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, poll_num), savedErrno);
            }
        }
        return poll_num;
    }
    PROF_END(UMQ_POLL_WRITE, true);

    int wr_cnt = 0;
    int cur_wr_cnt;
    umq_buf_t *first_qbuf = nullptr;
    std::unordered_map<int, int> socket_wr_cnt_map{};
    std::unordered_set<int> rnr_notify_fds{};
    for (int i = 0; i < poll_num; ++i) {
        // bigdata control-SEND / READ completion (additive: returns false for
        // ordinary SEND_IMM traffic so the normal path below is unchanged).
        uint32_t bigdata_span = 0;
        int bigdata_progress_fd = -1;
        if (UbsBigdata::HandleTxCompletion(poll_args.sock, buf[i], &bigdata_span, &bigdata_progress_fd)) {
            wr_cnt += bigdata_span;
            if (bigdata_progress_fd >= 0) {
                socket_wr_cnt_map[bigdata_progress_fd] += bigdata_span;
            }
            continue;
        }
        // RNR 软反压通知：URMA 透传 RNR 但仍在重传 WR，这里不做断链，而是
        // 置位反压状态（抑制 EPOLLOUT / writev 返回 EAGAIN），数据 WR 仍在途。
        if (buf[i] != nullptr && buf[i]->status == UMQ_BUF_RNR_RETRY_CNT_EXC &&
            GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED) {
            HandleRnrNotify(poll_args.sock, buf[i], rnr_notify_fds);
            continue;
        }
        if (buf[i] == nullptr || buf[i]->status != 0 || (((umq_buf_pro_t *)buf[i]->qbuf_ext) == nullptr) ||
            (first_qbuf = (umq_buf_t *)((umq_buf_pro_t *)(buf[i]->qbuf_ext))->user_ctx) == nullptr) {
            // set err_code to true to force a quick exit from current function.
            poll_args.err_code = ops_error_code::NORMAL_ERROR;

            if (buf[i] == nullptr) {
                UBS_VLOG_DEBUG("TX CQE is invalid, umq buffer is empty\n");
                continue;
            }

            if (buf[i]->status != 0) {
                error_cb.invoke(buf[i]);
                HandleTxCqeError(buf[i], wr_cnt, poll_args.sock);
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
        cur_wr_cnt = ProcessTxCqe(first_qbuf, buf[i], poll_args.sock, i == 0);
        if (cur_wr_cnt < 0) {
            // set err_code to true to force a quick exit from current function.
            poll_args.err_code = ops_error_code::FATAL_ERROR;
            return wr_cnt;
        }

        wr_cnt += cur_wr_cnt;

        auto buf_pro = (umq_buf_pro_t *)buf[i]->qbuf_ext;
        int socket_fd = raw_socket >= 0 ? raw_socket : static_cast<int>(buf_pro->umq_ctx);
        if (socket_fd >= 0) {
            socket_wr_cnt_map[socket_fd] += cur_wr_cnt;
        }
    }

    for (const auto &[fd, count] : socket_wr_cnt_map) {
        auto sock = ArraySet<Socket>::GetInstance().GetItem(fd);
        if (sock.Get() == nullptr) {
            UBS_VLOG_DEBUG("Socket %d has been removed.\n", fd);
            continue;
        }

        auto umq_sk = RefStaticCast<UmqSocket>(sock);
        auto tx_ops = umq_sk->GetTxOps();

        if (tx_ops == nullptr) {
            UBS_VLOG_DEBUG("Socket %d tx_ops is null, skipping tx_queue_avail update\n", fd);
            continue;
        }

        // jetty node 被 poll 空，它"可能"会被其他 socket 绑定
        tx_ops->tx_queue_avail_num_.fetch_add(count, std::memory_order_acq_rel);

        // 本批有成功 CQE 且无新的 RNR 反压通知，说明接收端已恢复，解除反压。
        if (rnr_notify_fds.count(fd) == 0 && umq_sk->IsRnrBlocked()) {
            umq_sk->SetRnrBlocked(false);
            umq_sk->OnRnrRecover();
            UBS_VLOG_WARN("RNR backpressure released, socket fd: %d\n", fd);
        }

        // socket 会因为 ENOBUFS 而 EAGAIN、阻塞，这种情况下会被唤醒；但也有可能 socket 只是发送了一些数据未遇
        // 到阻塞，这时的 NotifyWritable() 不会生效，后续 brpc 也不会主动调用 EpollCtlMod 来触发。
        uint64_t notify_start = ubsocket_get_timeNs_compile();
        umq_sk->NotifyWritable();
        uint64_t notify_end = ubsocket_get_timeNs_compile();
        STRACE_SAMPLED(raw_socket, PATH_TX_WRITEV, TX_WV_ASYNC_NOTIFY, 0,
                       notify_start, notify_end);
        STRACE_SAMPLED(raw_socket, PATH_TX_POST, TX_POST_ASYNC_NOTIFY, 0,
                       notify_start, notify_end);
    }

    // 唤醒因为 EMLINK 而被挂起的 socket. jetty node 被 poll 空不代表之前与它绑定的 socket 就不会占用它了，因
    // 此可能会存在多唤醒 socket 的情况。
    const int freed_jettys = std::exchange(poll_args.poll_option.tp_handle_free_num, 0);
    if (freed_jettys > 0) {
        UmqTpWaitQueue::Instance().WakeUp(freed_jettys);
    }

    return wr_cnt;
}

int UmqTxHelper::ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf, Socket *sock, bool is_first_cqe)
{
    int wr_cnt = 0;
    int raw_socket = sock ? sock->raw_socket_ : -1;
    umq_buf_t *cur_qbuf = start_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *wr_first_buf;
    uint64_t decref_start = ubsocket_get_timeNs_compile();
    do {
        wr_first_buf = cur_qbuf;
        int64_t left_size = (int64_t)wr_first_buf->total_data_size;
        while (cur_qbuf != nullptr && left_size > 0) {
            left_size -= cur_qbuf->data_size;
            if (cur_qbuf->is_coalesced_small) {
                /* Coalesced SMALL_DATA: data region is umq-allocated (with_data),
                 * not a brpc Block — no Block to DecRef. umq_buf_free below
                 * reclaims the with_data region. */
                last_qbuf = cur_qbuf;
                cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
                continue;
            }
            Block *block = nullptr;
            if (cur_qbuf->rsvd4 != 0) {
                block = reinterpret_cast<Block *>(cur_qbuf->rsvd4);
                cur_qbuf->rsvd4 = 0;
            } else {
                block = DataToBlock(cur_qbuf->buf_data);
            }
            if (block != nullptr) {
                block->DecRef();
            } else {
                UBS_VLOG_ERR("failed to locate brpc block for TX CQE data %p\n", cur_qbuf->buf_data);
            }
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
        }
        wr_cnt++;
    } while (cur_qbuf != nullptr && wr_first_buf != end_qbuf);

    uint64_t decref_end = ubsocket_get_timeNs_compile();
    if (sock != nullptr && is_first_cqe) {
        auto *buf_pro = (umq_buf_pro_t *)start_qbuf->qbuf_ext;
        if (buf_pro != nullptr) {
            uint32_t seq_no = buf_pro->imm.user_data;
            STRACE_SAMPLED(raw_socket, PATH_TX_WRITEV, TX_WV_ASYNC_PROCESS_CQE, seq_no,
                          decref_start, decref_end);
            STRACE_SAMPLED(raw_socket, PATH_TX_POST, TX_POST_ASYNC_PROCESS_CQE, seq_no,
                          decref_start, decref_end);
        }
    }

    if (wr_first_buf == nullptr) {
        UBS_VLOG_ERR("TX umq buffer list is in error, TX user context does not contain the right list\n");
        return -1;
    }

    // 如果是一个 read OP, 那么它的 left_size=0.
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }

    PROF_START(UMQ_BUF_FREE);
    UmqApi::umq_buf_free(start_qbuf);
    PROF_END(UMQ_BUF_FREE, true);

    if (sock != nullptr && is_first_cqe) {
        auto *buf_pro = (umq_buf_pro_t *)start_qbuf->qbuf_ext;
        if (buf_pro != nullptr) {
            uint32_t seq_no = buf_pro->imm.user_data;
            uint64_t buf_free_end = ubsocket_get_timeNs_compile();
            STRACE_SAMPLED(raw_socket, PATH_TX_WRITEV, TX_WV_ASYNC_BUF_FREE, seq_no,
                          decref_end, buf_free_end);
            STRACE_SAMPLED(raw_socket, PATH_TX_POST, TX_POST_ASYNC_BUF_FREE, seq_no,
                          decref_end, buf_free_end);
        }
    }

    return wr_cnt;
}

void UmqTxHelper::HandleTxCqeError(umq_buf_t *qbuf, int &wr_cnt, Socket *sock)
{
    // 探测包错误处理
    if (HandleProbePacket(qbuf)) {
        return;
    }

    // 正常错误处理流程
    LogTxCqeErrorMsg(qbuf, sock);
    ProcessErrorTxCqe(qbuf);
    wr_cnt++;
}

bool UmqTxHelper::HandleProbePacket(umq_buf_t *qbuf)
{
    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
    if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
        PROF_START(UMQ_BUF_FREE);
        UmqApi::umq_buf_free(qbuf);
        PROF_END(UMQ_BUF_FREE, true);
        return true; // 已处理
    }
    return false; // 不是探测包
}

void UmqTxHelper::LogTxCqeErrorMsg(umq_buf_t *buf, Socket *sock)
{
    auto bufStatus = static_cast<umq_buf_status_t>(buf->status);
    int mappedErrno = UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, bufStatus, errno);
    const char *desc = UmqErrnoConverter::GetBufStatusDescription(UmqOperation::WRITEV, bufStatus);
    UBS_VLOG_ERR("cqe error: buf status %lu, mapped errno: %d, desc: %s\n", buf->status, mappedErrno, desc);

    if (bufStatus == UMQ_BUF_SUCCESS) {
        return;
    }
    ::ock::ubs::txstat::CqeErr cqe_bucket;
    switch (bufStatus) {
        case UMQ_BUF_RNR_RETRY_CNT_EXC_ERR:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_RNR;
            break;
        case UMQ_BUF_ACK_TIMEOUT_ERR:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_ACK_TIMEOUT;
            break;
        case UMQ_FAKE_BUF_FC_ERR:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_FC;
            break;
        case UMQ_BUF_REM_RESP_LEN_ERR:
        case UMQ_BUF_REM_UNSUPPORTED_REQ_ERR:
        case UMQ_BUF_REM_OPERATION_ERR:
        case UMQ_BUF_REM_ACCESS_ABORT_ERR:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_REMOTE;
            break;
        case UMQ_BUF_LOC_LEN_ERR:
        case UMQ_BUF_LOC_OPERATION_ERR:
        case UMQ_BUF_LOC_ACCESS_ERR:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_LOCAL;
            break;
        default:
            cqe_bucket = ::ock::ubs::txstat::CQE_ERR_OTHER;
            break;
    }

    UmqTxOps *tx_ops = nullptr;
    if (sock != nullptr) {
        auto *umq_sock = static_cast<UmqSocket *>(sock);
        tx_ops = umq_sock->GetUmqTxOps();
    }
    UMQ_CQE_ERR_ADD(tx_ops, cqe_bucket);

    switch (buf->status) {
        case UMQ_BUF_SUCCESS:
            return;

        case UMQ_FAKE_BUF_FC_ERR:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: flow control failed\n");
            break;

        case UMQ_FAKE_BUF_FC_ERR_FATAL:
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: flow control fatal failed\n");
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
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: RNR retry exhausted (fatal), status=%lu\n",
                         static_cast<unsigned long>(buf->status));
            if (buf->qbuf_ext != nullptr) {
                auto *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
                int rnr_fd = static_cast<int>(buf_pro->umq_ctx);
                auto rnr_sock = ArraySet<Socket>::GetInstance().GetItem(rnr_fd);
                if (rnr_sock.Get() != nullptr) {
                    static_cast<UmqSocket *>(rnr_sock.Get())->OnRnrTimeout();
                }
            }
            break;

        case UMQ_BUF_RNR_RETRY_CNT_EXC:
            /* URMA 透传的 RNR 软反压信号（status=99）：正常由 PollUmqTxInternal
             * 的软反压拦截处理（HandleRnrNotify，不断链）。仅在
             * UBS_RNR_BACKPRESSURE_ENABLED=false 时落此（旧行为断链），仅打日志。 */
            UBS_VLOG_ERR("[UMQ_CQE] cqe error: RNR soft backpressure signal (backpressure disabled), status=%lu\n",
                         static_cast<unsigned long>(buf->status));
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

        default:
            UBS_VLOG_ERR("[UMQ_CQE] unreachable! status=%d\n", buf->status);
            break;
    }
}

void UmqTxHelper::ProcessErrorTxCqe(umq_buf_t *first_qbuf)
{
    if (first_qbuf->status >= UMQ_FAKE_BUF_FC_UPDATE) {
        UmqApi::umq_buf_free(first_qbuf);
        return;
    }
    umq_buf_t *cur_qbuf = first_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    int64_t left_size = (int64_t)cur_qbuf->total_data_size;
    while (cur_qbuf != nullptr && left_size > 0) {
        left_size -= cur_qbuf->data_size;
        if (cur_qbuf->is_coalesced_small) {
            last_qbuf = cur_qbuf;
            cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
            continue;
        }
        Block *block = nullptr;
        if (cur_qbuf->rsvd4 != 0) {
            block = reinterpret_cast<Block *>(cur_qbuf->rsvd4);
            cur_qbuf->rsvd4 = 0;
        } else {
            block = DataToBlock(cur_qbuf->buf_data);
        }
        if (block != nullptr) {
            block->DecRef();
        } else {
            UBS_VLOG_ERR("failed to locate brpc block for error TX CQE data %p\n", cur_qbuf->buf_data);
        }
        last_qbuf = cur_qbuf;
        cur_qbuf = QBUF_LIST_NEXT(cur_qbuf);
    }
    // 如果是一个 read OP, 那么它的 left_size=0.
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }
    PROF_START(UMQ_BUF_FREE);
    UmqApi::umq_buf_free(first_qbuf);
    PROF_END(UMQ_BUF_FREE, true);
}

void UmqTxHelper::HandleRnrNotify(Socket *sock, umq_buf_t *buf, std::unordered_set<int> &rnr_notify_fds)
{
    Socket *target = sock;
    SocketPtr sock_holder;
    if (target == nullptr && buf != nullptr && buf->qbuf_ext != nullptr) {
        auto *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
        int fd = static_cast<int>(buf_pro->umq_ctx);
        sock_holder = ArraySet<Socket>::GetInstance().GetItem(fd);
        target = sock_holder.Get();
    }

    if (target != nullptr) {
        auto *umq_sk = static_cast<UmqSocket *>(target);
        const bool first_entry = !umq_sk->IsRnrBlocked();
        umq_sk->SetRnrBlocked(true);
        // 清空可写就绪位，避免 EpollCtlMod 期间补发 EPOLLOUT 事件
        umq_sk->ReadyAndExchange();
        rnr_notify_fds.insert(target->raw_socket_);
        // 反压异常事件：首次进入打 WARN 便于跟踪；重传中重复上报 status=10 仅打 DEBUG 防刷屏。
        if (first_entry) {
            umq_sk->OnRnrEnter();
            UBS_VLOG_WARN("RNR backpressure entered, socket fd: %d, status: %lu\n", target->raw_socket_,
                          static_cast<unsigned long>(buf != nullptr ? buf->status : 0));
        } else {
            UBS_VLOG_DEBUG("RNR backpressure notification repeated, socket fd: %d\n", target->raw_socket_);
        }
    }

    // RNR 软反压通知 buffer：数据 WR 仍在途（URMA 持续重传 / bonding 路径切换
    // 可能复用该 userctx 重发），不能提前释放。若此处 umq_buf_free，bonding
    // 重发完成后新 CQE 携带同一已释放 buf 回来 → double free。buf 及 qbuf 链表
    // 保持完整，由最终 CQE（重传成功 status=0，或重试耗尽 status=99）走正常
    // 释放路径处理；若此处切断 QBUF_LIST_NEXT，最终 CQE 遍历链表时会因
    // total_data_size 未归零而误判 FATAL。
}

Block *UmqTxHelper::DataToBlock(void *data)
{
    umq_buf_t *qbuf = UmqApi::umq_data_to_head(data);
    if (qbuf == nullptr || qbuf->buf_data == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Block *>(qbuf->buf_data);
}

int UmqTxHelper::PollUmqTxForFcReturn(uint64_t umq_handle)
{
    umq_io_option_t poll_option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                   UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
    ops_error_code err_code = ops_error_code::OK;
    PollArgs poll_args(umq_handle, poll_option, err_code, nullptr);
    poll_args.silent_poll_err = UmqSetting::UMQ_TP_TYPE == POOL;

    int ret = PollUmqTx(poll_args, [](umq_buf_t *qbuf) {
        auto buf_pro = (umq_buf_pro_t *)qbuf->qbuf_ext;
        auto socket_fd = static_cast<int>(buf_pro->umq_ctx);
        auto sock_ref = ArraySet<Socket>::GetInstance().GetItem(socket_fd);
        auto *socket_ptr = sock_ref.Get();
        if (socket_ptr == nullptr) {
            UBS_VLOG_DEBUG("socket is NULL in socket fd=%d\n in TX CQE error for FC", socket_fd);
            return;
        }

        // 异步关闭. 等待下次 EPOLLIN 事件时关闭.
        // brpc 总是会关注 EPOLLIN 事件, 将读端关闭会产生一次 epoll 事件, 之后 brpc 会尝试从 m_fd 读
        // 取数据, 预期返回 0 表示 EOF. 之后 brpc 会自动处理 socket 的关闭.
        LibcApi::shutdown(socket_fd, SHUT_RD);
        UBS_VLOG_DEBUG("closing socket fd=%d\n in TX CQE error for FC", socket_fd);
        socket_ptr->State(SOCK_STAT_CLOSE);

        // 光组网下，如果出现了异常 CQE 2/4/9 则说明底层 URMA 已将所有 port 都给重试了
        auto *umq_sock = static_cast<UmqSocket *>(socket_ptr);
        if (umq_sock->GetTopoType() == UMQ_TOPO_TYPE_CLOS) {
            if (qbuf->status == UMQ_BUF_LOC_LEN_ERR || qbuf->status == UMQ_BUF_LOC_ACCESS_ERR ||
                qbuf->status == UMQ_BUF_ACK_TIMEOUT_ERR || qbuf->status == UMQ_FAKE_BUF_FC_ERR ||
                qbuf->status == UMQ_FAKE_BUF_FC_ERR_FATAL) {
                auto [ports, ports_num] = umq_sock->GetUsedPorts();
                for (std::size_t i = 0; i < ports_num; ++i) {
                    UBS_VLOG_WARN("port is down, new UB connection will not use port(chip=%u,die=%u,port=%u)\n",
                                  ports[i].bs.chip_id, ports[i].bs.die_id, ports[i].bs.port_idx);
                    PortCooldownManager::MarkPortInCooldown(ports[i]);
                }
            }
        }
    });
    if (poll_args.silent_poll_err && ret < 0) {
        int savedErrno = errno;
        errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, ret, savedErrno);
        if (errno == EMLINK) {
            UBS_VLOG_DEBUG(
                "[Debug] fc tx umq_poll() suspended: no available jetty. Dropped and will retry at next umq scan, "
                "umq handle: %lu.\n",
                umq_handle);
        } else {
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for fc tx, local umq: %llu, ret: %d, "
                         "mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(umq_handle), ret, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, ret), savedErrno);
            return UBS_ERROR;
        }
    }
    return UBS_OK;
}

} // namespace umq
} // namespace ubs
} // namespace ock
