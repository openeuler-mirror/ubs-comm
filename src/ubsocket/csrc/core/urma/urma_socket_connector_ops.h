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
#ifndef UBS_COMM_URMA_CONNECTOR_OPS_H
#define UBS_COMM_URMA_CONNECTOR_OPS_H

#include "core/ubsocket_socket_connector.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaConnectorOps : public ConnectorOps {
public:
    explicit UrmaConnectorOps(int fd);
    ~UrmaConnectorOps() override;

    Result PrepareConnect(int new_fd, const struct sockaddr *address, socklen_t address_len,
                          const SocketPtr &sock) override;
    Result Negotiate(int new_fd, const SocketPtr &sock) override;
    Result CreateSocketResources(SocketPtr &sock) override;
    void DestroySocketResources() override;

protected:
    Result BuildTfoPayload(uint8_t *buf, int &buf_len, const SocketPtr &sock) override;
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_CONNECTOR_OPS_H
