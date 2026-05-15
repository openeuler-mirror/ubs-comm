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
#ifndef UBS_COMM_UMQ_SETTING_H
#define UBS_COMM_UMQ_SETTING_H

#include <string>

#include "ubsocket_def.h"
#include "ubsocket_global_setting.h"
#include "umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqSetting {
public:
    /* 禁止实例化 */
    UmqSetting() = delete;

    /**
     * @brief 初始化 UMQ 设置，在 ubsocket_init 中调用
     */
    static void Init() noexcept;

    /**
     * @brief 获取适配 brpc IOBuf 的实际数据缓冲区大小
     * @return uint32_t 例如：配置为 8k 时，返回 8160 (8192 - 32)
     */
    static uint32_t GetIOBufSize() noexcept;

    static uint64_t FloorMask() noexcept;

public:
    static std::mutex MUTEX;
    static bool UBS_INITED;
    /* 存储解析后的枚举值 */
    static umq_buf_block_size_t IO_BLOCK_TYPE;
    static umq_trans_mode_t IO_TRANS_MODE;

    /**
     * @brief 将字符串环境变量转换为 UMQ 枚举
     */
    static umq_buf_block_size_t ParseBlockType(const std::string &typeStr) noexcept;

    /**
     * @brief 将字符串环境变量转换为 UMQ 枚举
     */
    static umq_trans_mode_t ParseTransMode(const std::string &typeStr) noexcept;
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SETTING_H