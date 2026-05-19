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

#include "umq_socket_acceptor.h"

namespace ock {
namespace ubs {
namespace umq {

int UmqAcceptorOps::PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                   const SocketPtr &sock)
{
    return 0;
}

Result UmqAcceptorOps::Negotiate(int new_fd, const SocketPtr &sock)
{
    return UBS_OK;
};

Result UmqAcceptorOps::CreateSocketResources(int new_fd, SocketPtr &sock)
{
#ifdef ENABLED
    // 1. 用户直接指定普通设备建链，失败不重试、可降级
    // 2. 用户指定 bonding 设备建链，但如果是节点内回环场景，失败不重试、可降级
    // 3. 用户指定 bonding 设备建链，跨节点场景返回 retryable 错误
    //   - 优先重试，如果重试过程中失败则降级
    //   - 如果无法重试，则尝试降级
    //   - 如果无法降级，则返回失败
    Result ackRet = UBS_OK;
    UmqSocket *umq_socket_fd_obj = static_cast<UmqSocket *>(socket_fd_obj);
    auto connEid = umq_socket_fd_obj->umq_acceptor_ops_->UMQConnInfo.conn_eid;
    UBHandshakeState state = UBHandshakeState::kSTART;
    umq_used_ports_t mUsedPorts = {.port = nullptr, .num = 0};
    while (!ok) {
        switch (state) {
            case UBHandshakeState::kOK:
                ok = true;
                break;

            case UBHandshakeState::kSTART:
                ackRet = DoUbAccept(new_fd, connEid, umq_socket_fd_obj, mUsedPorts);
                break;

            case UBHandshakeState::kRETRY:
                ackRet = ackRet | DoUbAccept(new_fd, connEid, umq_socket_fd_obj, mUsedPorts);
                break;

            case UBHandshakeState::kRETRY_FAILED_CHECK_OTHER_ROUTE:
                break;

            case UBHandshakeState::kDEGRADE:
                return UBS_UB_ACCEPT;

            case UBHandshakeState::kFAILED:
                return UBS_UB_ACCEPT;
        }
    }
#endif
    return UBS_OK;
};

// TODO: mUsedPorts 和 connEid 待收编到成员变量
Result UmqAcceptorOps::DoUbAccept(int new_fd, umq_eid_t &connEid, const SocketPtr &sock, umq_used_ports_t &mUsedPorts)
{
#ifdef ENALBED
    Result ret = UBS_OK;
    umq_topo_type_t acceptTopoType = umq_socket_fd_obj->GetTopoType();
    CpMsg local_cp_msg;
    CpMsg remote_cp_msg;
    Result ret;

    ret = CreateLocalUmq(&connEid, mUsedPorts);

    // 校验 bind 是否成功
    // ...

    // umq_bind

    // 1650 RC mode not support post rx right after create jetty, thus, move post rx operation after bind()
    if (PrefillRx() != 0) {
        return UBS_PREFILL_RX;
    }
#endif
    return UBS_OK;
}

void UmqAcceptorOps::DestroySocketResources() {}

void UmqAcceptorOps::SetConnInfo(std::string peer_ip, int peer_fd, int type_fd) {}

int UmqAcceptorOps::ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size)
{
    return 0;
}
} // namespace umq
} // namespace ubs
} // namespace ock
