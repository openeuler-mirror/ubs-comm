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
    (void)protocol_negotiation;
    (void)protocol_negotiation_recv_size;
    /* 非 TFO 模式：urma 控制面协商报文无 magic，无需校验 */
    if (GlobalSetting::UBS_HAND_SHAKE_MODE != UBHandshakeMode::TFO) {
        return 0;
    }
    /* TFO 模式：客户端 BuildTfoPayload 在 SYN 中携带前导 magic(8B)+version(4B)，
     * 复用基类校验 magic(8B)；version(4B) 由 Negotiate 开头的 DoVersionNegotiate 消费 */
    return AcceptorOps::ValidateProtocol(fd, protocol_negotiation, protocol_negotiation_recv_size);
}

uint32_t NegotiateConnFlag(uint32_t peer_flag)
{
    uint32_t ret_flag = 0;
    ret_flag |= GlobalSetting::UBS_ENABLE_DEGRADE ? (peer_flag & URMA_SOCKET_MASK_DEGRADE) : 0;
    ret_flag |= GlobalSetting::UBS_BACKUP_LINK_ENABLED ? (peer_flag & URMA_SOCKET_MASK_LINK_SWITCH) : 0;
    return ret_flag;
}

void UrmaAcceptorOps::CleanStaleSock(uint64_t stale_conn_id, Socket *cur_sock)
{
    ArraySet<Socket>::GetInstance().ForEach([&](int stale_fd, Socket *stale_sock) {
        if (stale_sock == nullptr || stale_sock->Type() != SocketType::SOCK_TYPE_URMA ||
            stale_sock->create_type_ != SOCK_CREATE_TYPE_ACCEPT || stale_sock == cur_sock || stale_fd == cur_sock->Fd()) {
            return;
        }
        auto *stale_urma = RefStaticCastToRaw<UrmaSocket, Socket>(stale_sock);
        if (stale_urma->GetConnId() != stale_conn_id ||
            stale_urma->State() == SocketState::SOCK_STAT_BROKEN) {
            return;
        }
        stale_urma->State(SocketState::SOCK_STAT_BROKEN);
        LibcApi::shutdown(stale_fd, SHUT_RD);
        stale_urma->NotifySocketReadable();
        UBS_VLOG_TRACE("negotiate fd=%d, stale conn_id=%lu cleaned, stale fd=%d", fd,
                       (unsigned long)stale_conn_id, stale_fd);
    });
}

