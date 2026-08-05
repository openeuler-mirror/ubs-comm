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
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataRx::DataRx(const SocketPtr &sock, DataRxOps *ops) : fd_(sock->raw_socket_), event_fd_(sock->event_fd_), rx_ops_(ops)
{
    /* caller must make sure ops is not null */
}

ssize_t DataRx::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    UBS_VLOG_TRACE("enter DataRx::ReadV fd=%d, iovcnt=%d, iov[0].buf=%p", sock->Fd(), iovcnt, iov[0].iov_base);
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        UBS_VLOG_TRACE("socket(%d) state raw established, run libc readv.", sock->Fd());
        return LibcApi::readv(fd_, iov, iovcnt);
    }

    if (UNLIKELY(iov == nullptr || iovcnt == 0)) {
        errno = EINVAL;
        UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                      Func::Error2Str(errno));
        return -1;
    }

    for (int i = 0; i < iovcnt; i++) {
        if (UNLIKELY(iov[i].iov_base == nullptr)) {
            errno = EINVAL;
            UBS_VLOG_WARN("ReadV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                          Func::Error2Str(errno));
            return -1;
        }
    }

    if (UNLIKELY(rx_ops_ == nullptr)) {
        errno = ENOTCONN;
        UBS_VLOG_ERR("socket(%d) rx ops null", sock->Fd());
        return -1;
    }

    return rx_ops_->ReadV(sock, iov, iovcnt, UBS_IO_FLAG_USE_BRPC_BLOCK);
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
} // namespace ubs
} // namespace ock
