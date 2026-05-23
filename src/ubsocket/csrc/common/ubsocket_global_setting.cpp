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
#include "ubsocket_global_setting.h"
#include "ubsocket_logger.h"
#include "ubsocket_setting_validator.h"

namespace ock {
namespace ubs {
std::mutex GlobalSetting::MUTEX;
uint32_t GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;            /* no protocol by default */
bool GlobalSetting::UBS_NATIVE_TCP_MODE = false;             /* use ubsocket by default */
bool GlobalSetting::UBS_TRACE_ENABLED = false;               /* disabled by default */
bool GlobalSetting::UBS_INITED = false;                      /* not inited by default */
int16_t GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;  /* disabled by default */
int16_t GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 0; /* disabled by default */
int16_t GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;     /* enabled by default */
bool GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_AUTO_FALLBACK_TCP = true;
bool GlobalSetting::UBS_READV_UNLIMITED = false;
bool GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
uint32_t GlobalSetting::UBS_SHARE_JFR_RX_QUEUE_DEPTH = 1024;
uint32_t GlobalSetting::UBS_TX_DEPTH = 1024;
uint32_t GlobalSetting::UBS_RX_DEPTH = 1024;
bool GlobalSetting::USE_BRPC_ZCOPY = false;
std::string GlobalSetting::UBS_BRPC_ALLOC_SYM_STR;
std::string GlobalSetting::UBS_BRPC_DEALLOC_SYM_STR;
UBHandshakeMode GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
uint32_t GlobalSetting::UBS_THREAD_POOL_SIZE = 1;

/* environment variable name */
#define ENV_TRACE_ENABLED "UBSOCKET_TRACE_ENABLE"
#define ENV_ASYNC_ACCEPTOR "UBSOCKET_ASYNC_ACCEPTOR_THREAD_COUNT"
#define ENV_ASYNC_CONNECTOR "UBSOCKET_ASYNC_CONNECTOR_THREAD_COUNT"
#define ENV_ASYNC_EPOLL "UBSOCKET_ASYNC_EPOLL_WAIT_THREAD_COUNT"
#define ENV_AUTO_FALLBACK_TCP "UBSOCKET_AUTO_FALLBACK_TCP"
#define ENV_SHARE_JFR_RX_QUEUE_DEPTH "UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH"

void GlobalSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {{ENV_ASYNC_ACCEPTOR, false, 0, 8L},
                               {ENV_ASYNC_CONNECTOR, false, 0, 8L},
                               {ENV_ASYNC_EPOLL, false, 1, 1L},
                               {ENV_SHARE_JFR_RX_QUEUE_DEPTH, false, 128, 10240}};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_TRACE_ENABLED, false, "true|false"},
                                    {ENV_AUTO_FALLBACK_TCP, false, "true|false"}};

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

Result GlobalSetting::VerifySetting() noexcept
{
    UBS_VLOG_DEBUG("start");
    /* set native tcp mode */
    if (UBS_ALLOWED_PROTOCOL == UBS_PROTOCOL_TCP) {
        UBS_NATIVE_TCP_MODE = true;
    }

    auto &validator = Validator::Instance();
    if (!validator.Validate(ENV_ASYNC_ACCEPTOR, (int64_t)UBS_ACCEPTOR_ASYNC_THREAD_COUNT,
                            "async_acceptor_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    if (!validator.Validate(ENV_ASYNC_CONNECTOR, (int64_t)UBS_CONNECTOR_ASYNC_THREAD_COUNT,
                            "async_connector_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    if (!validator.Validate(ENV_ASYNC_EPOLL, (int64_t)UBS_EPOLL_ASYNC_THREAD_COUNT, "async_epoll_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    UBS_VLOG_DEBUG("end");
    return UBS_OK;
}

Result GlobalSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t envValue = 0;
    std::string strEnvValue;

    /* load from env */
    if (GetEnvAndValidate(ENV_TRACE_ENABLED, envValue)) {
        UBS_TRACE_ENABLED = (envValue == 1);
    }

    if (GetEnvAndValidate(ENV_ASYNC_ACCEPTOR, envValue)) {
        UBS_ACCEPTOR_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_ASYNC_CONNECTOR, envValue)) {
        UBS_CONNECTOR_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_ASYNC_EPOLL, envValue)) {
        UBS_EPOLL_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_AUTO_FALLBACK_TCP, strEnvValue)) {
        UBS_AUTO_FALLBACK_TCP = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_SHARE_JFR_RX_QUEUE_DEPTH, envValue)) {
        UBS_SHARE_JFR_RX_QUEUE_DEPTH = static_cast<uint32_t>(envValue);
    }

    return UBS_OK;
}

} // namespace ubs
} // namespace ock