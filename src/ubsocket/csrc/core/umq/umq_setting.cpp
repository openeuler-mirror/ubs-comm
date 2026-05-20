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
#include <cstdlib>
#include <string>
#include "common/ubsocket_global_setting.h"

namespace ock {
namespace ubs {
namespace umq {
#define ENV_UMQ_MIN_RESERVED_CREDIT "UBSOCKET_UMQ_MIN_RESERVED_CREDIT"
#define ENV_UMQ_BLOCK_TYPE "UBSOCKET_UMQ_BLOCK_TYPE"
#define ENV_UMQ_MEM_POOL_INIT_SIZE "UBSOCKET_UMQ_POOL_INITIAL_SIZE"
#define ENV_UMQ_MEM_POOL_MAX_SIZE "UBSOCKET_UMQ_POOL_MAX_SIZE"
#define ENV_UMQ_UBF_POOL_DEPTH "UBSOCKET_UMQ_BUF_POOL_DEPTH"

umq_trans_mode_t m_trans_mode = UMQ_TRANS_MODE_UB;
umq_buf_block_size_t UmqSetting::IO_BLOCK_TYPE = BLOCK_SIZE_8K;
uint16_t UmqSetting::UMQ_FC_DEFAULT_CREDIT = 1024L;
uint16_t UmqSetting::UMQ_FC_MAX_CREDIT = 1024L;
uint16_t UmqSetting::UMQ_FC_MIN_CREDIT = 100L;
uint16_t UmqSetting::UMQ_MEM_POOL_INIT_SIZE_MB = 200;
uint16_t UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 2048;
uint64_t UmqSetting::UMQ_BUF_POOL_DEPTH = 12000;

void UmqSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {{ENV_UMQ_MIN_RESERVED_CREDIT, false, 100, 1024},
                               {ENV_UMQ_MEM_POOL_INIT_SIZE, false, 1, 1024},
                               {ENV_UMQ_MEM_POOL_MAX_SIZE, false, 1, 8192}};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_UMQ_BLOCK_TYPE, false, "default|small|medium|large"}};

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

    return UBS_OK;
}

void UmqSetting::Init() noexcept
{
    AddRules();
    LoadEnv();
}

uint32_t UmqSetting::GetIOBufSize() noexcept
{
    switch (IO_BLOCK_TYPE) {
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
    if (typeStr == DEFAULT_QBUF_BLOCK_TYPE) {
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
} // namespace umq
} // namespace ubs
} // namespace ock