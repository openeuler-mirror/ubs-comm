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
#include "urma_socket_connector_ops.h"

#include <arpa/inet.h>
#include <unistd.h>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket_helper.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

UrmaConnectorOps::UrmaConnectorOps(int fd)
{
    raw_fd_ = fd;
}

UrmaConnectorOps::~UrmaConnectorOps()
{
    DestroySocketResources();
}

Result UrmaConnectorOps::BuildTfoPayload(uint8_t *buf, int &buf_len, const SocketPtr &sock)
{
    int offset = 0;
    uint64_t magic = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    memcpy(buf + offset, &magic, sizeof(magic));
    offset += sizeof(magic);

    uint32_t version = UBS_PROTOCOL_VERSION.GetWhole();
    memcpy(buf + offset, &version, sizeof(version));
    offset += sizeof(version);

    buf_len = offset;
    (void)sock;
    return UBS_OK;
}

Result UrmaConnectorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                        const SocketPtr &sock)
{
    (void)address_len;
    (void)sock;
    raw_fd_ = new_fd;
    SocketConnHelper::SetTcpNoDelay(new_fd);
    conn_info.peer_ip = SocketConnHelper::ExtractIpFromSockAddr(address);
    conn_info.create_time = std::chrono::system_clock::now();
    conn_info.type_fd = 1;
    return UBS_OK;
}

Result UrmaConnectorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    (void)new_fd;
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    if (urma_sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    UBSVersion local_version = UBS_PROTOCOL_VERSION;
    UrmaNegotiateReq req = {};
    req.peer_version = local_version.GetWhole();
    req.tp_type = TPT_CTP;
    req.tp_mode = TPM_RC;
    req.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    req.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;

    Result ret = SocketConnHelper::SendLengthPrefixed(raw_fd_, &req, sizeof(req), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        return ret;
    }

    uint32_t negotiated_version = 0;
    ssize_t n = SocketConnHelper::RecvSocketData(raw_fd_, &negotiated_version, sizeof(negotiated_version),
                                                 NEGOTIATE_TIMEOUT_MS);
    if (n != static_cast<ssize_t>(sizeof(negotiated_version))) {
        return UBS_TCP_EXCHANGE;
    }

    UBSVersion negotiated_ver(negotiated_version);
    VersionCheckResult ver_result = negotiated_ver.ValidateNegotiated(local_version);
    if (ver_result != VersionCheckResult::kCompatible) {
        return UBS_TCP_EXCHANGE;
    }
    urma_sock->SetNegotiatedVersion(negotiated_version);

    UrmaNegotiateRsp rsp = {};
    ret = SocketConnHelper::RecvLengthPrefixed(raw_fd_, &rsp, sizeof(rsp), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        return ret;
    }

    if (rsp.ret_code != 0) {
        return UBS_TCP_EXCHANGE;
    }

    urma_sock->SetPeerEid(rsp.local_eid);

    return UBS_OK;
}

Result UrmaConnectorOps::CreateSocketResources(SocketPtr &sock)
{
    UrmaSocketPtr urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    if (urma_sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    return urma_sock->ExchangeSocketResources(raw_fd_, *urma_sock.Get());
}

void UrmaConnectorOps::DestroySocketResources() {}

} // namespace urma
} // namespace ubs
} // namespace ock
