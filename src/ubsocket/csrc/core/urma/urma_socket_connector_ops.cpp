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
    Result ret = DoTcpConnect(new_fd, address, address_len, sock);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("urma socket connect tcp failed: %d", ret);
        return ret;
    }
    raw_fd_ = new_fd;
    SocketConnHelper::SetTcpNoDelay(new_fd);
    conn_info.peer_ip = SocketConnHelper::ExtractIpFromSockAddr(address);
    conn_info.create_time = std::chrono::system_clock::now();
    conn_info.type_fd = 1;
    UBS_VLOG_TRACE("prepare connect fd=%d, ip=%s, create_time=%lu", raw_fd_, conn_info.peer_ip.c_str(),
                   conn_info.create_time);
    return UBS_OK;
}

Result UrmaConnectorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    (void)new_fd;
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
    if (UNLIKELY(urma_sock == nullptr)) {
        UBS_VLOG_ERR("negotiate fd=%d, socket not urma-socket.");
        return UBS_INVALID_PARAM;
    }

    UBSVersion local_version = UBS_PROTOCOL_VERSION;
    UrmaNegotiateReq req = {};
    req.peer_version = local_version.GetWhole();
    req.trans_mode = UrmaSetting::UB_TANS_MODE;
    req.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    req.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;
    req.conn_flag |= GlobalSetting::UBS_ENABLE_DEGRADE ? UBS_DEGRADABLE_MASK : 0;
    req.conn_flag |= GlobalSetting::UBS_ENABLE_LINK_SWITCH ? UBS_RETRYABLE_MASK : 0;

    Result ret = SocketConnHelper::SendLengthPrefixed(raw_fd_, &req, sizeof(req), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("negotiate fd=%d, send length prefixed for req(%zu: ver=%u) buff_size_kb(%u, %u) failed: %d",
                     new_fd, sizeof(req), req.peer_version, req.send_buf_kb, req.recv_buf_kb, ret);
        return ret;
    }

    UrmaNegotiateRsp rsp = {};
    ret = SocketConnHelper::RecvLengthPrefixed(raw_fd_, &rsp, sizeof(rsp), NEGOTIATE_TIMEOUT_MS);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("negotiate fd=%d, recv rsp of size=%zu failed: %d", raw_fd_, sizeof(rsp), ret);
        return ret;
    }

    if (rsp.ret_code != 0) {
        UBS_VLOG_ERR("negotiate fd=%d, recv rsp ret code=%d", raw_fd_, rsp.ret_code);
        return UBS_TCP_EXCHANGE;
    }

    uint32_t negotiated_version = rsp.negotiated_version;
    UBSVersion negotiated_ver(negotiated_version);
    VersionCheckResult ver_result = negotiated_ver.ValidateNegotiated(local_version);
    if (ver_result != VersionCheckResult::kCompatible) {
        UBS_VLOG_ERR("negotiate fd=%d, ValidateNegotiated negotiated_version failed: %d", raw_fd_, ver_result);
        return UBS_TCP_EXCHANGE;
    }
    urma_sock->SetNegotiatedVersion(negotiated_version);
    urma_sock->SetTransMode(rsp.trans_mode);
    urma_sock->SetConnFlag(rsp.conn_flag);
    urma_sock->SetPeerEid(rsp.local_eid);
    UBS_VLOG_TRACE("negotiate fd=%d, buf_size(%u, %u) eid(%lx, %lx)", raw_fd_, rsp.send_buf_kb, rsp.recv_buf_kb,
                   rsp.local_eid.in6.subnet_prefix, rsp.local_eid.in6.interface_id);

    return UBS_OK;
}

Result UrmaConnectorOps::CreateSocketResources(SocketPtr &sock)
{
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(sock);
    if (urma_sock == nullptr) {
        UBS_VLOG_ERR("CreateSocketResources fd=%d, socket not urma-socket.");
        return UBS_INVALID_PARAM;
    }

    auto res = urma_sock->GenerateSocketCommOps(sock);
    if (UNLIKELY(res != UBS_OK)) {
        UBS_VLOG_ERR("GenerateSocketCommOps failed: %d", res);
        return res;
    }

    return urma_sock->ExchangeSocketResources();
}

void UrmaConnectorOps::DestroySocketResources() {}

} // namespace urma
} // namespace ubs
} // namespace ock
