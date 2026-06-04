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
uint32_t GlobalSetting::UBS_ALLOWED_PROTOCOL = 0; /* no protocol by default */
bool GlobalSetting::UBS_NATIVE_TCP_MODE = false;  /* use ubsocket by default */
bool GlobalSetting::UBS_TRACE_ENABLED = true;
bool GlobalSetting::UBS_CLI_ENABLED = false;
bool GlobalSetting::UBS_PROBE_ENABLED = false;
bool GlobalSetting::UBS_INITED = false;                      /* not inited by default */
std::string GlobalSetting::UBS_TRANS_MODE = "ub";            /* transport mode, from env */
int16_t GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;  /* disabled by default */
int16_t GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 0; /* disabled by default */
int16_t GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;     /* enabled by default */
bool GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_AUTO_FALLBACK_TCP = true;
bool GlobalSetting::UBS_READV_UNLIMITED = false;
bool GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
bool GlobalSetting::UBS_ENABLE_DEGRADE = true;
uint32_t GlobalSetting::UBS_SHARE_JFR_RX_QUEUE_DEPTH = 1024;
uint32_t GlobalSetting::UBS_SHARE_JFR_RX_O3_QUEUE_DEPTH = 256;
uint32_t GlobalSetting::UBS_TX_DEPTH = 1024;
uint32_t GlobalSetting::UBS_RX_DEPTH = 1024;
bool GlobalSetting::USE_BRPC_ZCOPY = true;
std::string GlobalSetting::UBS_BRPC_ALLOC_SYM_STR;
std::string GlobalSetting::UBS_BRPC_DEALLOC_SYM_STR;
UBHandshakeMode GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
uint32_t GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
bool GlobalSetting::UBS_PROF_ENABLE = false;
uint16_t GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
std::string GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
uint64_t GlobalSetting::UBS_TRACE_TIME = UBSOCKET_TRACE_TIME_DEFAULT;
uint64_t GlobalSetting::UBS_TRACE_FILE_SIZE = UBSOCKET_TRACE_FILE_SIZE_DEFAULT;
std::string GlobalSetting::UBS_TRACE_FILE_PATH = "/tmp/ubsocket/log";
uint32_t GlobalSetting::UBS_PROBE_MS = 1000;
uint32_t GlobalSetting::UBS_PROBE_BATCH = 10;
bool GlobalSetting::UBS_BACKUP_LINK_ENABLED = false;

/* environment variable name */
#define ENV_TRACE_ENABLED "UBSOCKET_TRACE_ENABLE"
#define ENV_ASYNC_ACCEPTOR "UBSOCKET_ASYNC_ACCEPT" /* match brpc_test FLAGS_ubsocket_async_accept */
#define ENV_ASYNC_CONNECTOR "UBSOCKET_ASYNC_CONNECTOR_THREAD_COUNT"
#define ENV_ASYNC_EPOLL "UBSOCKET_ASYNC_EPOLL_WAIT_THREAD_COUNT"
#define ENV_AUTO_FALLBACK_TCP "UBSOCKET_AUTO_FALLBACK_TCP"
#define ENV_ENABLE_SHARE_JFR "UBSOCKET_ENABLE_SHARE_JFR"
#define ENV_SHARE_JFR_RX_QUEUE_DEPTH "UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH"
#define ENV_SHARE_JFR_RX_O3_QUEUE_DEPTH "UBSOCKET_SHARE_JFR_RX_O3_QUEUE_DEPTH"
#define ENV_TRANS_MODE "UBSOCKET_TRANS_MODE"
#define ENV_UBS_RX_DEPTH "UBSOCKET_RX_DEPTH"
#define ENV_UBS_TX_DEPTH "UBSOCKET_TX_DEPTH"
#define ENV_USE_BRPC_ZCOPY "UBSOCKET_USE_BRPC_ZCOPY"
#define ENV_UBS_HAND_SHAKE_MODE "UBSOCKET_UB_HANDSHAKE_MODE"
#define ENV_PROF_ENABLE "UBSOCKET_PROF_ENABLE"
#define ENV_PROF_DUMP_INTERVAL_MIN "UBSOCKET_PROF_DUMP_INTERVAL_MIN"
#define ENV_PROF_DUMP_PATH "UBSOCKET_PROF_DUMP_PATH"
#define ENV_TRACE_TIME "UBSOCKET_TRACE_TIME"
#define ENV_TRACE_FILE_SIZE "UBSOCKET_TRACE_FILE_SIZE"
#define ENV_TRACE_FILE_PATH "UBSOCKET_TRACE_FILE_PATH"
#define ENV_VAR_DEGRADE "UBSOCKET_DEGRADE"
#define ENV_VAR_CLI "UBSOCKET_STATS_CLI"
#define ENV_VAR_PROBE "UBSOCKET_PROBE_ENABLE"
#define ENV_VAR_PROBE_TIME "UBSOCKET_PROBE_TIME_MS"
#define ENV_VAR_PROBE_BATCH "UBSOCKET_PROBE_BATCH"
#define ENV_BACKUP_LINK_ENABLED "UBSOCKET_BACKUP_LINK_ENABLE"

void GlobalSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {
        {ENV_ASYNC_ACCEPTOR, false, 0, 8L},
        {ENV_ASYNC_CONNECTOR, false, 0, 8L},
        {ENV_ASYNC_EPOLL, false, 1, 1L},
        {ENV_SHARE_JFR_RX_QUEUE_DEPTH, false, 128, 10240},
        {ENV_SHARE_JFR_RX_O3_QUEUE_DEPTH, false, 128, 10240},
        {ENV_UBS_RX_DEPTH, false, 2, UINT32_MAX},
        {ENV_UBS_TX_DEPTH, false, 2, UINT32_MAX},
        {ENV_PROF_DUMP_INTERVAL_MIN, false, 1, 5},
        {ENV_TRACE_TIME, false, UBSOCKET_TRACE_TIME_MIN, UBSOCKET_TRACE_TIME_MAX},
        {ENV_TRACE_FILE_SIZE, false, UBSOCKET_TRACE_FILE_SIZE_MIN, UBSOCKET_TRACE_FILE_SIZE_MAX},
        {ENV_VAR_PROBE_TIME, false, UBSOCKET_PROBE_TIME_MS_MIN, UBSOCKET_PROBE_TIME_MS_MAX},
        {ENV_VAR_PROBE_BATCH, false, UBSOCKET_PROBE_BATCH_MIN, UBSOCKET_PROBE_BATCH_MAX},
    };

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_TRACE_ENABLED, true, "true|false"},
                                    {ENV_AUTO_FALLBACK_TCP, false, "true|false"},
                                    {ENV_ENABLE_SHARE_JFR, false, "true|false"},
                                    {ENV_USE_BRPC_ZCOPY, false, "true|false"},
                                    {ENV_TRANS_MODE, false, "ub|ib"},
                                    {ENV_UBS_HAND_SHAKE_MODE, false, "tfo|ub_sock_opt"},
                                    {ENV_PROF_ENABLE, false, "true|false"},
                                    {ENV_ASYNC_ACCEPTOR, false, "true|false"},
                                    {ENV_VAR_DEGRADE, false, "true|false"},
                                    {ENV_VAR_CLI, false, "true|false"},
                                    {ENV_VAR_PROBE, false, "true|false"},
                                    {ENV_BACKUP_LINK_ENABLED, false, "true|false"}};

    /* str not empty rules: name, required, maxLen */
    StrNotEmptyRule rules_str_not_empty[] = {{ENV_PROF_DUMP_PATH, false, 512},
                                             {ENV_TRACE_FILE_PATH, false, UBSOCKET_TRACE_FILE_PATH_LEN_MAX}};

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

    // 手动验证 ENV_ASYNC_ACCEPTOR (字符串类型)
    std::string strAsyncAccept;
    if (GetEnv(ENV_ASYNC_ACCEPTOR, strAsyncAccept)) {
        if (strAsyncAccept != "true" && strAsyncAccept != "false") {
            UBS_VLOG_ERR("Invalid value for %s: %s, expected 'true' or 'false'\n", ENV_ASYNC_ACCEPTOR,
                         strAsyncAccept.c_str());
            return UBS_INVALID_PARAM;
        }
    }

    auto &validator = Validator::Instance();
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

