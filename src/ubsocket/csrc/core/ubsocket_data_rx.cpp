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

    /* if socket failed to pass protocol negotiation validation, then
     * (1) pass the received protocol negotiation as message to caller;
     * (2) when all the received message passed to caller, fallback to tcp/ip */
    ssize_t rx_total_len = OutputErrorMagicNumber(sock, iov, iovcnt);
    if (rx_total_len > 0) {
        PROF_END(CORE_READ, false);
        return rx_total_len;
    }

    PROF_START(CORE_READ_POLL_RX);
    int ret = rx_ops_->PollRx(sock);
    PROF_END(CORE_READ_POLL_RX, ret >= 0);
    if (ret < 0) {
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

    ret = rx_ops_->RxDataSet(iov[0].iov_base, max_buf_size);
    if (ret < 0) {
        if (!((errno == EINTR) || (errno == EAGAIN))) {
            PROF_END(CORE_READ, false);
        } else {
            PROF_END(CORE_READ_EAGAIN, true);
        }
        return ret;
    }
    rx_total_len = ret;
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
        sockptr->GetStatsMgr()->UpdateTraceStats(Statistics::StatsMgr::RX_BYTE_COUNT, rx_total_len);
    }
    PROF_END(CORE_READ, true);
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
    /*
     * rpc adapter has replace brpc butil::iobuf::blockmem_allocate() & butil::iof::blockmem_deallocate()
     * and ensures that the starting address of the Block is aligned to an 8k boundary.
     */
    Block *out_first_block = (Block *)PtrFloorToBoundary(buf);
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