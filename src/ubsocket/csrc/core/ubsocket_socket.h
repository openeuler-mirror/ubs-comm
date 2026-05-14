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

#include "dl_libc_api.h"
#include "ubsocket_core_types.h"
#include "ubsocket_data_rx.h"
#include "ubsocket_data_tx.h"

namespace ock {
namespace ubs {
class Socket;
using SocketPtr = Ref<Socket>;

class Socket : public SocketInfo {
public:
    static Result Create(SocketType t, SocketPtr &sock);
    static Result CreateTxOps(SocketType value, const SocketPtr &sock, DataTxOpsPtr &ops);
    static Result CreateRxOps(SocketType value, const SocketPtr &sock, DataRxOpsPtr &ops);

public:
    Socket() = default;
    ~Socket() override = default;

    virtual Result Initialize() noexcept = 0;
    virtual void UnInitialize() noexcept = 0;

protected:
    DataTx tx_; /* take charge of send */
    DataRx rx_; /* take charge of receive */

    friend class DataTx;
    friend class DataRx;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
