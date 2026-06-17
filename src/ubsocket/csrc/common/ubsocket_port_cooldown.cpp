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
#include <mutex>
#include <unordered_map>

namespace ock {
namespace ubs {

// 冷却状态存储：port_hash -> 冷却到期时间
static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> s_portCooldownMap;
static std::mutex s_mutex;

uint32_t PortCooldownManager::GetPortHash(const umq_port_id_t &port)
{
    // 仅使用 chip_id + die_id + port_idx（24bit），忽略 reserved 字段
    return (port.bs.chip_id << 16) | (port.bs.die_id << 8) | port.bs.port_idx;
}

bool PortCooldownManager::IsPortInCooldown(const umq_port_id_t &port)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    auto now = std::chrono::steady_clock::now();
    auto cooldownDuration = std::chrono::seconds(GetCooldownSec());

    // 懒清理：自动移除已过期的冷却条目
    for (auto it = s_portCooldownMap.begin(); it != s_portCooldownMap.end();) {
        if (now > it->second + cooldownDuration) {
            it = s_portCooldownMap.erase(it);
        } else {
            ++it;
        }
    }

    uint32_t hash = GetPortHash(port);
    auto it = s_portCooldownMap.find(hash);
    return it != s_portCooldownMap.end();
}

void PortCooldownManager::MarkPortInCooldown(const umq_port_id_t &port)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    uint32_t hash = GetPortHash(port);
    s_portCooldownMap[hash] = std::chrono::steady_clock::now();
}

} // namespace ubs
} // namespace ock
