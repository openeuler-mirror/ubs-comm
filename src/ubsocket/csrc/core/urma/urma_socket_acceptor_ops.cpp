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
#include "urma_socket_acceptor_ops.h"

#include <arpa/inet.h>
#include <unistd.h>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket_helper.h"
#include "urma_setting.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

UrmaAcceptorOps::UrmaAcceptorOps(int listen_fd)
{
    fd = listen_fd;
}

UrmaAcceptorOps::~UrmaAcceptorOps()
{
    DestroySocketResources();
}

Result UrmaAcceptorOps::PrepareConnect(int new_fd)
{
    (void)new_fd;
    return UBS_OK;
}

int UrmaAcceptorOps::ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size)
{
    (void)fd;
    (void)protocol_negotiation;
    (void)protocol_negotiation_recv_size;
    return 0;
}

Result UrmaAcceptorOps::Negotiate(SocketPtr socketPtr)
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(socketPtr);
    if (urma_sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    UrmaNegotiateReq req = {};
    Result ret = SocketConnHelper::RecvLengthPrefixed(fd, &req, sizeof(req), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        return ret;
    }

    UBSVersion local_version = UBS_PROTOCOL_VERSION;
    UBSVersion peer_version(req.peer_version);
    UBSVersion negotiated;
    VersionCheckResult ver_result = local_version.Negotiate(peer_version, negotiated);
    uint32_t negotiated_version = negotiated.GetWhole();

    if (ver_result == VersionCheckResult::kMajorMismatch) {
        uint32_t mismatch_version = local_version.GetWhole();
        SocketConnHelper::SendSocketData(fd, &mismatch_version, sizeof(mismatch_version), NEGOTIATE_TIMEOUT_MS);
        return UBS_TCP_EXCHANGE;
    }

    ssize_t n =
        SocketConnHelper::SendSocketData(fd, &negotiated_version, sizeof(negotiated_version), NEGOTIATE_TIMEOUT_MS);
    if (n != static_cast<ssize_t>(sizeof(negotiated_version))) {
        return UBS_TCP_EXCHANGE;
    }

    urma_sock->SetNegotiatedVersion(negotiated_version);
    urma_sock->SetPeerVersion(peer_version.GetWhole());

    UrmaNegotiateRsp rsp = {};
    rsp.ret_code = 0;
    rsp.tp_type = TPT_CTP;
    rsp.tp_mode = TPM_RC;
    rsp.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    rsp.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;

    ret = SocketConnHelper::SendLengthPrefixed(fd, &rsp, sizeof(rsp), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        return ret;
    }

    urma_sock->SetPeerEid(req.local_eid);

    return UBS_OK;
}

Result UrmaAcceptorOps::CreateSocketResources(SocketPtr socketPtr)
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(socketPtr);
    if (urma_sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    return urma_sock->ExchangeSocketResources(fd, *urma_sock.Get());
}

void UrmaAcceptorOps::DestroySocketResources() {}

} // namespace urma
} // namespace ubs
} // namespace ock
