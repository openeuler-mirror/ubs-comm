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
    VersionCheckResult ValidateVersion(int fd, uint32_t &negotiated_version, uint32_t &peer_version);

    // ======================== 成员变量 ===========================
    struct UmqConnInfo : public ConnInfo {
        umq_eid_t peer_eid{};         // 对端EID
        umq_eid_t peer_bonding_eid{}; // 对端 bonding 设备 EID
        umq_eid_t bonding_eid{};      // 本端 bonding 设备 EID
        umq_eid_t conn_eid{};         // 本端EID
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
    Result AcceptNegotiate(SocketPtr socketPtr);
    Result DoUbAccept(SocketPtr socketPtr, umq_used_ports_t &mUsedPorts, bool early_prepared = false);
    /* DoUbAccept 的后半段（cooldown 校验 + umq_bind + prefill/注册收尾），供
     * 方案B 在协商应答前提前执行；info/info_len 为对端 bind_info。 */
    Result BindPeerAndFinalize(SocketPtr socketPtr, umq_used_ports_t &used_ports, uint8_t *info,
                               uint64_t info_len);
    /* 方案B/并行 bind 的提前 bind：按当前 used_ports 调 BindPeerAndFinalize，结果经降级
     * 开关掩码后存入 early_ack_ret_ 并置 early_bound_。 */
    void BindPeerEarly(SocketPtr socketPtr, NegotiateReqExt &req_ext);
    Result DoUbAcceptRetry(SocketPtr socketPtr, Result &ackRet, Result &peerRet);
    Result AcceptExchangeSocketIDs(const SocketPtr &socketPtr, int fd);
    Result FillLocalSocketIdsForNegotiate(uint32_t *socket_ids, uint32_t &socket_id_count);
    Result CheckRouteDevAddForAccept(const umq_eid_t &conn_eid, const UmqSocketPtr &sk);
    void BuildNegotiateRsp(NegotiateRsp &rsp);
    /* 方案A'：协商应答前提前完成路检 + 本地 umq 创建 + bind_info 提取（携带进
     * 应答尾部）。失败时半成品自清并返回非 OK——调用方发经典应答，后续
     * DoUbAccept 走原路径重新创建，语义零变化。 */
    Result PrepareLocalUmqEarly(SocketPtr socketPtr, NegotiateRspExt &ext);

    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;

    // 方案A'（NEGO_CAP_CARRY_BINDINFO）：对端能力位 + 本轮已提前建好本地 umq（一次性消费）
    bool peer_nego_carry_ = false;
    bool early_prepared_ = false;
    /* 方案B：create+bind+腿⑥ 已在协商应答前完成并随应答送出；kSTART 一次性消费。 */
    bool early_bound_ = false;
    Result early_ack_ret_ = UBS_OK;
    /* 并行 bind：应答先于 bind 送出，bind 结果需在 kSTART 以交叉 ack 补发（一次性消费） */
    bool early_ack_deferred_ = false;

    // degrade & retry
    bool degradable_ = false;
    // 交叉 ack 能力（AcceptNegotiate 从 NegotiateReq.cap_flags 解析；仅 kSTART 轮使用）
    bool peer_early_ack_ = false;
    OtherRouteMessage other_route_message_;
    UBHandshakeState retry_state_ = UBHandshakeState::kSTART;
};
using UmqAcceptorOpsPtr = Ref<UmqAcceptorOps>;
} // namespace umq
} // namespace ubs
} // namespace ock
#endif
