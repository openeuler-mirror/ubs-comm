/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "umq_socket.h"

namespace ock {
namespace ubs {
namespace umq {
Result UmqSocket::Initialize() noexcept
{
    return UBS_OK;
}

void UmqSocket::UnInitialize() noexcept {}

ALWAYS_INLINE uint64_t UmqSocket::UmqHandle() const noexcept
{
    return umq_handle_;
}

Result UmqSocket::CreateLocalUmq(umq_eid_t *connEid, umq_used_ports_t &mUsedPorts)
{
    return UBS_OK;
}

int UmqSocket::PrefillRx()
{
    return 0;
}
} // namespace umq
} // namespace ubs
} // namespace ock