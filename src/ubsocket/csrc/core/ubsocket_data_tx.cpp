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
#include "ubsocket_data_tx.h"

#include "profiling/ubsocket_prof.h"
#include "under_api/dl_libc_api.h"

#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
DataTx::DataTx(const SocketPtr &sock, DataTxOps *ops) : fd_(sock->raw_socket_), event_fd_(sock->event_fd_), tx_ops_(ops)
{
    /* caller must make sure ops is not null */
}

DataTx::~DataTx()
{
    delete tx_ops_;
    tx_ops_ = nullptr;
}

DataTx::DataTx(DataTx &&other) noexcept : fd_(other.fd_), event_fd_(other.event_fd_), tx_ops_(other.tx_ops_)
{
    other.tx_ops_ = nullptr;
}

DataTx &DataTx::operator=(DataTx &&other) noexcept
{
    if (this != &other) {
        delete tx_ops_;
        fd_ = other.fd_;
        event_fd_ = other.event_fd_;
        tx_ops_ = other.tx_ops_;
        other.tx_ops_ = nullptr;
    }
    return *this;
}

ssize_t DataTx::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept
{
    auto *trace = sock->split_trace_;
    TRACE_ADD_WRITE_SIMPLE(trace, CORE_WRITE, fd_);
    PROF_START(CORE_WRITE);

    if (sock->State() == SOCK_STAT_BROKEN) {
        errno = EPIPE;
        UBS_VLOG_TRACE("socket(%d) is broken for physical link error.", sock->Fd());
        return -1;
    }

    if (UNLIKELY(iov == nullptr || iovcnt == 0)) {
        errno = EINVAL;
        UBS_VLOG_ERR("WriteV invalid argument, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return -1;
    }

    if (UNLIKELY(sock->State() == SOCK_STAT_CLOSE)) {
        errno = EPIPE;
        UBS_VLOG_ERR("WriteV socket is closed, fd: %d, ret: %d, errno: %d, errmsg: %s\n", fd_, -1, errno,
                     Func::Error2Str(errno));
        PROF_END(CORE_WRITE, false);
        return -1;
    }

    ssize_t result;
    if (UNLIKELY(tx_ops_ == nullptr)) {
        errno = ENOTCONN;
        UBS_VLOG_ERR("data tx for socket(%d) not set tx_ops", fd_);
        result = -1;
    } else {
        result = tx_ops_->WriteV(sock, iov, iovcnt, flags);
    }
    PROF_END(CORE_WRITE, result >= 0);
    TRACE_TRY_SWAP(trace);
    return result;
}
} // namespace ubs
} // namespace ock
