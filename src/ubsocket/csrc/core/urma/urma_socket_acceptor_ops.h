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
#ifndef UBS_COMM_URMA_ACCEPTOR_OPS_H
#define UBS_COMM_URMA_ACCEPTOR_OPS_H

#include "core/ubsocket_socket_acceptor.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaAcceptorOps : public AcceptorOps {
public:
    explicit UrmaAcceptorOps(int listen_fd);
    ~UrmaAcceptorOps() override;

    Result PrepareConnect(int new_fd) override;
    Result Negotiate(SocketPtr socketPtr) override;
    Result CreateSocketResources(SocketPtr socketPtr) override;
    void DestroySocketResources() override;
    int ValidateProtocol(int fd, uint64_t &protocol_negotiation, ssize_t &protocol_negotiation_recv_size) override;
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_ACCEPTOR_OPS_H
