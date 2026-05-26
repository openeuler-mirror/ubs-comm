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
#include "umq/umq_socket_connector.h"

namespace ock {
namespace ubs {
// ======================== 基础方法 ========================
int Connector::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    PROF_START(CORE_CONNECT);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    Result ret = 0;
    bool is_blocking = SocketConnHelper::IsBlocking(raw_fd_);
    if (connector_ops_ != nullptr) {
        ret = connector_ops_->PrepareConnect(raw_fd_, address, address_len, sock);
        if (ret != 0) {
            return ret;
        }

        ret = connector_ops_->Negotiate(raw_fd_, sock);
        if (ret != UBS_OK) {
            return -1;
        }

        ret = connector_ops_->CreateSocketResources(raw_fd_, sock);
        if (ret != UBS_OK) {
            return -1;
        }
    }
    if (ret != 0) {
        // Log
        SocketSet::Instance().RemoveSocket(raw_fd_);
        /* Clear messages that already exist on the TCP link to prevent 
                 * dirty messages from affecting user data transmission*/
        SocketConnHelper::FlushSocketMsg(raw_fd_);
    }

    if (is_blocking) {
        SocketConnHelper::SetBlocking(raw_fd_);
    }
    // m_peer_info.type_fd = 1;
    PROF_END(CORE_CONNECT, true);
    return ret;
}

Connector::~Connector() {}
} // namespace ubs
} // namespace ock
