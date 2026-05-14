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
#include "umq_data_rx_ops.h"
#include "umq_data_tx_ops.h"
#include "umq_socket.h"

namespace ock {
namespace ubs {
Result Socket::Create(ock::ubs::SocketType t, SocketPtr &sock)
{
    if (t == SOCK_TYPE_UMQ) {
        /* step1: create umq socket */
        auto umqSock = MakeRef<UmqSocket>();
        if (umqSock == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* step2: do initialize */
        auto result = umqSock->Initialize();
        if (result != UBS_OK) {
            return result;
        }

        auto baseSock = RefConvert<UmqSocket, Socket>(umqSock);

        /* step3: create tx ops */
        DataTxOpsPtr txOps;
        result = CreateTxOps(t, baseSock, txOps);
        if (result != UBS_OK) {
            return result;
        }

        /* step4: create rx ops */
        DataRxOpsPtr rxOps;
        result = CreateRxOps(t, baseSock, rxOps);
        if (result != UBS_OK) {
            return result;
        }

        /* step5: assign tx and rx */
        SocketInfo info = {.raw_socket_ = baseSock->raw_socket_, .event_fd_ = baseSock->event_fd_};
        baseSock->tx_ = DataTx(info, txOps);
        baseSock->rx_ = DataRx(info, rxOps);

        /* assign out */
        sock = baseSock;

        return UBS_OK;
    } else {
        return UBS_INVALID_PARAM;
    }
} // namespace ubs

Result Socket::CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOpsPtr &ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SOCK_TYPE_UMQ) {
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq ops */
        auto umqOps = MakeRef<UmqDataTxOps>(umqSock->UmqHandle());
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        ops = umqOps.Get();
        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}

Result Socket::CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOpsPtr &ops)
{
    if (sock == nullptr) {
        return UBS_INVALID_PARAM;
    }

    if (value == SOCK_TYPE_UMQ) {
        /* convert to umq socket */
        UmqSocketPtr umqSock = RefConvert<Socket, UmqSocket>(sock);

        /* create umq ops */
        auto umqOps = MakeRef<UmqDataRxOps>(umqSock->UmqHandle());
        if (umqOps == nullptr) {
            return UBS_MALLOC_FAILED;
        }

        /* set out ops */
        ops = umqOps.Get();
        return UBS_OK;
    } else {
        /* invalid type */
        return UBS_INVALID_PARAM;
    }
}
} // namespace ubs
} // namespace ock