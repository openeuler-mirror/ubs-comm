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
#include "profiling/ubsocket_prof.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataTx::DataTx(const SocketPtr &sock, DataTxOps *ops) : fd_(sock->raw_socket_), event_fd_(sock->event_fd_), tx_ops_(ops)
{
    /* caller must make sure ops is not null */
}

ssize_t DataTx::WriteVCopy(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    auto *trace = sock->split_trace_;
    TRACE_ADD_WRITE_SIMPLE(trace, CORE_WRITE, fd_);
    PROF_START(CORE_WRITE);
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        ssize_t size = LibcApi::writev(fd_, iov, iovcnt);
        PROF_END(CORE_WRITE, size >= 0);
        TRACE_TRY_SWAP(trace);
        return size;
    }

    if (iov == nullptr || iovcnt == 0) {
        errno = EINVAL;
        UBS_VLOG_ERR("WriteV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return UBS_ERROR;
    }

    if (sock->State() == SOCK_STAT_CLOSE) {
        errno = EPIPE;
        UBS_VLOG_ERR("WriteV socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return UBS_ERROR;
    }

    if (!tx_ops_->Writable(sock)) {
        errno = EAGAIN;
        UBS_VLOG_DEBUG("WriteV socket is not writable, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                       Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return -1;
    }

    const struct iovec *final_iov = iov;
    int final_iovcnt = iovcnt;
    std::vector<struct iovec> new_iovs;
    std::vector<void *> allocated_blocks; // 用于错误回滚

    size_t total_len = 0;
    for (int i = 0; i < iovcnt; ++i)
        total_len += iov[i].iov_len;
    if (total_len == 0)
        return 0;

    const size_t PAYLOAD = tx_ops_->IOBufSize();
    uint32_t num_blocks = (total_len + PAYLOAD - 1) / PAYLOAD;

    // 提前检查窗口
    if (num_blocks > tx_ops_->tx_queue_avail_num_.load(std::memory_order_acq_rel)) {
        errno = EAGAIN;
        return -1;
    }

    new_iovs.reserve(num_blocks);
    allocated_blocks.reserve(num_blocks);

    size_t remain = total_len;
    int iov_idx = 0;
    size_t iov_off = 0;

    PROF_START(CORE_WRITE_COPY);
    while (remain > 0) {
        size_t copy_len = std::min(remain, PAYLOAD);
        char *block = static_cast<char *>(ubsocket_iobuf_allocate(IOBUF_DIFF + copy_len, nullptr));
        if (block == nullptr) {
            for (auto b : allocated_blocks)
                ubsocket_iobuf_deallocate(b);
            errno = ENOMEM;
            return -1;
        }
        allocated_blocks.push_back(block);

        // 手动初始化 Block 头
        Block *blk = reinterpret_cast<Block *>(block);
        blk->nshared.store(0, std::memory_order_relaxed);
        blk->flags = 0;
        blk->abi_check = 0;
        blk->size = copy_len;
        blk->cap = PAYLOAD;
        blk->u.portal_next = nullptr;
        blk->data = block + IOBUF_DIFF;
        char *payload = blk->data;
        // 拷贝数据
        size_t done = 0;
        while (done < copy_len) {
            const struct iovec &v = iov[iov_idx];
            size_t left = v.iov_len - iov_off;
            size_t to_copy = std::min(copy_len - done, left);
            memcpy(payload + done, (char *)v.iov_base + iov_off, to_copy);
            done += to_copy;
            iov_off += to_copy;
            if (iov_off >= v.iov_len) {
                ++iov_idx;
                iov_off = 0;
            }
        }
        new_iovs.push_back({payload, copy_len});
        remain -= copy_len;
    }
    PROF_END(CORE_WRITE_COPY, true);

    final_iov = new_iovs.data();
    final_iovcnt = new_iovs.size();


    PROF_START(CORE_WRITE_BUILD_IOV);
    /* 拆细 BUILD_IOV: INNER 仅覆盖 converter 构造, 其余为切分循环 + AllocTxBuf */
    PROF_START(CORE_WRITE_BUILD_IOV_INNER);
    ConverterPtr converterPtr = tx_ops_->BuildIovConverter(final_iov, final_iovcnt);
    PROF_END(CORE_WRITE_BUILD_IOV_INNER, converterPtr != nullptr);
    uint32_t input_total_len = 0;
    uint32_t batch = 0;
    uint32_t post_batch_max = tx_ops_->tx_queue_avail_num_.load(std::memory_order_acq_rel) > TX_POST_BATCH_MAX ?
                                  TX_POST_BATCH_MAX :
                                  tx_ops_->tx_queue_avail_num_.load(std::memory_order_acq_rel);
    uint32_t buf_cnt = 0;
    uint32_t cut_total_len = 0;

    do {
        cut_total_len = 0;
        uint32_t cut_len = 0;
        uint32_t wr_left_len = tx_ops_->IOBufSize();
        uint32_t sge_idx = 0;
        while (sge_idx++ < TX_SGE_MAX && cut_total_len < tx_ops_->IOBufSize() &&
               ((cut_len = converterPtr->IndexMove(wr_left_len)) != 0)) {
            ++buf_cnt;
            wr_left_len -= cut_len;
            cut_total_len += cut_len;
        }
        input_total_len += cut_total_len;
    } while (cut_total_len != 0 && ++batch < post_batch_max);

    /*
     * TX buffer 申请。SINGLE jetty 场景下 TX CQE 仅由 TxCqePoller 的 100ms 定时器回收，
     * 一旦 buffer 池吃紧，这里会成为写路径的阻塞点，是 RTT 长尾的重点怀疑项。
     * 失败次数(failure 列)同样关键：失败即意味着本次 writev 退化为 EAGAIN 重试。
     */
    PROF_START(CORE_WRITE_ALLOC_TX_BUF);
    uintptr_t txBuf = tx_ops_->AllocTxBuf(0, buf_cnt);
    PROF_END(CORE_WRITE_ALLOC_TX_BUF, txBuf != 0);

    if (txBuf == 0) {
        PROF_END(CORE_WRITE, false);
        PROF_END(CORE_WRITE_BUILD_IOV, false);
        return -1;
    }

    PROF_END(CORE_WRITE_BUILD_IOV, true);

    PROF_START(CORE_WRITE_POST_SEND);

    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    /*
     * Clear the latent EPOLLOUT token before posting. A concurrent flow-control or
     * resource wakeup can then restore it without being overwritten after umq_post().
     */
    sockBase->SetWritableReady(false);

    uint32_t tx_total_len;
    int64_t ret = tx_ops_->PostSend(sock, txBuf, batch, converterPtr);
    if (ret < 0) {
        PROF_END(CORE_WRITE_POST_SEND, false);
        PROF_END(CORE_WRITE, false);
        return ret;
    }
    PROF_END(CORE_WRITE_POST_SEND, true);
    tx_total_len = ret;

    if (tx_total_len == input_total_len) {
        /*
         * EPOLLOUT is removed from the raw socket registration. Keep one readiness
         * token so a later EPOLL_CTL_MOD can synthesize the next edge-triggered event.
         */
        sockBase->SetWritableReady(true);
    }

    if (GlobalSetting::UBS_TRACE_ENABLED) {
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::TX_BYTE_COUNT, tx_total_len);
    }
    PROF_END(CORE_WRITE, true);
    TRACE_TRY_SWAP(trace);
    return tx_total_len;
}

ssize_t DataTx::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    auto *trace = sock->split_trace_;
    TRACE_ADD_WRITE_SIMPLE(trace, CORE_WRITE, fd_);
    PROF_START(CORE_WRITE);
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        ssize_t size = LibcApi::writev(fd_, iov, iovcnt);
        PROF_END(CORE_WRITE, size >= 0);
        TRACE_TRY_SWAP(trace);
        return size;
    }
    if (iov == nullptr || iovcnt == 0) {
        errno = EINVAL;
        UBS_VLOG_ERR("WriteV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return UBS_ERROR;
    }
    if (sock->State() == SOCK_STAT_CLOSE) {
        errno = EPIPE;
        UBS_VLOG_ERR("WriteV socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return UBS_ERROR;
    }
    if (!tx_ops_->Writable(sock)) {
        errno = EAGAIN;
        UBS_VLOG_DEBUG("WriteV socket is not writable, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                       Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return -1;
    }
    size_t total_len = 0;
    for (int i = 0; i < iovcnt; ++i)
        total_len += iov[i].iov_len;
    if (total_len == 0)
        return 0;

    PROF_START(CORE_WRITE_BUILD_IOV);
    PROF_START(CORE_WRITE_BUILD_IOV_INNER);
    ConverterPtr converterPtr = tx_ops_->BuildIovConverter(iov, iovcnt);
    PROF_END(CORE_WRITE_BUILD_IOV_INNER, converterPtr != nullptr);
    uint32_t input_total_len = 0;
    uint32_t batch = 0;
    uint32_t post_batch_max = tx_ops_->tx_queue_avail_num_.load(std::memory_order_acq_rel) > TX_POST_BATCH_MAX ?
                                  TX_POST_BATCH_MAX :
                                  tx_ops_->tx_queue_avail_num_.load(std::memory_order_acq_rel);
    uint32_t buf_cnt = 0;
    uint32_t cut_total_len = 0;
    do {
        cut_total_len = 0;
        uint32_t cut_len = 0;
        uint32_t wr_left_len = tx_ops_->IOBufSize();
        uint32_t sge_idx = 0;
        while (sge_idx++ < TX_SGE_MAX && cut_total_len < tx_ops_->IOBufSize() &&
               ((cut_len = converterPtr->IndexMove(wr_left_len)) != 0)) {
            ++buf_cnt;
            wr_left_len -= cut_len;
            cut_total_len += cut_len;
        }
        input_total_len += cut_total_len;
    } while (cut_total_len != 0 && ++batch < post_batch_max);

    PROF_START(CORE_WRITE_ALLOC_TX_BUF);
    uintptr_t txBuf = tx_ops_->AllocTxBuf(0, buf_cnt);
    PROF_END(CORE_WRITE_ALLOC_TX_BUF, txBuf != 0);
    if (txBuf == 0) {
        PROF_END(CORE_WRITE, false);
        PROF_END(CORE_WRITE_BUILD_IOV, false);
        return -1;
    }
    PROF_END(CORE_WRITE_BUILD_IOV, true);

    PROF_START(CORE_WRITE_POST_SEND);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    sockBase->SetWritableReady(false);
    uint32_t tx_total_len;
    int64_t ret = tx_ops_->PostSend(sock, txBuf, batch, converterPtr);
    if (ret < 0) {
        PROF_END(CORE_WRITE_POST_SEND, false);
        PROF_END(CORE_WRITE, false);
        return ret;
    }
    PROF_END(CORE_WRITE_POST_SEND, true);
    tx_total_len = ret;
    if (tx_total_len == input_total_len) {
        sockBase->SetWritableReady(true);
    }
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        sockBase->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::TX_BYTE_COUNT, tx_total_len);
    }
    PROF_END(CORE_WRITE, true);
    TRACE_TRY_SWAP(trace);
    return tx_total_len;
}
} // namespace ubs
} // namespace ock