UBHandshakeMode HandShakeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == "tfo") {
        return UBHandshakeMode::TFO;
    } else if (typeStr == "ub_sock_opt") {
        return UBHandshakeMode::UB_SOCK_OPT;
    }
    // 如果字符串不匹配，返回默认值
    return UBHandshakeMode::TFO;
}

Result GlobalSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t envValue = 0;
    std::string strEnvValue;

    /* load from env */
    if (GetEnvAndValidate(ENV_TRACE_ENABLED, strEnvValue)) {
        UBS_TRACE_ENABLED = Func::BoolFromStr(strEnvValue);
    }
    // 正确处理 ENV_ASYNC_ACCEPTOR (字符串类型 "true"|"false")
    std::string strAsyncAccept;
    if (GetEnvAndValidate(ENV_ASYNC_ACCEPTOR, strAsyncAccept)) {
        if (Func::BoolFromStr(strAsyncAccept)) {
            UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 1; // 默认线程数
        } else {
            UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
        }
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

    if (GetEnvAndValidate(ENV_ENABLE_SHARE_JFR, strEnvValue)) {
        UBS_ENABLE_SHARE_JFR = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_SHARE_JFR_RX_QUEUE_DEPTH, envValue)) {
        UBS_SHARE_JFR_RX_QUEUE_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_SHARE_JFR_RX_O3_QUEUE_DEPTH, envValue)) {
        UBS_SHARE_JFR_RX_O3_QUEUE_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_TRANS_MODE, strEnvValue)) {
        UBS_TRANS_MODE = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_USE_BRPC_ZCOPY, strEnvValue)) {
        USE_BRPC_ZCOPY = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_UBS_TX_DEPTH, envValue)) {
        UBS_TX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_UBS_RX_DEPTH, envValue)) {
        UBS_RX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_UBS_HAND_SHAKE_MODE, strEnvValue)) {
        UBS_HAND_SHAKE_MODE = HandShakeFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_PROF_ENABLE, strEnvValue)) {
        UBS_PROF_ENABLE = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_PROF_DUMP_INTERVAL_MIN, envValue)) {
        UBS_PROF_DUMP_INTERVAL_MIN = static_cast<uint16_t>(envValue);
    }

    if (GetEnvAndValidateNotEmpty(ENV_PROF_DUMP_PATH, strEnvValue)) {
        UBS_PROF_DUMP_PATH = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_TRACE_TIME, envValue)) {
        UBS_TRACE_TIME = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_TRACE_FILE_SIZE, envValue)) {
        UBS_TRACE_FILE_SIZE = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidateNotEmpty(ENV_TRACE_FILE_PATH, strEnvValue)) {
        UBS_TRACE_FILE_PATH = strEnvValue;
    }

    if (GetEnvAndValidate(ENV_VAR_DEGRADE, strEnvValue)) {
        UBS_ENABLE_DEGRADE = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_CLI, strEnvValue)) {
        UBS_CLI_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE, strEnvValue)) {
        UBS_PROBE_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE_TIME, envValue)) {
        UBS_PROBE_MS = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_VAR_PROBE_BATCH, envValue)) {
        UBS_PROBE_BATCH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_BACKUP_LINK_ENABLED, strEnvValue)) {
        UBS_BACKUP_LINK_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    return UBS_OK;
}
} // namespace ubs
} // namespace ock