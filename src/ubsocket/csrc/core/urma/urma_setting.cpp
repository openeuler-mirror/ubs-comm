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
#define ENV_URMA_DEV_NAME "UBSOCKET_UB_DEV"
#define ENV_URMA_EID_IDX "UBSOCKET_UB_EID_IDX"
#define ENV_URMA_BOUNDING_DEV "UBSOCKET_UB_BOUNDING_DEV"
#define ENV_URMA_BUSY_POLLING "UBSOCKET_UB_BUSY_POLLING"
#define ENV_URMA_EPOLL_HANDLE_MODE "UBSOCKET_EPOLL_HANDLE_MODE"
#define ENV_URMA_KEEPALIVE_TIME "UBSOCKET_KEEPALIVE_TIME"

bool UrmaSetting::UB_USING_BOUNDING_DEV = true;
std::string UrmaSetting::UB_DEV_NAME = "";
std::string UrmaSetting::UB_DEV_EID;
uint32_t UrmaSetting::UB_EID_INDEX = 0;

ub_trans_mode UrmaSetting::UB_TANS_MODE = ub_trans_mode::RM_CTP;
bool UrmaSetting::UB_JETTY_SHARED_ENABLED = false;
uint16_t UrmaSetting::UB_JETTY_INIT_COUNT = 2;
uint16_t UrmaSetting::UB_JETTY_MAX_COUNT = 2;
uint16_t UrmaSetting::UB_JETTY_JFR_COUNT = 2;
uint16_t UrmaSetting::UB_JETTY_JFS_DEPTH = 256;
uint16_t UrmaSetting::UB_JETTY_JFR_DEPTH = 256;
urma_transport_mode_t UrmaSetting::UB_JETTY_TRANS_MODE = URMA_TM_RM;
urma_tp_type_t UrmaSetting::UB_JETTY_TP_TYPE = URMA_CTP;

uint16_t UrmaSetting::UB_POLLER_COUNT = 2;
uint32_t UrmaSetting::UB_POLLER_JFC_DEPTH = 1024;
bool UrmaSetting::UB_POLLER_BUSY_POLLING = false;

uint32_t UrmaSetting::UB_BUF_POOL_SPACE_SIZE = 32;
uint16_t UrmaSetting::UB_BUF_BLOCK_SIZE_MB = 16;
uint16_t UrmaSetting::UB_BUF_INIT_MEM_SIZE_MB = 256;
uint16_t UrmaSetting::UB_BUF_PAGE_SIZE_KB = 64;
uint64_t UrmaSetting::UB_BAD_PORT_RECOVER_SECONDS = 300;
uint32_t UrmaSetting::UB_KEEPALIVE_TIME = 0;
uint32_t UrmaSetting::UB_KEEPALIVE_INTVL = 75;
uint32_t UrmaSetting::UB_KEEPALIVE_PROBES = 1;

bool UrmaSetting::UB_BUF_ASYNC_EXPAND = false;
EpollHandleMode UrmaSetting::UB_EPOLL_HANDLE_MODE = THREAD_PER_CONNECTION;

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
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {{ENV_URMA_EID_IDX, false, 0, 1024},
                               {ENV_URMA_BOUNDING_DEV, false, 0, 1},
                               {ENV_URMA_BUSY_POLLING, false, 0, 1},
                               {ENV_URMA_EPOLL_HANDLE_MODE, false, EPOLL_HANDLE_MODE_MIN, EPOLL_HANDLE_MODE_COUNT - 1},
                               {ENV_URMA_KEEPALIVE_TIME, false, 0, 86400},};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {};

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
        UB_DEV_NAME = strEnvValue;
    }

    if (GS::GetEnvAndValidate(ENV_URMA_EID_IDX, int64EnvValue)) {
        UB_EID_INDEX = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_URMA_BOUNDING_DEV, int64EnvValue)) {
        UB_USING_BOUNDING_DEV = (int64EnvValue != 0);
    }

    if (GS::GetEnvAndValidate(ENV_URMA_BUSY_POLLING, int64EnvValue)) {
        UB_POLLER_BUSY_POLLING = (int64EnvValue != 0);
    }

    if (GS::GetEnvAndValidate(ENV_URMA_EPOLL_HANDLE_MODE, int64EnvValue)) {
        UB_EPOLL_HANDLE_MODE = static_cast<EpollHandleMode>(int64EnvValue);
    }
    if (GS::GetEnvAndValidate(ENV_URMA_KEEPALIVE_TIME, int64EnvValue)) {
        UB_KEEPALIVE_TIME = static_cast<uint32_t>(int64EnvValue);
    }
    return UBS_OK;
}
} // namespace urma
} // namespace ubs
} // namespace ock