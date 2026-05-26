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

#include "umq_setting.h"
#include <arpa/inet.h>  // 包含 inet_pton、inet_ntop 函数
#include <sys/socket.h> // 包含 AF_INET、AF_INET6 等地址族常量
#include <cstdlib>
#include "core/ubsocket_socket_helper.h"

namespace ock {
namespace ubs {
namespace umq {
#define ENV_UMQ_MIN_RESERVED_CREDIT "UBSOCKET_MIN_RESERVED_CREDIT"
#define ENV_UMQ_BLOCK_TYPE "UBSOCKET_BLOCK_TYPE"
#define ENV_UMQ_MEM_POOL_INIT_SIZE "UBSOCKET_POOL_INITIAL_SIZE"
#define ENV_UMQ_MEM_POOL_MAX_SIZE "UBSOCKET_POOL_MAX_SIZE"
#define ENV_UMQ_UBF_POOL_DEPTH "UBSOCKET_BUF_POOL_DEPTH"
#define ENV_UMQ_SCHEDULE_POLICY "UBSOCKET_SCHEDULE_POLICY"
#define ENV_UMQ_DEV_IP "UBSOCKET_DEV_IP"
#define ENV_UMQ_DEV_NAME "UBSOCKET_DEV_NAME"
#define ENV_UMQ_DEV_SRC_EID "UBSOCKET_SRC_EID"
#define ENV_UMQ_EID_IDX "UBSOCKET_EID_IDX"
#define ENV_UMQ_UB_TRANS_MODE "UBSOCKET_UB_TRANS_MODE"
#define ENV_UMQ_FLOW_CONTROL_ENABLED "UBSOCKET_FLOW_CONTROL_ENABLE"

#define DEFAULT_DEV_SCHEDULE_POLICY "affinity_priority"
#define ROUND_ROBIN_DEV_SCHEDULE_POLICY "rr"
#define CPU_AFFINITY_DEV_SCHEDULE_POLICY "affinity"
#define CPU_AFFINITY_PRIORITY_DEV_SCHEDULE_POLICY "affinity_priority"

umq_buf_block_size_t UmqSetting::IO_BLOCK_TYPE = BLOCK_SIZE_8K;
uint16_t UmqSetting::UMQ_FC_DEFAULT_CREDIT = 1024L;
uint16_t UmqSetting::UMQ_FC_MAX_CREDIT = 1024L;
uint16_t UmqSetting::UMQ_FC_MIN_CREDIT = 100L;
uint64_t UmqSetting::UMQ_IO_TOTAL_SIZE_MB = 1024;
uint64_t UmqSetting::UMQ_MEM_POOL_INIT_SIZE_MB = 200;
uint64_t UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 2048;
uint64_t UmqSetting::UMQ_BUF_POOL_DEPTH = 12000;
int UmqSetting::UMQ_PROCESS_SOCKET_ID = -1;
std::vector<uint32_t> UmqSetting::UMQ_ALL_SOCKET_IDS = {};
uint32_t UmqSetting::UMQ_POST_BATCH_MAX = 256UL;
uint32_t UmqSetting::UMQ_EID_INDEX = 0;
uint32_t UmqSetting::UMQ_SHARE_JFR_RX_QUEUE_DEPTH = 1024;
std::string UmqSetting::UMQ_DEV_NAME = "";
std::string UmqSetting::UMQ_DEV_IP = "";
std::string UmqSetting::UMQ_DEV_SRC_EID_STR = "";
umq_eid_t UmqSetting::UMQ_LOCAL_EID = {};
std::string UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME = DEFAULT_DEV_SCHEDULE_POLICY;
dev_schedule_policy UmqSetting::UMQ_DEV_SCHEDULE_POLICY = CPU_AFFINITY_PRIORITY;
// TODO: 根据 UBS_TRANS_MODE 来设置 UMQ_TRANS_MODE, 待增加 ENV转换器
umq_trans_mode_t UmqSetting::UMQ_TRANS_MODE = UMQ_TRANS_MODE_UB;
ub_trans_mode UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
bool UmqSetting::UMQ_IS_BONDING = false;
bool UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;

void UmqSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {{ENV_UMQ_MIN_RESERVED_CREDIT, false, 100, 1024},
                               {ENV_UMQ_MEM_POOL_INIT_SIZE, false, 1, 1024},
                               {ENV_UMQ_MEM_POOL_MAX_SIZE, false, 1, 8192}};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_UMQ_BLOCK_TYPE, false, "tiny|default|small|medium|large"},
                                    {ENV_UMQ_SCHEDULE_POLICY, false, "rr|affinity|affinity_priority"},
                                    {ENV_UMQ_UB_TRANS_MODE, false, "RC_TP|RM_TP|RM_CTP|RC_CTP"},
                                    {ENV_UMQ_FLOW_CONTROL_ENABLED, false, "true|false"}};

    /* str not empty rules: name, required */
    StrNotEmptyRule rules_str_not_empty[] = {};

    for (auto &item : rules_int64) {
        Validator::Instance().AddNumRule(item);
    }

    for (auto &item : rules_str_enum) {
        Validator::Instance().AddStrEnumRule(item);
    }

    for (auto &item : rules_str_not_empty) {
        Validator::Instance().AddStrNotEmtpyRule(item);
    }

    UBS_SLOG_DEBUG(Validator::Instance().DumpString());
}

