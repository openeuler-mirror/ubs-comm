/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {
#define ENV_URMA_DEV_NAME "UBS_DEV_NAME"
#define ENV_URMA_EID_IDX "UBS_DEV_EID_INDEX"
#define ENV_URMA_EPOLL_HANDLE_MODE "UBS_EPOLL_MODE"
#define ENV_URMA_BUF_DIRECT_INIT_COUNT "UBS_BUF_DIRECT_INIT_BLOCK_COUNT"
#define ENV_URMA_POLLER_COUNT "UBS_POLLER_COUNT"
#define ENV_URMA_POLLER_CQ_DEPTH "UBS_POLLER_CQ_DEPTH"
#define ENV_URMA_BUF_PAGE_SIZE_KB "UBS_BUF_PAGE_SIZE_KB"
#define ENV_URMA_CONNECTS_BUF_DEFAULT_KB "UBS_CONNECTS_BUF_DEFAULT_KB"
#define ENV_URMA_JFS_DEPTH "UBS_JFS_DEPTH"
#define ENV_URMA_JFR_DEPTH "UBS_JFR_DEPTH"
#define ENV_URMA_LINK_PRIORITY "UBS_LINK_PRIORITY"
#define ENV_URMA_SND_BUFF_ALLOC_UNIT "UBS_SND_BUFF_ALLOC_UNIT"

std::string UrmaSetting::UBS_DEV_NAME = "";
std::string UrmaSetting::UBS_DEV_EID;
uint32_t UrmaSetting::UBS_DEV_EID_INDEX = 0;

ub_trans_mode UrmaSetting::UBS_TRANS_MODE = ub_trans_mode::RM_CTP;
int8_t UrmaSetting::UBS_LINK_PRIORITY = UBSOCKET_LINK_PRIORITY_NOT_SET;

uint16_t UrmaSetting::UBS_POLLER_COUNT = 2;
uint32_t UrmaSetting::UBS_POLLER_CQ_DEPTH = 16384;
bool UrmaSetting::UBS_POLLER_BUSY_POLLING = false;

uint32_t UrmaSetting::UBS_BUF_POOL_SPACE_SIZE = 32;
uint16_t UrmaSetting::UBS_BUF_EXPAND_SIZE_MB = 16;
uint16_t UrmaSetting::UBS_BUF_INIT_SIZE_MB = 256;
uint16_t UrmaSetting::UBS_BUF_DIRECT_INIT_BLOCK_COUNT = 4;
std::string UrmaSetting::UBS_BUF_PAGE_SIZE_KB = "large"; /* page type: "large"(64K) | "tiny"(4K, pure CTP), unified with legacy umq block_type */
uint64_t UrmaSetting::UBS_BAD_UBPORT_TTL = 300;
uint32_t UrmaSetting::UBS_KEEPALIVE_TIME = 180;
uint32_t UrmaSetting::UBS_KEEPALIVE_INTVL = 0;
uint32_t UrmaSetting::UBS_KEEPALIVE_PROBES = 1;

bool UrmaSetting::UBS_BUF_ASYNC_EXPAND = true;
uint32_t UrmaSetting::UBS_CONNECTS_BUF_DEFAULT_KB = 128;
uint32_t UrmaSetting::UBS_JFS_DEPTH = 8192;
uint32_t UrmaSetting::UBS_JFR_DEPTH = 8192;
uint32_t UrmaSetting::UBS_SND_BUFF_ALLOC_UNIT = 1024; /* wire offset granularity in bytes; protocol default, keep both ends identical */
EpollHandleMode UrmaSetting::UBS_EPOLL_MODE = THREAD_PER_CONNECTION;

void UrmaSetting::Init() noexcept
{
    AddRules();
    auto result = LoadEnv();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        return;
    }
}

