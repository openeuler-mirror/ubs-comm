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
#include "ubsocket_tx_cqe_poller.h"
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

        SocketExt *ext = sockBase->EnsureExt();
        if (ext == nullptr) {
            delete acceptorOps;
            return UBS_MALLOC_FAILED;
        }
        auto *hs = new (std::nothrow) HandshakeCtx;
        if (hs == nullptr) {
            delete acceptorOps;
            return UBS_MALLOC_FAILED;
        }
        ext->hs = hs;
        hs->acceptor.Init(sock, acceptorOps);
        /*
         * connector 仅 connect 路径需要，accepted 链路终生不用（每链路 UmqConnectorOps
         * 约 0.3KB），改为 Connect() 首次调用时经 EnsureConnector 惰性创建。
         * acceptor 无法惰性创建：conn_info/type_fd/Negotiate 挂在 acceptor_ops_ 上，
         * accepted 与 connected 两种角色都会读取——但可以随 HandshakeCtx 在
         * OnEstablished 收尾时整体释放（监听/TCP 链路保留到析构）。
         */

        /* Start the background TX CQE poller for all TP modes: in POOL mode
         * it is the only TX CQE drain channel for UB-native mode. */
        result = TxCqePoller::Instance().Start();

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
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sock->type_ != SocketType::SOCK_TYPE_UMQ) {
        /* invalid type（与原 CreateTxOps/CreateRxOps 对非 UMQ 类型的行为一致） */
        return UBS_INVALID_PARAM;
    }

    /* Rx/Tx ops 按值内嵌于 UmqSocket：此处仅重建并接线，无堆分配。
     * 重协商路径重复进入时，placement-new 重建等价于旧实现的 ops 替换。 */
    umq::UmqSocketPtr umqSock = RefConvert<Socket, umq::UmqSocket>(sock);
    DataTxOps *tx_ops = umqSock->ReinitTxOps();
    DataRxOps *rx_ops = umqSock->ReinitRxOps();
    if (tx_ops == nullptr || rx_ops == nullptr) {
        /* 数据面表页分配失败：按建链资源不足处理 */
        return UBS_MALLOC_FAILED;
    }
    umq::DataPlaneEntry *entry = umq::DataPlaneTable::Live(sock->Fd());
    if (entry == nullptr) {
        return UBS_MALLOC_FAILED;
    }
    entry->txw = DataTx(sock, tx_ops);
    entry->rxw = DataRx(sock, rx_ops);
    return UBS_OK;
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

Result SocketBase::EnsureConnector(const SocketPtr &sock)
{
    /*
     * 惰性创建 connector：仅 connect 路径进入。connect(2) 对单个 fd 由单一线程
     * 发起（与 Accept/Connect 既有的单线程使用约定一致），无并发创建风险。
     */
    ConnectorOps *connectorOps = nullptr;
    Result result = CreateConnectorOps(type_, sock, connectorOps);
    if (result != UBS_OK) {
        return result;
    }

    SocketExt *ext = EnsureExt();
    if (ext == nullptr) {
        delete connectorOps;
        return UBS_MALLOC_FAILED;
    }
    if (ext->hs == nullptr) {
        /* 防御：正常路径 Create 已分配；此处兜底纯 connect 用法 */
        ext->hs = new (std::nothrow) HandshakeCtx;
        if (ext->hs == nullptr) {
            delete connectorOps;
            return UBS_MALLOC_FAILED;
        }
    }
    HandshakeCtx *hs = static_cast<HandshakeCtx *>(ext->hs);
    hs->connector = new (std::nothrow) Connector(sock, connectorOps);
    if (hs->connector == nullptr) {
        delete connectorOps;
        return UBS_MALLOC_FAILED;
    }
    return UBS_OK;
}

int SocketBase::GetSockOpt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (optval == nullptr || optlen == nullptr) {
        errno = EINVAL;
        UBS_VLOG_ERR("getsockopt optval or optlen is null\n");
        return -1;
    }

    if (level == static_cast<int>(UbsocketLevel::SOL_UB)) {
        switch (static_cast<UbSocketOpt>(optname)) {
            case UbSocketOpt::UBS_OPT_PROTOCOL: {
                int connectType = static_cast<int>(type_);
                memcpy(optval, &connectType, sizeof(int));
                *optlen = sizeof(int);
                return 0;
            }
            default: {
                errno = EINVAL;
                return -1;
            }
        }
    }
    errno = EINVAL;
    return -1;
}

int SocketBase::SetSockOpt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (optval == nullptr || optlen == 0) {
        errno = EINVAL;
        UBS_VLOG_ERR("SetSockOpt optval is null or optlen is 0\n");
        return -1;
    }

    if (level == static_cast<int>(UbsocketLevel::SOL_UB)) {
        switch (static_cast<UbSocketOpt>(optname)) {
            case UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS: {
                if (optlen < sizeof(uint32_t)) {
                    errno = EINVAL;
                    UBS_VLOG_ERR("SetSockOpt UBS_OPT_RPC_TIMEOUT_MS optlen too small: %u\n", optlen);
                    return -1;
                }
                uint32_t timeout_ms = 0;
                (void)memcpy(&timeout_ms, optval, sizeof(uint32_t));
                SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
                auto umq_sock = RefConvert<Socket, umq::UmqSocket>(sock);
                if (umq_sock == nullptr) {
                    errno = ENOTSOCK;
                    UBS_VLOG_ERR("SetSockOpt UBS_OPT_RPC_TIMEOUT_MS fd %d is not a umq socket\n", fd);
                    return -1;
                }
                umq_sock->SetLocalRpcTimeoutMs(timeout_ms);
                UBS_VLOG_DEBUG("SetSockOpt UBS_OPT_RPC_TIMEOUT_MS fd %d, timeout_ms: %u\n", fd, timeout_ms);
                return 0;
            }
            default: {
                errno = EINVAL;
                UBS_VLOG_ERR("SetSockOpt unknown SOL_UB optname: %d\n", optname);
                return -1;
            }
        }
    }
    errno = EINVAL;
    return -1;
}
} // namespace ubs
} // namespace ock