Result UmqSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t int64EnvValue = 0;
    std::string strEnvValue;
    using GS = GlobalSetting;

    /* load from env */
    if (GS::GetEnvAndValidate(ENV_UMQ_MIN_RESERVED_CREDIT, int64EnvValue)) {
        UMQ_FC_MIN_CREDIT = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_BLOCK_TYPE, strEnvValue)) {
        IO_BLOCK_TYPE = BlockTypeFromStr(strEnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MEM_POOL_INIT_SIZE, int64EnvValue)) {
        UMQ_MEM_POOL_INIT_SIZE_MB = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MEM_POOL_MAX_SIZE, int64EnvValue)) {
        UMQ_MEM_POOL_MAX_SIZE_MB = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_DEV_IP, strEnvValue)) {
        UMQ_DEV_IP = strEnvValue;
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_DEV_NAME, strEnvValue)) {
        UMQ_DEV_NAME = strEnvValue;
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_EID_IDX, int64EnvValue)) {
        UMQ_EID_INDEX = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_DEV_SRC_EID, strEnvValue)) {
        UMQ_DEV_SRC_EID_STR = strEnvValue;
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_FLOW_CONTROL_ENABLED, strEnvValue)) {
        UMQ_FLOW_CONTROL_ENABLE = Func::BoolFromStr(strEnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_SCHEDULE_POLICY, strEnvValue)) {
        UMQ_DEV_SCHEDULE_POLICY_NAME = strEnvValue;
        UMQ_DEV_SCHEDULE_POLICY = SchedulePolicyFromStr(strEnvValue);
        UBS_VLOG_INFO("Current policy type: %s", UMQ_DEV_SCHEDULE_POLICY_NAME.c_str());
    }

    if (UMQ_DEV_IP.size() > 0) {
        UBS_VLOG_ERR("IP address is invalid. Please double check your input(%s)\n", UMQ_DEV_IP.c_str());
        return UBS_ERROR;
    } else if (UMQ_DEV_NAME.size() > 0) {
        UBS_VLOG_INFO("%s: %s\n", ENV_UMQ_DEV_NAME, UMQ_DEV_NAME.c_str());
        if (UMQ_DEV_SRC_EID_STR.size() > 0) {
            if (inet_pton(AF_INET6, UMQ_DEV_SRC_EID_STR.c_str(), &(UMQ_LOCAL_EID)) == 1) {
                UBS_VLOG_INFO("%s: %s (eid)\n", ENV_UMQ_DEV_SRC_EID, UMQ_DEV_SRC_EID_STR.c_str());
            } else {
                UBS_VLOG_ERR("Eid is invalid. Please double check your input(%s)\n", UMQ_DEV_SRC_EID_STR.c_str());
                return UBS_ERROR;
            }
        }
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_UB_TRANS_MODE, strEnvValue)) {
        std::string ub_trans_mode_str = strEnvValue;
        if (ub_trans_mode_str == "RM_TP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RM_TP;
        } else if (ub_trans_mode_str == "RM_CTP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RM_CTP;
        } else if (ub_trans_mode_str == "RC_TP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_TP;
        } else if (ub_trans_mode_str == "RC_CTP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_CTP;
        } else {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_TP;
        }
        UBS_VLOG_INFO("Current ub trans mode");
    }

    return UBS_OK;
}

Result UmqSetting::Init() noexcept
{
    AddRules();
    return LoadEnv();
}

uint32_t UmqSetting::GetIOBufSize() noexcept
{
    switch (IO_BLOCK_TYPE) {
        case BLOCK_SIZE_4K:
            return SIZE_4K - IOBUF_DIFF;
        case BLOCK_SIZE_8K:
            return SIZE_8K - IOBUF_DIFF;
        case BLOCK_SIZE_16K:
            return SIZE_16K - IOBUF_DIFF;
        case BLOCK_SIZE_32K:
            return SIZE_32K - IOBUF_DIFF;
        case BLOCK_SIZE_64K:
            return SIZE_64K - IOBUF_DIFF;
        default:
            return SIZE_8K - IOBUF_DIFF;
    }
}

uint64_t UmqSetting::FloorMask() noexcept
{
    switch (IO_BLOCK_TYPE) {
        case BLOCK_SIZE_4K:
            return SIZE_4K - MASK_DIFF;
        case BLOCK_SIZE_8K:
            return SIZE_8K - MASK_DIFF;
        case BLOCK_SIZE_16K:
            return SIZE_16K - MASK_DIFF;
        case BLOCK_SIZE_32K:
            return SIZE_32K - MASK_DIFF;
        case BLOCK_SIZE_64K:
            return SIZE_64K - MASK_DIFF;
        default:
            return SIZE_8K - MASK_DIFF;
    }
}

umq_buf_block_size_t UmqSetting::BlockTypeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == TINY_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_4K;
    } else if (typeStr == DEFAULT_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_8K;
    } else if (typeStr == SMALL_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_16K;
    } else if (typeStr == MEDIUM_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_32K;
    } else if (typeStr == LARGE_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_64K;
    }
    // 如果字符串不匹配，返回默认值
    return BLOCK_SIZE_8K;
}
umq_trans_mode_t UmqSetting::TransModeFromStr(const std::string &typeStr) noexcept
{
    return UMQ_TRANS_MODE_IB_PLUS;
}

dev_schedule_policy UmqSetting::SchedulePolicyFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == ROUND_ROBIN_DEV_SCHEDULE_POLICY) {
        return ROUND_ROBIN;
    } else if (typeStr == CPU_AFFINITY_DEV_SCHEDULE_POLICY) {
        return CPU_AFFINITY;
    } else if (typeStr == CPU_AFFINITY_PRIORITY_DEV_SCHEDULE_POLICY) {
        return CPU_AFFINITY_PRIORITY;
    }
    // 如果字符串不匹配，返回默认值
    return CPU_AFFINITY_PRIORITY;
}
} // namespace umq
} // namespace ubs
} // namespace ock