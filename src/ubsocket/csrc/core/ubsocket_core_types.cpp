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
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
const std::string &SocketStateToStr(SocketState value)
{
    static std::string strings[SOCK_STATE_COUNT + 1L] = {
        "init", "raw_socket_established", "established", "shutdown", "closed", "unknown"};
    if (SocketStateValid(value)) {
        return strings[value];
    }

    return strings[SOCK_STATE_COUNT];
}
const std::string &SocketTypeToStr(SocketType value)
{
    static std::string strings[SOCK_TYPE_COUNT + 1L] = {"TCP", "UMQ", "unknown"};
    if (SocketTypeValid(value)) {
        return strings[value];
    }

    return strings[value];
}

const std::string &SocketCreateTypeToStr(SocketCreateType value)
{
    static std::string strings[SOCK_CREATE_TYPE_COUNT + 1L] = {"unknown", "listen", "client", "accept", "unknown"};
    if (SocketCreateTypeValid(value)) {
        return strings[value];
    }

    return strings[value];
}

bool SocketStateValid(SocketState value)
{
    return (value < SOCK_STATE_COUNT);
}

bool SocketTypeValid(SocketType value)
{
    return (value < SOCK_TYPE_COUNT);
}

bool SocketCreateTypeValid(SocketCreateType value)
{
    return (value < SOCK_CREATE_TYPE_COUNT);
}
} // namespace ubs
} // namespace ock