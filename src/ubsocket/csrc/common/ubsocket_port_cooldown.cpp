/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "ubsocket_port_cooldown.h"

#include <chrono>

namespace ock {
namespace ubs {
bool PortCooldownManager::IsPortInCooldownImpl(const umq_port_id_t &port)
{
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::seconds(GetCooldownSec());

    Locker lock(mtx_);

    // 懒清理：自动移除已过期的冷却条目
    for (auto it = port_cooldown_map_.begin(); it != port_cooldown_map_.end();) {
        if (now > it->second + duration) {
            it = port_cooldown_map_.erase(it);
        } else {
            ++it;
        }
    }

    const uint32_t hash = GetPortHash(port);
    auto it = port_cooldown_map_.find(hash);
    return it != port_cooldown_map_.end();
}

void PortCooldownManager::MarkPortInCooldownImpl(const umq_port_id_t &port)
{
    Locker lock(mtx_);
    const uint32_t hash = GetPortHash(port);
    port_cooldown_map_[hash] = std::chrono::steady_clock::now();
}

} // namespace ubs
} // namespace ock
