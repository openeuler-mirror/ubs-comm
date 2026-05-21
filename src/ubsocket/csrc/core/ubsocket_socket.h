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

#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_event_epoll.h"
#include "ubsocket_socket_acceptor.h"
#include "ubsocket_socket_connector.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {
class SocketBase;
using SocketBasePtr = Ref<SocketBase>;

class SocketBase : public Socket {
public:
    static Result Create(int fd, SocketType t, SocketPtr &sock);

    static Result GenerateSocketCommOps(const SocketPtr &sock);
public:
    SocketBase(int fd, SocketType type) : Socket(fd, type) {}
    ~SocketBase() override = default;

    virtual Result Initialize() noexcept = 0;
    virtual void UnInitialize() noexcept = 0;

    int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len);
    int Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len);
    int WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);
    int ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

protected:
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops);
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops);
    static Result CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptor);
    static Result CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connector);

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

ALWAYS_INLINE int SocketBase::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    if (acceptor_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return acceptor_->Accept(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    if (connector_ == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    return connector_->Connect(sock, address, address_len);
}

ALWAYS_INLINE int SocketBase::WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    return tx_.WriteV(sock, iov, iovcnt);
}

ALWAYS_INLINE int SocketBase::ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt)
{
    return rx_.ReadV(sock, iov, iovcnt);
}


} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
