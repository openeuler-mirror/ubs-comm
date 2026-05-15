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

#include "ubsocket_common_includes.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {

// accept 操作抽象层
class ConnectorOps {
public:
    virtual ~ConnectorOps() = default;

    // ======================== 主流程方法 ========================
    // 阶段1：协商信息
    virtual Result Negotiate(int new_fd, const SocketPtr &sock) = 0;
    // 阶段2：创建资源（例如：umq create + bind + prefill rx）
    virtual Result CreateSocketResources(int new_fd, SocketPtr &sock) = 0;
    // 阶段3：销毁资源（握手失败/重试时清理已创建的资源）
    virtual void DestroySocketResources() = 0;

    DEFINE_REF_OPERATION_FUNC
protected:
    DECLARE_REF_COUNT_VARIABLE;
};

// connector 建链通用实现层：TCP 建链，协商，建链
class Connector {
public:
    Connector();
    ~Connector();

    // ======================== 基础方法 ========================
    int Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t *address_len);

    // ======================== 成员变量 ========================
    std::shared_ptr<ConnectorOps> connector_ops_ = nullptr;
};

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_CONNECTOR_H
