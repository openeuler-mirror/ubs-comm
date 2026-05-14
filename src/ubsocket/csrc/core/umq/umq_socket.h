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
#ifndef UBS_COMM_UMQ_SOCKET_H
#define UBS_COMM_UMQ_SOCKET_H

#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
class UmqSocket : public Socket {
public:
    UmqSocket() = default;
    ~UmqSocket() override = default;

    Result Initialize() noexcept;
    void UnInitialize() noexcept;

    uint64_t UmqHandle() const noexcept;

private:
    uint64_t umq_handle_;
};
using UmqSocketPtr = Ref<UmqSocket>;

ALWAYS_INLINE uint64_t UmqSocket::UmqHandle() const noexcept
{
    return umq_handle_;
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SOCKET_H
