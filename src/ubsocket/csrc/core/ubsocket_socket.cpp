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
#include "ubsocket_socket.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"
#include "umq/umq_data_rx_ops.h"
#include "umq/umq_data_tx_ops.h"
#include "umq/umq_socket.h"
#include "umq/umq_socket_acceptor.h"
#include "umq/umq_socket_connector.h"

namespace ock {
namespace ubs {
Result SocketBase::Create(int fd, ock::ubs::SocketType t, SocketPtr &outSocket)
{
    if (t == SocketType::SOCK_TYPE_UMQ) {
        /* step1: create umq socket */
        using namespace umq;
        auto umqSock = MakeRef<UmqSocket>(fd);
        if (umqSock == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* step2: do initialize */
        auto result = umqSock->Initialize();
        if (result != UBS_OK) {
            return result;
        }

        auto sock = RefConvert<UmqSocket, Socket>(umqSock);
        auto sockBase = RefConvert<UmqSocket, SocketBase>(umqSock);

        AcceptorOps *acceptorOps = nullptr;
        result = CreateAcceptorOps(t, sock, acceptorOps);
        if (result != UBS_OK) {
            return result;
        }

        ConnectorOps *connectorOps = nullptr;
        result = CreateConnectorOps(t, sock, connectorOps);
        if (result != UBS_OK) {
            delete acceptorOps;
            return result;
        }

        sockBase->acceptor_ = new Acceptor(sock, acceptorOps);
        sockBase->connector_ = new Connector(sock, connectorOps);

        /* step6: start epoll runner */
        result = EpollRunnerFactory::GetInstance(SocketType::SOCK_TYPE_UMQ).Start();
        if (result != UBS_OK) {
            return result;
        }
        // TODO：资源回收时进行销毁

        /* assign out */
        outSocket = sock;

        return UBS_OK;
    } else {
        return UBS_INVALID_PARAM;
    }
} // namespace ubs

Result SocketBase::GenerateSocketCommOps(const SocketPtr &sock)
{
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    DataTxOps *txOps = nullptr;
    Result result = CreateTxOps(sock->type_, sock, txOps);
    if (result != UBS_OK) {
        return result;
    }

    DataRxOps *rxOps = nullptr;
    result = CreateRxOps(sock->type_, sock, rxOps);
    if (result != UBS_OK) {
        delete txOps;
        return result;
    }

    sockBase->tx_ = DataTx(sock, txOps);
    sockBase->rx_ = DataRx(sock, rxOps);
    return UBS_OK;
}

Result SocketBase::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOps *&ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SocketType::SOCK_TYPE_UMQ) {
        using namespace umq;
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq ops */
        auto umqOps = new (std::nothrow) UmqTxOps(umqSock->raw_socket_, umqSock->UmqHandle());
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        ops = umqOps;
        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}

Result SocketBase::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOps *&ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SocketType::SOCK_TYPE_UMQ) {
        using namespace umq;
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq ops */
        auto umqOps = new (std::nothrow) UmqRxOps(umqSock->raw_socket_, umqSock->UmqHandle());
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        ops = umqOps;
        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}
Result SocketBase::CreateAcceptorOps(SocketType value, const SocketPtr &sock, AcceptorOps *&acceptorOps)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SocketType::SOCK_TYPE_UMQ) {
        using namespace umq;
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq acceptor */
        auto umqOps = new (std::nothrow) UmqAcceptorOps(sock->raw_socket_);
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        acceptorOps = umqOps;
        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}

Result SocketBase::CreateConnectorOps(SocketType value, const SocketPtr &sock, ConnectorOps *&connectorOps)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SocketType::SOCK_TYPE_UMQ) {
        using namespace umq;
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq acceptor */
        auto umqOps = new (std::nothrow) UmqConnectorOps(sock->raw_socket_);
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        connectorOps = umqOps;

        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}

} // namespace ubs
} // namespace ock