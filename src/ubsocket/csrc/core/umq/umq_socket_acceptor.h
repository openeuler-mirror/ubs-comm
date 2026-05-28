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
#ifndef UBS_COMM_UMQ_SOCKET_ACCEPTOR
#define UBS_COMM_UMQ_SOCKET_ACCEPTOR

#include "core/ubsocket_socket_acceptor.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

// 基于 umq 的 accept 实现层
class UmqAcceptorOps : public AcceptorOps {
public:
    UmqAcceptorOps(int fd_)
    {
        fd = fd_;
    }
    ~UmqAcceptorOps() = default;

    Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                          const SocketPtr &sock) override;

    Result Negotiate(SocketPtr socketPtr) override;

    Result CreateSocketResources(SocketPtr socketPtr) override;

    void DestroySocketResources() override;

    // ======================== 建链辅助方法 ========================
    int ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size) override;

    // ======================== 成员变量 ===========================
    struct UmqConnInfo : public ConnInfo {
        umq_eid_t peer_eid{}; // 对端EID
        umq_eid_t conn_eid{}; // 本端EID
    };
    UmqConnInfo umq_conn_info_;

    // TODO: 考虑将 mPeerSocketId 和 mPeerAllSocketIds 迁移到 UMQConnInfo 中
    int peer_socket_id_ = -1;
    std::vector<uint32_t> peer_all_socket_ids_;

    // 协商结果
    ub_trans_mode umq_trans_mode_; // 协商后的传输模式
    bool umq_enable_share_jfr_{false};
    dev_schedule_policy umq_schedule_policy_{dev_schedule_policy::ROUND_ROBIN};
    dev_schedule_policy peer_schedule_policy_{dev_schedule_policy::ROUND_ROBIN};
    // 路由信息（bonding 场景）
    // umq_route_t umq_conn_route; // 主路由
    // umq_route_t umq_back_route; // 备路由

private:
    Result AcceptNegotiate(SocketPtr socketPtr, umq_eid_t &connEid, umq_eid_t &dstEid);
    Result DoUbAccept(SocketPtr socketPtr, umq_used_ports_t &mUsedPorts);
    Result DoUbAcceptRetry(SocketPtr socketPtr, Result &ackRet, Result &peerRet);
    Result AcceptExchangeSocketIDs(int fd);
    Result FillLocalSocketIdsForNegotiate(uint32_t *socket_ids, uint32_t &socket_id_count);
    void BuildNegotiateRsp(NegotiateRsp &rsp);

    umq_topo_type_t topo_type_;
    umq_eid_t conn_eid_;
    umq_eid_t peer_eid_;
    umq_route_t conn_route_;
    umq_route_t back_route_;

    // degrade & retry
    bool degradable_ = false;
    OtherRouteMessage other_route_message_;
    UBHandshakeState retry_state_ = UBHandshakeState::kSTART;
};
using UmqAcceptorOpsPtr = Ref<UmqAcceptorOps>;
} // namespace umq
} // namespace ubs
} // namespace ock
#endif