void UrmaSetting::AddRules()
{
    /* int64 rule: name, required, min, max
     * ranges below MUST stay aligned with consumer-layer hard constraints:
     * POLLER_COUNT <=16 (UrmaAggPollerSet::VerifyOptions), CQ_DEPTH >=4096 (UrmaPoller silent
     * PAGE_SIZE_KB levels "large"(64K) / "tiny"(4K, pure CTP) must divide the 16MB block (FixedAllocator init),
     * CONNECTS_BUF <=65536 KB (wire buffer_offset: 16bit x 1KB = 64MB addressing limit),
     * JFS/JFR_DEPTH upper bound is device max_xxx_depth (checked at create time). */
    Int64Rule rules_int64[] = {{ENV_URMA_EID_IDX, false, 0, 256},
                               {ENV_URMA_EPOLL_HANDLE_MODE, false, EPOLL_HANDLE_MODE_MIN, EPOLL_HANDLE_MODE_COUNT - 1},
                               {ENV_URMA_BUF_DIRECT_INIT_COUNT, false, 1, 2048},
                               {ENV_URMA_POLLER_COUNT, false, 1, 16},
                               {ENV_URMA_POLLER_CQ_DEPTH, false, 4096, UINT32_MAX},
                               {ENV_URMA_CONNECTS_BUF_DEFAULT_KB, false, 4, 65536},
                               {ENV_URMA_JFS_DEPTH, false, 2, UINT32_MAX},
                               {ENV_URMA_JFR_DEPTH, false, 2, UINT32_MAX},
                               {ENV_URMA_LINK_PRIORITY, false, UBSOCKET_LINK_PRIORITY_NOT_SET, URMA_MAX_PRIORITY},
                               {ENV_URMA_SND_BUFF_ALLOC_UNIT, false, 128, 2048}};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_URMA_BUF_PAGE_SIZE_KB, false, "large|tiny"}};

    /* str not empty rules: name, required */
    StrNotEmptyRule rules_str_not_empty[] = {{ENV_URMA_DEV_NAME, true}};

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

Result UrmaSetting::LoadEnv()
{
    int64_t int64EnvValue = 0;
    std::string strEnvValue;
    using GS = GlobalSetting;

    if (GS::GetEnvAndValidateNotEmpty(ENV_URMA_DEV_NAME, strEnvValue)) {
        UBS_DEV_NAME = strEnvValue;
    }

    if (GS::GetEnvAndValidate(ENV_URMA_EID_IDX, int64EnvValue)) {
        UBS_DEV_EID_INDEX = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_URMA_EPOLL_HANDLE_MODE, int64EnvValue)) {
        UBS_EPOLL_MODE = static_cast<EpollHandleMode>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_POLLER_COUNT, int64EnvValue)) {
        UBS_POLLER_COUNT = static_cast<uint16_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_POLLER_CQ_DEPTH, int64EnvValue)) {
        UBS_POLLER_CQ_DEPTH = static_cast<uint32_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_BUF_PAGE_SIZE_KB, strEnvValue)) {
        UBS_BUF_PAGE_SIZE_KB = strEnvValue;
    }
    if (GS::GetEnvAndValidate(ENV_URMA_CONNECTS_BUF_DEFAULT_KB, int64EnvValue)) {
        UBS_CONNECTS_BUF_DEFAULT_KB = static_cast<uint32_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_JFS_DEPTH, int64EnvValue)) {
        UBS_JFS_DEPTH = static_cast<uint32_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_JFR_DEPTH, int64EnvValue)) {
        UBS_JFR_DEPTH = static_cast<uint32_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_LINK_PRIORITY, int64EnvValue)) {
        UBS_LINK_PRIORITY = static_cast<int8_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_SND_BUFF_ALLOC_UNIT, int64EnvValue)) {
        UBS_SND_BUFF_ALLOC_UNIT = static_cast<uint32_t>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_BUF_DIRECT_INIT_COUNT, int64EnvValue)) {
        UBS_BUF_DIRECT_INIT_BLOCK_COUNT = static_cast<uint16_t>(int64EnvValue);
    }
    return UBS_OK;
}
} // namespace urma
} // namespace ubs
} // namespace ock