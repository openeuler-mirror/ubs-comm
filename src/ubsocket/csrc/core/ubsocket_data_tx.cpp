/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "ubsocket_data_tx.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataTx::DataTx(const SocketInfo &info, DataTxOpsPtr ops)
    : fd_(info.raw_socket_),
      event_fd_(info.event_fd_),
      tx_ops_(std::move(ops))
{
}

ssize_t DataTx::WriteV(const SocketInfo &sock, const struct iovec *iov, int iovcnt)
{
    int retCode = -1;
    int flagEIO = -1;
    if (sock.State() == SOCK_STAT_RAW_ESTABLISHED) {
        ssize_t size = LibcApi::writev(fd_, iov, iovcnt);
        retCode = size < 0 ? -1 : 0;
        return size;
    }

    TRACE_DELAY_AUTO(BRPC_WRITEV_CALL, retCode);
    if (iov == nullptr || iovcnt == 0) {
        errno = EINVAL;
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        //RPC_ADPT_VLOG_ERR(UBSocket, "WriteV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
        //                  NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        return -1;
    }

    if (sock.State() == SOCK_STAT_CLOSE) {
        errno = EPIPE;
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        //  RPC_ADPT_VLOG_ERR(UBSocket, "WriteV socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
        //                  NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        return -1;
    }

    if (tx_ops_->PollTx() < 0) {
        return -1;
    }

    IovConverter iov_converter(iov, iovcnt);
    uint32_t input_total_len = 0;
    uint32_t batch = 0;
    uint32_t post_batch_max = tx_ops_->tx_queue_avail_num_ > TX_POST_BATCH_MAX ? TX_POST_BATCH_MAX :
                                                                                 tx_ops_->tx_queue_avail_num_;
    uint32_t buf_cnt = 0;
    uint32_t cut_total_len = 0;

    do {
        cut_total_len = 0;
        uint32_t cut_len = 0;
        uint32_t wr_left_len = tx_ops_->IOBufSize();
        uint32_t sge_idx = 0;
        while (sge_idx++ < TX_SGE_MAX && cut_total_len < tx_ops_->IOBufSize() &&
               ((cut_len = iov_converter.Cut(wr_left_len)) != 0)) {
            ++buf_cnt;
            wr_left_len -= cut_len;
            cut_total_len += cut_len;
        }
        input_total_len += cut_total_len;
    } while (cut_total_len != 0 && ++batch < post_batch_max);

    // 分配txBuf
    uintptr_t txBuf = tx_ops_->AllocTxBuf(buf_cnt);

    // 发送数据
    uint32_t tx_total_len = 0;
    int64_t ret = tx_ops_->PostSend(txBuf, batch);
    if (ret < 0) {
        return ret;
    }
    tx_total_len = ret;

    if (globalSetting.UBS_TRACE_ENABLED) {
        UpdateTraceStats(StatsMgr::TX_BYTE_COUNT, tx_total_len);
    }

    retCode = 0;
    return tx_total_len;
}
} // namespace ubs
} // namespace ock