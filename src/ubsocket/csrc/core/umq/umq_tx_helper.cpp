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
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/trace/ubsocket_trace.h"
#include "umq_errno_converter.h"
#include "umq_qbuf_list.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqTxHelper::PollUmqTxInternal(uint64_t umq_handle, umq_io_option_t &poll_option, ops_error_code &err_code,
                                   ICallback &error_cb, const SocketPtr &sock)
{
    auto *trace = (sock.Get() && !SplitTrace::SuppressTrace()) ? sock->split_trace_ : nullptr;
    int raw_socket = sock.Get() ? sock->raw_socket_ : -1;
    umq_buf_t *buf[POLL_BATCH_MAX];
    uint64_t umq_poll_start = 0;
    if (trace != nullptr) {
        umq_poll_start = ubsocket_get_timeNs();
    }
    int poll_num = UmqApi::umq_poll(umq_handle, &poll_option, buf, POLL_BATCH_MAX);
    if (trace != nullptr) {
        uint64_t umq_poll_end = ubsocket_get_timeNs();
        trace->AddWriteTrace(CORE_WRITE_UMQ_POLL, raw_socket, umq_poll_start, umq_poll_end,
                             static_cast<uint32_t>(poll_num));
    }
    if (poll_num <= 0) {
        if (poll_num < 0) {
            int savedErrno = errno;
            errno = UmqErrnoConverter::Convert(UmqOperation::WRITEV, poll_num, savedErrno);
            UBS_VLOG_ERR("[UMQ_API] umq_poll() failed for TX, local umq: %llu, ret: %d, "
                         "mapped errno: %d(%s), original errno: %d\n",
                         static_cast<unsigned long long>(umq_handle), poll_num, errno,
                         UmqErrnoConverter::GetErrorDescription(UmqOperation::WRITEV, poll_num), savedErrno);
        }
        return poll_num;
    }

    int wr_cnt = 0;
    int cur_wr_cnt;
    umq_buf_t *first_qbuf = nullptr;
    uint64_t cqe_start = 0;
    if (trace != nullptr) {
        cqe_start = ubsocket_get_timeNs();
    }
    for (int i = 0; i < poll_num; ++i) {
        if (buf[i] == nullptr || buf[i]->status != 0 || (((umq_buf_pro_t *)buf[i]->qbuf_ext) == nullptr) ||
            (first_qbuf = (umq_buf_t *)((umq_buf_pro_t *)(buf[i]->qbuf_ext))->user_ctx) == nullptr) {
            // set err_code to true to force a quick exit from current function.
            err_code = ops_error_code::NORMAL_ERROR;

            if (buf[i] == nullptr) {
                UBS_VLOG_DEBUG("TX CQE is invalid, umq buffer is empty\n");
                continue;
            }

            if (buf[i]->status != 0) {
                HandleTxCqeError(buf[i], wr_cnt);
                error_cb.invoke(buf[i]);
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
        cur_wr_cnt = ProcessTxCqe(first_qbuf, buf[i], sock, i == 0);
        if (cur_wr_cnt < 0) {
            // set err_code to true to force a quick exit from current function.
            err_code = ops_error_code::FATAL_ERROR;
            return wr_cnt;
        }

        wr_cnt += cur_wr_cnt;
    }

    if (trace != nullptr) {
        uint64_t cqe_end = ubsocket_get_timeNs();
        trace->AddWriteTrace(CORE_WRITE_POLL_CQE, raw_socket, cqe_start, cqe_end);
    }
    return wr_cnt;
}

int UmqTxHelper::ProcessTxCqe(umq_buf_t *start_qbuf, umq_buf_t *end_qbuf, const SocketPtr &sock, bool is_first_cqe)
{
    int wr_cnt = 0;
    umq_buf_t *cur_qbuf = start_qbuf;
    umq_buf_t *last_qbuf = nullptr;
    umq_buf_t *wr_first_buf;
    uint64_t decref_start = 0;
    auto *trace = (sock.Get() && !SplitTrace::SuppressTrace()) ? sock->split_trace_ : nullptr;
    int raw_socket = sock.Get() ? sock->raw_socket_ : -1;
    bool do_trace = (trace != nullptr && is_first_cqe);
    if (do_trace) {
        decref_start = ubsocket_get_timeNs();
    }
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

    if (do_trace) {
        uint64_t decref_end = ubsocket_get_timeNs();
        trace->AddWriteTrace(CORE_WRITE_POLL_CQE_DECREF, raw_socket, decref_start, decref_end);
    }

    if (wr_first_buf == nullptr) {
        UBS_VLOG_ERR("TX umq buffer list is in error, TX user context does not contain the right list\n");
        return -1;
    }

    // 如果是一个 read OP, 那么它的 left_size=0.
    if (last_qbuf != nullptr) {
        QBUF_LIST_NEXT(last_qbuf) = nullptr;
    }

    uint64_t free_start = 0;
    if (do_trace) {
        free_start = ubsocket_get_timeNs();
    }
    UmqApi::umq_buf_free(start_qbuf);
    if (do_trace) {
        uint64_t free_end = ubsocket_get_timeNs();
        trace->AddWriteTrace(CORE_WRITE_POLL_CQE_FREE, raw_socket, free_start, free_end);
    }

    return wr_cnt;
}

void UmqTxHelper::HandleTxCqeError(umq_buf_t *qbuf, int &wr_cnt)
{
    // 探测包错误处理
    if (HandleProbePacket(qbuf)) {
        return;
    }

    // 正常错误处理流程
    LogTxCqeErrorMsg(qbuf);
    ProcessErrorTxCqe(qbuf);
    wr_cnt++;
}

bool UmqTxHelper::HandleProbePacket(umq_buf_t *qbuf)
{
    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
    if (buf_pro->opcode == UMQ_OPC_SEND_IMM && buf_pro->imm.user_data == UmqSetting::UMQ_PROBE_USER_DATA_ID) {
        UmqApi::umq_buf_free(qbuf);
        return true; // 已处理
    }
    return false; // 不是探测包
}

void UmqTxHelper::LogTxCqeErrorMsg(umq_buf_t *buf)
{
    auto bufStatus = static_cast<umq_buf_status_t>(buf->status);
    int mappedErrno = UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, bufStatus, errno);
    const char *desc = UmqErrnoConverter::GetBufStatusDescription(UmqOperation::WRITEV, bufStatus);
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

        default:
            UBS_VLOG_ERR("[UMQ_CQE] unreachable! status=%d\n", buf->status);
            break;
    }
}

void UmqTxHelper::ProcessErrorTxCqe(umq_buf_t *first_qbuf)
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

void *UmqTxHelper::PtrFloorToBoundary(void *ptr)
{
    return (void *)((uint64_t)ptr & ~UmqSetting::FloorMask());
}

} // namespace umq
} // namespace ubs
} // namespace ock