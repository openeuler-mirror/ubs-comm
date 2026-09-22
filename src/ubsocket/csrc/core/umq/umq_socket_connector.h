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
#ifndef UBS_COMM_UMQ_SOCKET_CONNECTOR_H
#define UBS_COMM_UMQ_SOCKET_CONNECTOR_H

#include "core/ubsocket_socket_connector.h"
#include "core/umq/umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

// 基于 umq 的 connector 实现层
class UmqConnectorOps : public ConnectorOps {
public:
    UmqConnectorOps(int fd)
    {
        raw_fd_ = fd;
    }
    ~UmqConnectorOps() = default;

    Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                          const SocketPtr &sock) override;
    Result Negotiate(int new_fd, const SocketPtr &sock) override;
    Result CreateSocketResources(const SocketPtr &sock) override;
    void DestroySocketResources() override;

    struct UmqConnInfo : public ConnInfo {
        umq_eid_t peer_eid{};         // 对端 EID
        umq_eid_t peer_bonding_eid{}; // 对端 bonding 设备 EID
        umq_eid_t bonding_eid{};      // 本端 bonding 设备 EID
        umq_eid_t conn_eid{};         // 本端 EID
    };
    UmqConnInfo umq_conn_info_;

    /* 交叉 ack（NEGO_CAP_EARLY_ACK）下客户端的降级共识合成。
     * 经典路径由服务端把 "客户端可降级失败 && 服务端允许降级" 回声(echo)进应答；
     * 交叉路径应答不含回声，客户端用协商轮下发的 consent 位本地合成同一结果。
     * 两者对全部组合逐一等价（见 umq_socket_connector_test.cpp 的 8 组合等价性测试）。 */
    static bool ClientDegradableVerdict(Result peer_ret, Result ack_ret, bool peer_early_ack, bool peer_degrade_consent)
    {
        return IsDegradable(peer_ret) || (peer_early_ack && peer_degrade_consent && IsDegradable(ack_ret));
    }

private:
    // ======================== 建链辅助方法 ========================
    Result BuildNegotiateReq(NegotiateReq *req, const UmqSocketPtr &umq_socket);
    Result BuildNegotiateReqBuffer(uint8_t *buf, const UmqSocketPtr &umq_socket, int &buf_len);
    Result ConnectNegotiate(const UmqSocketPtr &umq_socket);
    Result DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports, bool umq_precreated = false);
    Result DoUbConnectRetry(SocketPtr socketPtr, Result &ack_ret, Result &peer_ret);
    Result CheckRouteDevAddForConnect(const umq_eid_t &conn_eid, const UmqSocketPtr &umq_socket);
    void TryPrecreateLocalUmq(const UmqSocketPtr &umq_socket);
    void DiscardPrecreatedUmq(const UmqSocketPtr &umq_socket);

    uint32_t GetTargetChipId(const std::vector<uint32_t> &socket_ids, const std::vector<uint32_t> &chip_id_list,
                             int processSocketId);
    Result ConnectViaHandshakeOpt(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    Result ConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    void PrintSocketsInfo();

    // ======================== 成员变量 ===========================
    bool use_round_robin_ = true;
    int peer_socket_id_ = -1;                   // 对端socket id
    std::vector<uint32_t> peer_all_socket_ids_; // 对端所有socket id
    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
    // retry & degrade
    bool degradable_ = false;
    // 协商等待期预建本地 umq（见 TryPrecreateLocalUmq；仅首轮 kSTART 消费）
    bool precreate_done_ = false;
    // 交叉 ack 能力（协商轮从 NegotiateRsp.reserved[0] 解析）
    bool peer_early_ack_ = false;
    // 方案A'：NegotiateRsp 尾部携带的服务端 bind_info（len==0 = 未携带/回退经典 CpMsg 轮）
    uint64_t peer_bind_info_len_ = 0;
    uint8_t peer_bind_info_[UMQ_BIND_INFO_SIZE_MAX] = {};
    /* 方案B：发起连接前预建 umq 并暂存本端 bind_info（挂 NegotiateReq 尾部）。
     * len==0 表示本轮不携带（预建失败/开关关/BONDING_ROUTE）。 */
    uint64_t local_bind_info_len_ = 0;
    uint8_t local_bind_info_[UMQ_BIND_INFO_SIZE_MAX] = {};
    /* 方案B：服务端确认消费了请求携带的 bind_info ⇒ 应答已含其 bind 结果（腿⑥），
     * ack 轮免收；一次性消费，重试轮回退经典。 */
    bool peer_req_carry_consumed_ = false;
    Result peer_carried_ack_ = UBS_OK;
    /* 并行 bind：服务端确认推迟 ⇒ 应答不含 bind 结果，末尾 ack 轮照收腿⑥（一次性消费） */
    bool peer_ack_deferred_ = false;
    bool peer_degrade_consent_ = false;
    OtherRouteMessage other_route_message_;
    UBHandshakeState retry_state_ = UBHandshakeState::kSTART;
};
using UmqConnectorOpsPtr = Ref<UmqConnectorOps>;

} // namespace umq
} // namespace ubs
} // namespace ock
#endif
