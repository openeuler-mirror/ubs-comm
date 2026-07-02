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

#include "ubsocket_data_rx.h"
#include "common/ubsocket_common_includes.h"
#include "profiling/ubsocket_prof.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataRx::DataRx(const SocketPtr &sock, DataRxOps *ops) : fd_(sock->raw_socket_), event_fd_(sock->event_fd_), rx_ops_(ops)
{
    /* caller must make sure ops is not null */
}

ssize_t DataRx::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    PROF_START(CORE_READ);
    PROF_START(CORE_READ_EAGAIN);
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        ssize_t size = LibcApi::readv(fd_, iov, iovcnt);
        PROF_END(CORE_READ, size >= 0);
        return size;
    }

    if (iov == nullptr || iovcnt == 0) {
        errno = EINVAL;
        UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                      Func::Error2Str(errno));
        PROF_END(CORE_READ, false);
        return UBS_ERROR;
    }

    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_base == nullptr) {
            errno = EINVAL;
            UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                          Func::Error2Str(errno));
            PROF_END(CORE_READ, false);
            return UBS_ERROR;
        }
    }

    auto *trace = sock->split_trace_;

    /* if socket failed to pass protocol negotiation validation, then
     * (1) pass the received protocol negotiation as message to caller;
     * (2) when all the received message passed to caller, fallback to tcp/ip */
    ssize_t rx_total_len = OutputErrorMagicNumber(sock, iov, iovcnt);
    if (rx_total_len > 0) {
        TRACE_ADD_READ(trace, CORE_READ, fd_, ubsocket_get_timeNs_compile(), 0);
        PROF_END(CORE_READ, false);
        return rx_total_len;
    }

    PROF_START(CORE_READ_POLL_RX);
    uint64_t start_time = ubsocket_get_timeNs_compile();
    int ret = rx_ops_->PollRx(sock);
    uint64_t end_time = ubsocket_get_timeNs_compile();
    PROF_END(CORE_READ_POLL_RX, ret >= 0);
    TRACE_ADD_READ(trace, CORE_READ_POLL_RX, fd_, start_time, end_time);

    if (ret < 0) {
        TRACE_ADD_READ(trace, CORE_READ, fd_, ubsocket_get_timeNs_compile(), 0);
        PROF_END(CORE_READ, false);
        return ret;
    }

    uint32_t max_buf_size;
    if (GlobalSetting::UBS_READV_UNLIMITED) {
        max_buf_size = UINT32_MAX;
    } else {
        max_buf_size = 0;
        for (int i = 0; i < iovcnt; i++) {
            max_buf_size += iov[i].iov_len;
        }
    }

    uint64_t data_set_start_time = ubsocket_get_timeNs_compile();
    ret = rx_ops_->RxDataSet(iov[0].iov_base, max_buf_size);
    uint64_t data_set_end_time = ubsocket_get_timeNs_compile();
    TRACE_ADD_READ(trace, CORE_READ_RX_DATA_SET, fd_, data_set_start_time, data_set_end_time);

    if (ret < 0) {
        if (!((errno == EINTR) || (errno == EAGAIN))) {
            PROF_END(CORE_READ, false);
            TRACE_ADD_READ(trace, CORE_READ, fd_, ubsocket_get_timeNs_compile(), 0);
        } else {
            PROF_END(CORE_READ_EAGAIN, true);
            TRACE_ADD_READ(trace, CORE_READ_EAGAIN, fd_, ubsocket_get_timeNs_compile(), 0);
        }
        return ret;
    }
    rx_total_len = ret;
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
        sockptr->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::RX_BYTE_COUNT, rx_total_len);
    }
    TRACE_ADD_READ(trace, CORE_READ, fd_, ubsocket_get_timeNs_compile(), 0);
    PROF_END(CORE_READ, true);
    TRACE_TRY_SWAP(trace);
    return rx_total_len;
}

ssize_t DataRx::OutputErrorMagicNumber(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    if (protocol_negotiation_recv_size_ == 0) {
        return 0;
    }

    ssize_t rx_total_len = 0;
    int iov_idx = 0;
    do {
        size_t copy_size = iov[iov_idx].iov_len < protocol_negotiation_recv_size_ ? iov[iov_idx].iov_len :
                                                                                    protocol_negotiation_recv_size_;
        (void)memcpy(iov[iov_idx++].iov_base, (char *)&protocol_negotiation_ + protocol_negotiation_offset_, copy_size);
        protocol_negotiation_recv_size_ -= copy_size;
        protocol_negotiation_offset_ += copy_size;
        rx_total_len += copy_size;
    } while (protocol_negotiation_recv_size_ > 0 && iov_idx < iovcnt);

    if (protocol_negotiation_recv_size_ == 0) {
        sock->State(SOCK_STAT_RAW_ESTABLISHED);
    }
    return rx_total_len;
}

ssize_t DataRxOps::RxDataSet(void *buf, uint32_t size)
{
    Block *out_first_block = DataToBlock(buf);
    if (out_first_block == nullptr) {
        errno = EINVAL;
        UBS_VLOG_ERR("ReadV failed to locate brpc block for data %p, fd: %d\n", buf, fd_);
        return UBS_ERROR;
    }
    ssize_t rx_total_len = block_cache_.CutAndInsertAfter(size, out_first_block);
    if (rx_total_len == 0) {
        /*
         * m_rx.epoll_event_num_ not equals to m_rx.m_expect_epoll_event_num means another epoll event is reported
         * during readv processing procedure, set m_rx.m_poll to enable poll RX operation and set errno to EINTR
         * to let brpc retry and call readv()
         */
        if (!epoll_event_num_.compare_exchange_strong(expect_epoll_event_num_, 0, std::memory_order_release,
                                                      std::memory_order_acquire)) {
            poll_ = true;
            errno = EINTR;
            return UBS_ERROR;
        }
        if (ArraySet<Socket>::GetInstance().GetItem(fd_)->State() == SOCK_STAT_CLOSE) {
            return 0;
        }

        if (flow_control_failed_) {
            errno = EIO;
            UBS_VLOG_ERR("ReadV flow control failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return UBS_ERROR;
        }

        if (RearmRxInterrupt() < 0) {
            errno = EIO;
            UBS_VLOG_ERR("ReadV RearmRxInterrupt() failed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                         Func::Error2Str(errno));
            return UBS_ERROR;
        }

        // UB 链路上无数据，但还是触发了 EPOLLIN 事件，可能是对端 TCP 连接关闭了，此种场景下向 brpc
        // 返回 0 暗示读到 EOF, brpc 随后会主动关闭连接.
        char b[1];
        int n = LibcApi::recv(fd_, b, sizeof(b), MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) {
            UBS_VLOG_INFO("The TCP connection has been closed by peer.\n");
            auto trace_sock = ArraySet<Socket>::GetInstance().GetItem(fd_);
            if (trace_sock != nullptr) {
                TRACE_FLUSH(trace_sock->split_trace_);
            }
            return 0;
        }

        /* return UBS_ERROR and set errno to EAGAIN to notice user no more data to read */
        errno = EAGAIN;
        return UBS_ERROR;
    }

    /* Set the first block as used to prevent brpc from utilizing this block,
     * and only use it as the head of the block linked list. */
    out_first_block->size = out_first_block->cap;
    return rx_total_len;
}
} // namespace ubs
} // namespace ock
