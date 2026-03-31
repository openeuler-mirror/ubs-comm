// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: Provide the utility for umq buffer, iov, etc

#include "brpc_file_descriptor.h"

namespace Brpc {
void SocketFd::HandleErrorRxCqe(umq_buf_t *buf)
{
    switch (buf->status) {
        case UMQ_BUF_SUCCESS:
            return;

        case UMQ_BUF_UNSUPPORTED_OPCODE_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: unsupported opcode\n");
            break;

        case UMQ_BUF_LOC_LEN_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: local length too long\n");
            break;

        case UMQ_BUF_LOC_OPERATION_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: local op err\n");
            break;

        case UMQ_BUF_LOC_ACCESS_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: access to local memory error\n");
            break;

        case UMQ_BUF_REM_RESP_LEN_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote rx buffer length error\n");
            break;

        case UMQ_BUF_REM_UNSUPPORTED_REQ_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote does not support req\n");
            break;

        case UMQ_BUF_REM_OPERATION_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty can not complete op\n");
            break;

        case UMQ_BUF_REM_ACCESS_ABORT_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty access memory error\n");
            break;

        case UMQ_BUF_ACK_TIMEOUT_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty does not send ack\n");
            break;

        case UMQ_BUF_RNR_RETRY_CNT_EXC_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty has no enough RQE\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR:
            break;

        case UMQ_BUF_WR_SUSPEND_DONE:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: suspend done\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR_DONE:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: flush err done\n");
            break;

        case UMQ_BUF_WR_UNHANDLED:
            // See umq_ub_flush_seq
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "It wont be here.\n");
            break;

        case UMQ_BUF_LOC_DATA_POISON:
        case UMQ_BUF_REM_DATA_POISON:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: not supported yet\n");
            break;

        case UMQ_FAKE_BUF_FC_UPDATE:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "You should handle flow control message manually\n");
            break;

        case UMQ_MEMPOOL_UPDATE_SUCCESS:
        case UMQ_MEMPOOL_UPDATE_FAILED:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "Something went wrong. brpc-adaptor ONLY uses UB send/recv\n");
            break;

        default:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "unreachable! status=%d\n", buf->status);
            break;
    }

    // 异步关闭. 当前处于 readv 中，等到下次 EPOLLIN 事件到来时会触发关闭
    Close();
}

void SocketFd::HandleErrorTxCqe(umq_buf_t *buf)
{
    switch (buf->status) {
        case UMQ_BUF_SUCCESS:
            return;

        case UMQ_BUF_UNSUPPORTED_OPCODE_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: unsupported opcode\n");
            break;

        case UMQ_BUF_LOC_LEN_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: local length too long\n");
            break;

        case UMQ_BUF_LOC_OPERATION_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: local op err\n");
            break;

        case UMQ_BUF_LOC_ACCESS_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: access to local memory error\n");
            break;

        case UMQ_BUF_REM_RESP_LEN_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote rx buffer length error\n");
            break;

        case UMQ_BUF_REM_UNSUPPORTED_REQ_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote does not support req\n");
            break;

        case UMQ_BUF_REM_OPERATION_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty can not complete op\n");
            break;

        case UMQ_BUF_REM_ACCESS_ABORT_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty access memory error\n");
            break;

        case UMQ_BUF_ACK_TIMEOUT_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty does not send ack\n");
            break;

        case UMQ_BUF_RNR_RETRY_CNT_EXC_ERR:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: remote jetty has no enough RQE\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR:
            break;

        case UMQ_BUF_WR_SUSPEND_DONE:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: suspend done\n");
            break;

        case UMQ_BUF_WR_FLUSH_ERR_DONE:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: flush err done\n");
            break;

        case UMQ_BUF_WR_UNHANDLED:
            // See umq_ub_flush_seq
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "It wont be here.\n");
            break;

        case UMQ_BUF_LOC_DATA_POISON:
        case UMQ_BUF_REM_DATA_POISON:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "cqe error: not supported yet\n");
            break;

        default:
            RPC_ADPT_VLOG_ERR(ubsocket::UMQ_CQE, "unreachable! status=%d\n", buf->status);
            break;
    }

    // 异步关闭. 当前处于 writev 尾部, 等待下次 EPOLLIN 事件时关闭
    Close();
}
}  // namespace Brpc
