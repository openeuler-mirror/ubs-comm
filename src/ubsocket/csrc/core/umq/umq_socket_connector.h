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
#include "umq_setting.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {

// 基于 umq 的 connector 实现层
class UmqConnectorOps : public ConnectorOps {
public:
    UmqConnectorOps() = default;
    ~UmqConnectorOps() = default;

    Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                          const SocketPtr &sock) override;
    Result Negotiate(int new_fd, const SocketPtr &sock) override;
    Result CreateSocketResources(int new_fd, const SocketPtr &sock) override;
    void DestroySocketResources() override;

private:
    // ======================== 建链辅助方法 ========================
    Result BuildNegotiateReq(NegotiateReq *req, const UmqSocketPtr &umq_socket);
    Result FillLocalSocketIdsForNegotiate(const UmqSocketPtr &umq_socket, uint32_t *socket_ids,
                                          uint32_t &socket_id_count);
    Result ConnectNegotiate(const UmqSocketPtr &umq_socket);
    Result ConnectExchangeSocketIDs(void);
    Result DoRoute(const umq_eid_t *src_eid, const umq_eid_t *dst_eid, umq_route_t *conn_route, bool use_round_robin,
                   umq_route_t *back_route);
    Result DoUbConnect(const UmqSocketPtr &umq_socket, umq_used_ports_t &used_ports);

    // ======================== 成员变量 ===========================
    struct UmqConnInfo : public ConnInfo {
        umq_eid_t peer_eid{}; // 对端EID
        umq_eid_t conn_eid{}; // 本端EID
    };
    UmqConnInfo umq_conn_info_;
    bool use_round_robin_ = true;
    int server_socket_id_for_affinity_ = -1;
    std::vector<uint32_t> peer_all_socket_ids_;
    umq_eid_t route_backup_src_eid_; // 备
    umq_route_t conn_route_;
    umq_route_t back_route_;
    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
};
using UmqConnectorOpsPtr = Ref<UmqConnectorOps>;

} // namespace umq
} // namespace ubs
} // namespace ock
#endif
