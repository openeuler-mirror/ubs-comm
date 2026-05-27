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

private:
    // ======================== 建链辅助方法 ========================
    Result BuildNegotiateReq(NegotiateReq *req, const UmqSocketPtr &umq_socket);
    Result ConnectNegotiate(const UmqSocketPtr &umq_socket);
    Result ConnectExchangeSocketIDs(void);
    Result GetDevRouteList(const umq_eid_t *src_eid, const umq_eid_t *dst_eid, umq_route_list_t &filtered_list);
    Result DoRoute(const umq_eid_t *src_eid, const umq_eid_t *dst_eid);
    Result DoUbConnect(const UmqSocketPtr &umq_socket, umq_eid_t &conn_eid, umq_used_ports_t &used_ports);
    Result DoUbConnectRetry(SocketPtr socketPtr, Result &ack_ret, Result &peer_ret);
    Result CheckOtherRoute(const UmqSocketPtr &umq_socket);
    Result CheckOtherRouteForClos(const UmqSocketPtr &umq_socket);
    Result CheckRouteDevAddForConnect(const umq_eid_t &conn_eid, const UmqSocketPtr &umq_socket);

    Result GetRoundRobinConnEid(umq_route_list_t &route_list, const umq_eid_t *dst_eid);
    void GetBondingEidMapIndex(const umq_eid_t &dst_eid, uint32_t &index);
    uint32_t GetTargetChipId(const std::vector<uint32_t> &socket_ids, const std::vector<uint32_t> &chip_id_list,
                             int processSocketId);
    Result GetConnEid(umq_route_list_t &route_list, const umq_eid_t *dst_eid);
    void RRChooseMainRoute(std::vector<umq_route_t> &main_routes, const umq_eid_t *dst_eid,
                           umq_route_t &conn_main_route, umq_route_t &conn_back_route);
    Result GetCpuAffinityUmqRoute(umq_route_list_t &route_list, std::vector<umq_route_t> &main_routes,
                                  std::vector<umq_route_t> &back_routes);
    Result ConnectViaHandshakeOpt(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    Result ConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    void PrintSocketsInfo();

    // ======================== 成员变量 ===========================
    struct UmqConnInfo : public ConnInfo {
        umq_eid_t peer_eid{}; // 对端EID
        umq_eid_t conn_eid{}; // 本端EID
    };
    UmqConnInfo umq_conn_info_;
    bool use_round_robin_ = true;
    int peer_socket_id_ = -1;                   // 对端socket id
    std::vector<uint32_t> peer_all_socket_ids_; // 对端所有socket id
    umq_route_t conn_route_;
    umq_route_t back_route_;
    // TODO: 主备切换逻辑待优化
    std::vector<umq_route_t> back_route_list_;
    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
    // retry & degrade
    bool degradable_ = false;
    OtherRouteMessage other_route_message_;
    umq_route_t other_conn_route;
    umq_route_t other_back_conn_route;
    UBHandshakeState retry_state_ = UBHandshakeState::kSTART;
};
using UmqConnectorOpsPtr = Ref<UmqConnectorOps>;

} // namespace umq
} // namespace ubs
} // namespace ock
#endif
