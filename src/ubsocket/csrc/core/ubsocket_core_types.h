/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_CORE_TYPES_H
#define UBS_COMM_UBSOCKET_CORE_TYPES_H

#include "ubsocket_common_includes.h"

namespace ock {
namespace ubs {
enum SocketState : uint8_t {
    SOCK_STAT_INIT = 0,        /* init */
    SOCK_STAT_RAW_ESTABLISHED, /* the raw socket established */
    SOCK_STAT_ESTABLISHED,     /* all things established */
    SOCK_STAT_SHUTDOWN,        /* shutdown */
    SOCK_STAT_CLOSE,           /* closed */
                               /* add state before COUNT */
    SOCK_STATE_COUNT
};

enum SocketType : uint8_t {
    SOCK_TYPE_TCP = 0, /* only contains raw socket */
    SOCK_TYPE_UMQ,     /* an ubsocket based on umq */
                       /* add type before COUNT */
    SOCK_TYPE_COUNT
};

class SocketInfo {
public:
    virtual ~SocketInfo() = default;

    SocketState State() const noexcept
    {
        return state_;
    }

    SocketType Type() const noexcept
    {
        return type_;
    }

    DEFINE_REF_OPERATION_FUNC

public:
    int raw_socket_ = -1;                /* fd of raw socket */
    int event_fd_ = -1;                  /* event fd */
    SocketState state_ = SOCK_STAT_INIT; /* state of ubsocket */
    SocketType type_ = SOCK_TYPE_TCP;    /* type of ubsocket */

    DECLARE_REF_COUNT_VARIABLE /* int16_t */
};

const std::string &SocketStateToStr(SocketState value);
const std::string &SocketTypeToStr(SocketType value);
bool SocketStateValid(SocketState value);
bool SocketTypeValid(SocketType value);
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_CORE_TYPES_H
