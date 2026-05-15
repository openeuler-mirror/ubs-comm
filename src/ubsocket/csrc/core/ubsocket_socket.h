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
#ifndef UBS_COMM_UBSOCKET_CONNECTION_H
#define UBS_COMM_UBSOCKET_CONNECTION_H

#include <sys/socket.h>
#include <chrono>

#include "dl_libc_api.h"
#include "ubsocket_common_includes.h"
#include "ubsocket_core_types.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_socket_acceptor.h"
#include "ubsocket_socket_connector.h"

namespace ock {
namespace ubs {
class Socket;
using SocketPtr = Ref<Socket>;

class Socket : public SocketInfo {
public:
    static Result Create(SocketType t, SocketPtr &sock);
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops);
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops);
    static Result CreateAcceptor(SocketType value, const SocketPtr &sock, Acceptor *&acceptor);
    static Result CreateConnector(SocketType value, const SocketPtr &sock, Connector *&connector);

public:
    Socket() = default;
    ~Socket() override = default;

    virtual Result Initialize() noexcept = 0;
    virtual void UnInitialize() noexcept = 0;

    int Accept(const SocketInfo &sock, struct sockaddr *address, socklen_t *address_len);
    int Connect(const SocketInfo &sock, struct sockaddr *address, socklen_t *address_len);

protected:
    DataTx tx_;                      /* take charge of send */
    DataRx rx_;                      /* take charge of receive */
    Acceptor *acceptor_ = nullptr;   /* acceptor of ubsocket */
    Connector *connector_ = nullptr; /* connector of ubsocket */

    friend class DataTx;
    friend class DataRx;
    friend class Acceptor;
    friend class Connector;
};

ALWAYS_INLINE int Socket::Accept(const SocketInfo &sock, struct sockaddr *address, socklen_t *address_len)
{
    if (acceptor_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return acceptor_->Accept(sock, address, address_len);
}

ALWAYS_INLINE int Socket::Connect(const SocketInfo &sock, struct sockaddr *address, socklen_t *address_len)
{
    if (connector_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return connector_->Connect(sock, address, address_len);
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
