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

#include "common/ubsocket_common_includes.h"
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
    static std::string UMQ_DEV_IP;
    static std::string UMQ_DEV_NAME;
    static uint32_t UMQ_EID_INDEX;
    static std::string UMQ_DEV_SRC_EID_STR;
    static umq_eid_t UMQ_LOCAL_EID;
    static uint16_t UMQ_FC_DEFAULT_CREDIT;
    static uint16_t UMQ_FC_MAX_CREDIT;
    static uint16_t UMQ_FC_MIN_CREDIT;
    // TODO: 从环境变量中获取相关内存大小设置
    static uint64_t UMQ_IO_TOTAL_SIZE_MB;
    static uint64_t UMQ_MEM_POOL_INIT_SIZE_MB;
    static uint64_t UMQ_MEM_POOL_MAX_SIZE_MB;
    static uint64_t UMQ_BUF_POOL_DEPTH;
    static uint32_t UMQ_POST_BATCH_MAX;
    static umq_buf_block_size_t IO_BLOCK_TYPE;
    static umq_trans_mode_t IO_TRANS_MODE;
    static umq_trans_mode_t UMQ_TRANS_MODE;
    static int UMQ_PROCESS_SOCKET_ID;
    static uint32_t UMQ_SHARE_JFR_RX_QUEUE_DEPTH;
    static std::vector<uint32_t> UMQ_ALL_SOCKET_IDS;
    static std::string UMQ_DEV_SCHEDULE_POLICY_NAME;
    static dev_schedule_policy UMQ_DEV_SCHEDULE_POLICY;
    static ub_trans_mode UMQ_UB_TRANS_MODE;
    static bool UMQ_IS_BONDING;

private:
    static Result Init() noexcept;

    static void AddRules() noexcept;
    static Result LoadEnv() noexcept;

    static umq_buf_block_size_t BlockTypeFromStr(const std::string &typeStr) noexcept;
    static umq_trans_mode_t TransModeFromStr(const std::string &typeStr) noexcept;
    static dev_schedule_policy SchedulePolicyFromStr(const std::string &policyStr) noexcept;

    friend class UmqBackend;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SETTING_H