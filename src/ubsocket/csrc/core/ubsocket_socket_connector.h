/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_SOCKET_CONNECTOR_H
#define UBS_COMM_UBSOCKET_SOCKET_CONNECTOR_H

#include "../common/ubsocket_defines.h"
#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
/**
 * Connector ops for extension, urma/posix_shm etc
 */
class ConnectorOps {
public:
    virtual ~ConnectorOps() = default;

    // ======================== 主流程方法 ========================
    // 阶段0：准备连接( TCP 辅助建链, 包括 TFO 发送 等 DoConnect 和 DoAccept 的前置操作)
    virtual Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                                  const SocketPtr &sock) = 0;
    // 阶段1：协商信息
    virtual Result Negotiate(int new_fd, const SocketPtr &sock) = 0;
    // 阶段2：创建资源（例如：urma jetty/jfr 资源创建 + prefill rx）
    virtual Result CreateSocketResources(SocketPtr &sock) = 0;
    // 阶段3：销毁资源（握手失败/重试时清理已创建的资源）
    virtual void DestroySocketResources() = 0;

    DEFINE_REF_OPERATION_FUNC

public:
    RawConnInfoV4 conn_info;

protected:
    Result DoTcpConnect(int new_fd, const struct sockaddr *address, socklen_t address_len, const SocketPtr &sock);

    Result DoConnectViaHandshakeOpt(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);

    Result DoConnectViaTfo(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);

    virtual Result BuildTfoPayload(uint8_t *buf, int &buf_len, const SocketPtr &sock) = 0;

    int raw_fd_ = -1; // 传入 sock 的原生 socket fd
    DECLARE_REF_COUNT_VARIABLE;
};
using ConnectorOpsPtr = Ref<ConnectorOps>;

// connector 建链通用实现层：TCP 建链，协商，建链
class Connector {
public:
    Connector(const SocketPtr &sock, ConnectorOps *connectorOps)
        : raw_fd_(sock->raw_socket_),
          connector_ops_(connectorOps)
    {
    }
    ~Connector();

    int Connect(SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);

    ConnectorOpsPtr GetConnectorOps() const;

    bool IsClient() const noexcept;

private:
    int raw_fd_ = -1;   // 传入 sock 的原生 socket fd
    int event_fd_ = -1; // eventfd（通知上层可读）
    ConnectorOpsPtr connector_ops_ = nullptr;
};

ALWAYS_INLINE ConnectorOpsPtr Connector::GetConnectorOps() const
{
    return connector_ops_;
}
ALWAYS_INLINE bool Connector::IsClient() const noexcept
{
    return connector_ops_->conn_info.type_fd == 1 ? true : false;
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_CONNECTOR_H