Result UrmaAcceptorOps::Negotiate(SocketPtr socketPtr)
{
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(socketPtr);
    if (urma_sock == nullptr) {
        UBS_VLOG_ERR("negotiate fd=%d, socket not urma-socket.", fd);
        return UBS_INVALID_PARAM;
    }

    /* TFO 模式：消费 SYN 前导中的 version(4B) 并完成版本协商，与 umq 基类链路同构；
     * Major mismatch 时由 DoVersionNegotiate 通知对端并丢弃后续 body，实现早期降级 */
    bool is_tfo = GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::TFO;
    uint32_t tfo_negotiated_version = 0;
    uint32_t tfo_peer_version = 0;
    if (is_tfo) {
        Result ver_ret = DoVersionNegotiate(fd, tfo_negotiated_version, tfo_peer_version);
        if (ver_ret != UBS_OK) {
            return ver_ret;
        }
    }

    UrmaNegotiateReq req = {};
    Result ret = SocketConnHelper::RecvLengthPrefixed(fd, &req, sizeof(req), NEGOTIATE_ACCEPT_TIMEOUT_MS);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("negotiate fd=%d, recv req of size=%zu failed: %d", fd, sizeof(req), ret);
        return ret;
    }

    uint32_t negotiated_version = 0;
    if (is_tfo) {
        /* TFO：版本协商已在 DoVersionNegotiate 完成（peer_version 取自 SYN 前导），
         * req.peer_version 仅保留解析，不再用于协商 */
        negotiated_version = tfo_negotiated_version;
        urma_sock->SetPeerVersion(tfo_peer_version);
    } else {
        UBSVersion local_version = UBS_PROTOCOL_VERSION;
        UBSVersion peer_version(req.peer_version);
        UBSVersion negotiated;
        VersionCheckResult ver_result = local_version.Negotiate(peer_version, negotiated);
        negotiated_version = negotiated.GetWhole();

        if (ver_result == VersionCheckResult::kMajorMismatch) {
            UBS_VLOG_ERR("negotiate fd=%d, ValidateNegotiated negotiated_version failed: %d", fd, ver_result);
            uint32_t mismatch_version = local_version.GetWhole();
            SocketConnHelper::SendSocketData(fd, &mismatch_version, sizeof(mismatch_version), NEGOTIATE_ACCEPT_TIMEOUT_MS);
            return UBS_TCP_EXCHANGE;
        }

        urma_sock->SetPeerVersion(peer_version.GetWhole());
    }

    urma_sock->SetTransMode(std::min(req.trans_mode, UrmaSetting::UB_TANS_MODE));
    urma_sock->SetSocketFlags(NegotiateConnFlag(req.conn_flag));
    urma_sock->SetConnId(UrmaBackend::Instance().AllocConnId());

    if (req.last_conn_id != 0) {
        CleanStaleSock(req.last_conn_id, urma_sock);
    }

    /* match peer context health list against local contexts, select first healthy pair */
    UrmaNegotiateRsp rsp = {};
    int32_t selected_index = -1;
    auto nego_ctx = UrmaBackend::Instance().GetContextPoll()->SelectContext(
        UrmaPortSelectionPolicy::PSP_ROUND_ROBIN, &selected_index, req.context_health, req.context_count);
    if (nego_ctx != nullptr) {
        urma_sock->context_ = nego_ctx;
        UBS_VLOG_TRACE("negotiate fd=%d, selected context index=%d", fd, selected_index);
    }
    rsp.ret_code = 0;
    rsp.negotiated_version = negotiated_version;
    rsp.trans_mode = urma_sock->trans_mode;
    rsp.send_buf_kb = URMA_SEND_BUF_DEFAULT_KB;
    rsp.recv_buf_kb = URMA_RECV_BUF_DEFAULT_KB;
    rsp.conn_flag = urma_sock->socket_flags;
    rsp.selected_context_index = selected_index;
    rsp.conn_id = urma_sock->GetConnId();

    ret = SocketConnHelper::SendLengthPrefixed(fd, &rsp, sizeof(rsp), NEGOTIATE_ACCEPT_TIMEOUT_MS);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("negotiate fd=%d, send length prefixed for rsp(%zu: code=%u) buff_size_kb(%u, %u) failed: %d", fd,
                     sizeof(rsp), rsp.ret_code, rsp.send_buf_kb, rsp.recv_buf_kb, ret);
        return ret;
    }
    if (nego_ctx == nullptr) {
        UBS_VLOG_WARN("negotiate fd=%d, has no context valid: %d", fd, rsp.selected_context_index);
        return UBS_TCP_EXCHANGE | SocketMaskToErrnoFlag(urma_sock->socket_flags);
    }
    UBS_VLOG_TRACE("negotiate fd=%d, buf_size(%u, %u) eid(%lx, %lx) sel_ctx=%d", fd, rsp.send_buf_kb, rsp.recv_buf_kb,
                   urma_sock->context_->EidInfo().eid.in6.subnet_prefix, urma_sock->context_->EidInfo().eid.in6.interface_id, selected_index);

    return UBS_OK;
}

Result UrmaAcceptorOps::CreateSocketResources(SocketPtr socketPtr)
{
    UrmaSocket *urma_sock = RefStaticCastToRaw<UrmaSocket>(socketPtr);
    if (urma_sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    auto res = urma_sock->GenerateSocketCommOps(socketPtr);
    if (UNLIKELY(res != UBS_OK)) {
        UBS_VLOG_ERR("GenerateSocketCommOps failed: %d", res);
        return res;
    }

    return urma_sock->ExchangeSocketResources();
}

void UrmaAcceptorOps::DestroySocketResources() {}

} // namespace urma
} // namespace ubs
} // namespace ock
