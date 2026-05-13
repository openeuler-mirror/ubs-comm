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
#include "ubsocket_common.h"

namespace ock {
namespace ubs {
enum SocketState : uint8_t {
    SOCK_STAT_INIT = 0,        /* init */
    SOCK_STAT_RAW_ESTABLISHED, /* the raw socket established */
    SOCK_STAT_ESTABLISHED,     /* all things established */
    SOCK_STAT_SHUTDOWN,        /* shutdown */
    SOCK_STAT_CLOSE,           /* closed */
};

enum SocketType : uint8_t {
    SOCK_TYPE_TCP = 0, /* only contains raw socket */
    SOCK_TYPE_UMQ,     /* an ubsocket based on umq */
    SOCK_TYPE_RDMA,    /* an ubscoket based on rdma */
    SOCK_TYPE_URMA_CTP /* an ubsocket based urma ctp */
};

class Socket : public Referable {
public:
    SocketState GetSoketState() const
    {
        return state_;
    }

    int GetRawFD() const
    {
        return raw_socket_;
    }

protected:
    int raw_socket_ = 0;                 /* fd of raw socket */
    SocketState state_ = SOCK_STAT_INIT; /* state of ubsocket */
    SocketType type_ = SOCK_TYPE_TCP;    /* type of ubsocket */
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CONNECTION_H
