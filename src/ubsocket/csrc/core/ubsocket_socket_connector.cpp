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

#include "ubsocket_socket_connector.h"
#include "ubsocket_socket_set.h"

namespace ock {
namespace ubs {
// ======================== 基础方法 ========================
int Connector::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t *address_len)
{
#ifdef ENABLED
    auto sock_obj = reinterpret_cast<Socket *>(sock);
    raw_fd_ = sock_obj->raw_socket_;
    // BuildNegotiateReq + sendto(req)
    // IsBlocking
    // IsTfoConnection
#endif
    return 0;
}

Connector::Connector() {}
Connector::~Connector() {}
} // namespace ubs
} // namespace ock
