/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A
 * PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBSOCKET_PORT_COOLDOWN_H
#define UBSOCKET_PORT_COOLDOWN_H

#include <chrono>
#include <unordered_map>
#include "common/ubsocket_global_setting.h"
#include "umq_types.h"

namespace ock {
namespace ubs {

/**
 * @brief 端口冷却管理器
 * 
 * 在CLOS组网场景下，umq_bind() 需要主备端口都可用，否则会失败且耗时分钟级。
 * 本管理器用于在 umq_bind() 失败后，将故障端口加入冷却期（10秒），
 * 重建连接时自动避开这些故障端口，让重试走其他可用端口。
 */
class PortCooldownManager {
public:
    /**
     * @brief 获取冷却时长（秒）
     * 
     * 从 GlobalSetting::UBS_PORT_COOLDOWN_SEC 读取，
     * 端口故障可能是瞬时的（物理层闪断，几秒后恢复），
     * 默认10秒足够让物理层恢复，也不会让冷却期过长。
     */
    static uint32_t GetCooldownSec()
    {
        return GlobalSetting::UBS_PORT_COOLDOWN_SEC;
    }

    /**
     * @brief 查询端口是否处于冷却期
     * 
     * @param port 端口ID（umq_port_id_t类型）
     * @return true: 端口在冷却期内，应避免使用; false: 端口可用
     * 
     * 注意：每次调用时会自动清理已过期的冷却条目（懒清理策略）
     */
    static bool IsPortInCooldown(const umq_port_id_t &port);

    /**
     * @brief 标记端口进入冷却期
     * 
     * 将指定端口加入冷却期，冷却到期时间 = 当前时间 + GetCooldownSec()
     * 冷却期内的端口应被建连重试逻辑避开。
     * 
     * @param port 端口ID（umq_port_id_t类型）
     */
    static void MarkPortInCooldown(const umq_port_id_t &port);

private:
    /**
     * @brief 生成端口的哈希值（用于unordered_map的key）
     * 
     * 仅使用 chip_id + die_id + port_idx（24bit），忽略 reserved 字段
     * 结构：chip_id(8bit) | die_id(8bit) | port_idx(8bit)
     */
    static uint32_t GetPortHash(const umq_port_id_t &port);
};

} // namespace ubs
} // namespace ock

#endif // UBSOCKET_PORT_COOLDOWN_H
