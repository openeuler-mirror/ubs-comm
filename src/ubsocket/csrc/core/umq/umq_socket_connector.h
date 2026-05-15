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

#include "ubsocket_socket_connector.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

// 基于 umq 的 connector 实现层
class UmqConnectorOps : public ConnectorOps {
public:
    UmqConnectorOps() = default;
    ~UmqConnectorOps() = default;
};
using UmqConnectorOpsPtr = Ref<UmqConnectorOps>;

} // namespace umq
} // namespace ubs
} // namespace ock
#endif
