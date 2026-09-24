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
#include "ubsocket_conn_ops_base.h"

#include <sys/socket.h>

#include <cerrno>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {

UBConnOpsBase::UBConnOpsBase(ConnRole role, int fd, const struct sockaddr &addr, socklen_t addr_len)
    : fd_(fd),
      addr_(addr),
      addr_len_(addr_len),
      role_(role),
      state_(role == ConnRole::kClient ? ConnState::kWaitTcpConnect : ConnState::kWaitNegotiate)
{
}

ConnStageResult UBConnOpsBase::Execute(uint64_t effective_now_ms)
{
    last_active_ms_ = effective_now_ms;
    if (IsAborted()) {
        return ConnStageResult::kFailed;
    }

    switch (State()) {
        case ConnState::kWaitTcpConnect:
            return DoWaitTcpConnect();
        case ConnState::kWaitNegotiate:
            return DoWaitNegotiate();
        case ConnState::kWaitPeerRes:
            return DoWaitPeerRes();
        case ConnState::kWaitPeerAck:
            return DoWaitPeerAck();
        case ConnState::kConnEstablished:
            return ConnStageResult::kDone;
        case ConnState::kConnFailed:
            return ConnStageResult::kFailed;
    }
    return ConnStageResult::kFailed;
}

const char *UBConnOpsBase::StateName() const
{
    if (State() == ConnState::kWaitNegotiate) {
        /* kWaitNegotiate 两侧语义相反（Client 等 Rsp / Server 等 Req），按角色细分便于诊断 */
        return (role_ == ConnRole::kClient) ? "WaitNegotiateRsp" : "WaitNegotiateReq";
    }
    return ConnStateName(State());
}

Result UBConnOpsBase::SendControlNonBlocking(const void *data, size_t size, size_t &sent)
{
    sent = 0;
    const char *cur = static_cast<const char *>(data);
    size_t left = size;
    while (left > 0) {
        errno = 0;
        ssize_t n = LibcApi::send(fd_, cur, left, MSG_NOSIGNAL);
        if (n > 0) {
            cur += n;
            left -= static_cast<size_t>(n);
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            UBS_VLOG_ERR("UBConnOpsBase send returns 0, fd: %d\n", fd_);
            return UBS_TCP_EXCHANGE;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return UBS_WOULD_BLOCK; /* 已发 sent 字节，剩余等 fd 可写后续发 */
        }
        UBS_VLOG_ERR("UBConnOpsBase send failed, fd: %d, errno: %d, errmsg: %s\n", fd_, errno, Func::Error2Str(errno));
        return UBS_TCP_EXCHANGE;
    }
    return UBS_OK;
}

void UBConnOpsBase::StageControl(const void *data, size_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    pending_send_buf_.assign(bytes, bytes + size);
    pending_send_off_ = 0;
    wants_writable_ = true;
}

Result UBConnOpsBase::FlushPendingSend()
{
    if (pending_send_buf_.empty()) {
        return UBS_OK;
    }
    size_t sent = 0;
    Result ret = SendControlNonBlocking(pending_send_buf_.data() + pending_send_off_,
                                        pending_send_buf_.size() - pending_send_off_, sent);
    if (ret == UBS_WOULD_BLOCK) {
        pending_send_off_ += sent;
        wants_writable_ = true;
        return ret;
    }
    pending_send_buf_.clear();
    pending_send_off_ = 0;
    wants_writable_ = false;
    return ret;
}

} // namespace ubs
} // namespace ock
