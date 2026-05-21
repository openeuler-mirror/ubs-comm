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

#include "common/ubsocket_global_setting.h"
#include "include/ubsocket_def.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqSetting {
public:
    UmqSetting() = delete;

    /**
     * @brief 获取适配 brpc IOBuf 的实际数据缓冲区大小
     * @return uint32_t 例如：配置为 8k 时，返回 8160 (8192 - 32)
     */
    static uint32_t GetIOBufSize() noexcept;

    static uint64_t FloorMask() noexcept;

public:
    static uint16_t UMQ_FC_DEFAULT_CREDIT;
    static uint16_t UMQ_FC_MAX_CREDIT;
    static uint16_t UMQ_FC_MIN_CREDIT;
    static uint64_t UMQ_MEM_POOL_INIT_SIZE_MB;
    static uint64_t UMQ_MEM_POOL_MAX_SIZE_MB;
    static uint64_t UMQ_BUF_POOL_DEPTH;
    static uint32_t UMQ_POST_BATCH_MAX;
    static umq_buf_block_size_t IO_BLOCK_TYPE;
    static umq_trans_mode_t IO_TRANS_MODE;
    static std::string UMQ_DEV_NAME;
    static uint32_t UMQ_EID_INDEX;
    static umq_eid_t UMQ_LOCAL_EID;
    static int UMQ_PROCESS_SOCKET_ID;
    static std::vector<uint32_t> UMQ_ALL_SOCKET_IDS;

private:
    static void Init() noexcept;

    static void AddRules() noexcept;
    static Result LoadEnv() noexcept;

    static umq_buf_block_size_t BlockTypeFromStr(const std::string &typeStr) noexcept;
    static umq_trans_mode_t TransModeFromStr(const std::string &typeStr) noexcept;

    friend class UmqBackend;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SETTING_H