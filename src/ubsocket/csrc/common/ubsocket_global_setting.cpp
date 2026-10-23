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
#include "ubsocket_setting_validator.h"

namespace ock {
namespace ubs {
std::mutex GlobalSetting::MUTEX;
uint32_t GlobalSetting::UBS_ALLOWED_PROTOCOL = 0; /* no protocol by default */
bool GlobalSetting::UBS_NATIVE_TCP_MODE = false;  /* use ubsocket by default */
bool GlobalSetting::UBS_DIRECT_IO_ENABLE = false;
bool GlobalSetting::UBS_TRACE_ENABLED = true;
bool GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
uint32_t GlobalSetting::UBS_SPLIT_TRACE_BUF_CAPACITY = 65535;
uint32_t GlobalSetting::UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS = 10;
SplitTraceLevel GlobalSetting::UBS_SPLIT_TRACE_LEVEL = SplitTraceLevel::LEVEL_ALL;
bool GlobalSetting::UBS_CLI_ENABLED = true;
bool GlobalSetting::UBS_INITED = false;                      /* not inited by default */
std::string GlobalSetting::UBS_TRANS_MODE = "ub";            /* transport mode, from env */
ConnMode GlobalSetting::UBS_CONNECTS_MODE = ConnMode::FULL_ASYNC; /* conn mode: SYNC | HALF_ASYNC | FULL_ASYNC */
int16_t GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 1;     /* enabled by default */
bool GlobalSetting::UBS_AUTO_FALLBACK_TCP = true;
bool GlobalSetting::UBS_READV_UNLIMITED = true;
bool GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
bool GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
bool GlobalSetting::UBS_ENABLE_DEGRADE = true;
bool GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
uint32_t GlobalSetting::UBS_TX_DEPTH = 1024;
uint32_t GlobalSetting::UBS_RX_DEPTH = 2048;
UBHandshakeMode GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::UB_SOCK_OPT;
uint32_t GlobalSetting::UBS_CONNECTS_POLL_THREAD_NUM = 1; /* unified conn-establishment thread count, shared by half-async ExecutorService and full-async ConnDispatcher */
bool GlobalSetting::UBS_PROF_ENABLE = false;
std::string GlobalSetting::UBS_PROF_MODE = "fast";
uint16_t GlobalSetting::UBS_PROF_DUMP_INTERVAL_MIN = 1;
std::string GlobalSetting::UBS_PROF_DUMP_PATH = "/tmp/ubsocket/profiling";
uint64_t GlobalSetting::UBS_TRACE_TIME = UBSOCKET_TRACE_TIME_DEFAULT;
uint64_t GlobalSetting::UBS_TRACE_FILE_SIZE = UBSOCKET_TRACE_FILE_SIZE_DEFAULT;
std::string GlobalSetting::UBS_TRACE_FILE_PATH = "/tmp/ubsocket/log";
LinkSelectionPolicy GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
uint32_t GlobalSetting::UBS_PORT_COOLDOWN_SEC = 60;
uint32_t GlobalSetting::UBS_PRE_ALLOC_SYSTEM_FD_NUM = 45000; /* fd table pre-expansion target, 0 = off */

/* environment variable name */
#define ENV_CONNECTS_MODE "UBS_CONNECTS_MODE" /* conn mode: "sync" | "half_async" | "full_async" */
#define ENV_ASYNC_EPOLL "UBS_ASYNC_EPOLL_WAIT_THREAD_COUNT"
#define ENV_SHARE_JFR_LOOP_POLL_ENABLED "UBS_SHARE_JFR_LOOP_POLL_ENABLED"
#define ENV_ENABLE_SHARE_JFR "UBS_SHARE_JFR_ENABLE"
#define ENV_UBS_RX_DEPTH "UBS_RX_DEPTH"
#define ENV_UBS_TX_DEPTH "UBS_TX_DEPTH"
#define ENV_OPT_LINK_HANDSHAKE_ENABLE "UBS_OPT_LINK_HANDSHAKE_ENABLE" /* UB optimized-link handshake switch, TRUE/FALSE; TFO removed from configuration */
#define ENV_CONN_POLL_THREAD_NUM "UBS_CONNECTS_POLL_THREAD_NUM"          /* unified conn-establishment thread count (half-async + full-async shared) */
#define ENV_SERVER_HANDLER_NUM_DEPRECATED "UBSOCKET_SERVER_ASYNC_HANDLER_NUM" /* legacy name, kept for compatibility */
#define ENV_PROF_ENABLE "UBS_PROF_ENABLE"
#define ENV_PROF_MODE "UBS_PROF_MODE"
#define ENV_PROF_DUMP_INTERVAL_MIN "UBS_PROF_DUMP_INTERVAL_MIN"
#define ENV_PROF_DUMP_PATH "UBS_PROF_DUMP_FILE_PATH"
#define ENV_TRACE_TIME "UBS_MONITOR_INTERVAL"
#define ENV_TRACE_FILE_SIZE "UBS_MONITOR_FILE_SIZE"
#define ENV_TRACE_FILE_PATH "UBS_MONITOR_FILE_PATH"
#define ENV_PORT_COOLDOWN_SEC "UBS_PORT_COOLDOWN_SEC"
#define ENV_DIRECT_IO_ENABLE "UBS_DIRECT_IO_ENABLE"
#define ENV_PRE_ALLOC_SYSTEM_FD_NUM "UBS_PRE_ALLOC_SYSTEM_FD_NUM"
/* Deprecated env names (entries made internal / external entry removed, fields and defaults kept):
 * UBSOCKET_MONITOR_ENABLE, UBSOCKET_SPLIT_TRACE_ENABLE/BUF_CAPACITY/DRAIN_INTERVAL_MS/LEVEL,
 * UBSOCKET_ASYNC_CONNECTOR_ENABLE, UBSOCKET_SERVER_ASYNC_ENABLE, UBSOCKET_DEGRADE_ENABLE,
 * UBSOCKET_CLI_ENABLE, UBSOCKET_PROBE_ENABLE, UBSOCKET_BACKUP_LINK_ENABLE,
 * UBSOCKET_READV_UNLIMITED, UBSOCKET_UB_HANDSHAKE_MODE,
 * UBS_ASYNC_ACCEPT_ENABLE, UBS_ASYNC_CONNECTOR_THREAD_COUNT, UBS_ASYNC_ACCEPT_THREAD_NUM
 * (unified into UBS_CONNECTS_MODE / UBS_CONNECTS_POLL_THREAD_NUM) */

void GlobalSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {
        {ENV_ASYNC_EPOLL, false, 1, 1L},
        {ENV_UBS_RX_DEPTH, false, 2, UINT32_MAX},
        {ENV_UBS_TX_DEPTH, false, 2, UINT32_MAX},
        {ENV_PROF_DUMP_INTERVAL_MIN, false, 1, 5},
        {ENV_TRACE_TIME, false, UBSOCKET_TRACE_TIME_MIN, UBSOCKET_TRACE_TIME_MAX},
        {ENV_TRACE_FILE_SIZE, false, UBSOCKET_TRACE_FILE_SIZE_MIN, UBSOCKET_TRACE_FILE_SIZE_MAX},
        {ENV_CONN_POLL_THREAD_NUM, false, 1, UINT32_MAX},
        {ENV_SERVER_HANDLER_NUM_DEPRECATED, false, 1, UINT32_MAX},
        {ENV_PORT_COOLDOWN_SEC, false, 1, 300},
        {ENV_PRE_ALLOC_SYSTEM_FD_NUM, false, 0, 1048576},
    };

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_CONNECTS_MODE, false, "sync|half_async|full_async"},
                                    {ENV_ENABLE_SHARE_JFR, false, "true|false"},
                                    {ENV_SHARE_JFR_LOOP_POLL_ENABLED, false, "true|false"},
                                    {ENV_OPT_LINK_HANDSHAKE_ENABLE, false, "true|false"},
                                    {ENV_PROF_ENABLE, false, "true|false"},
                                    {ENV_PROF_MODE, false, "fast|ext"},
                                    {ENV_DIRECT_IO_ENABLE, false, "true|false"}};

    /* str not empty rules: name, required, maxLen */
    StrNotEmptyRule rules_str_not_empty[] = {{ENV_PROF_DUMP_PATH, false, UBSOCKET_TRACE_FILE_PATH_LEN_MAX},
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

    auto &validator = Validator::Instance();
    if (!validator.Validate(ENV_ASYNC_EPOLL, (int64_t)UBS_EPOLL_ASYNC_THREAD_COUNT, "async_epoll_thread_count")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    UBS_VLOG_DEBUG("end");
    return UBS_OK;
}

ConnMode GlobalSetting::ConnModeFromStr(const std::string &modeStr) noexcept
{
    /* StrEnumRule("sync|half_async|full_async") already rejects invalid values at read time, fallback only */
    if (modeStr == "sync") {
        return ConnMode::SYNC;
    }
    if (modeStr == "half_async") {
        return ConnMode::HALF_ASYNC;
    }
    return ConnMode::FULL_ASYNC;
}

const char *GlobalSetting::ConnModeToStr(ConnMode mode) noexcept
{
    switch (mode) {
        case ConnMode::SYNC:
            return "sync";
        case ConnMode::HALF_ASYNC:
            return "half_async";
        case ConnMode::FULL_ASYNC:
            return "full_async";
        default:
            return "full_async";
    }
}

UBHandshakeMode HandShakeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == "true" || typeStr == "ub_sock_opt") {
        return UBHandshakeMode::UB_SOCK_OPT;
    }
    /* false also falls back to UB_SOCK_OPT: TFO removed from configuration, UB optimized-link handshake is the only mode */
    return UBHandshakeMode::UB_SOCK_OPT;
}

SplitTraceLevel SplitTraceLevelFromStr(const std::string &env_str) noexcept
{
    // 1. 默认打开所有 Trace 模块
    SplitTraceLevel level = SplitTraceLevel::LEVEL_NONE;

    // 2. 如果环境变量为空或纯空格，直接返回默认的 ALL
    if (env_str.empty() || env_str.find_first_not_of(" \t") == std::string::npos) {
        return level;
    }

    std::stringstream ss(env_str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        // 去除可能存在的前后空格
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        // 3. 处理连续逗号导致的空串（如 "ubsocket,,"），直接跳过
        if (start == std::string::npos) {
            continue;
        }

        std::string level_str = token.substr(start, end - start + 1);
        UBS_VLOG_DEBUG("split trace level config str: [%s].\n", level_str.c_str());
        if (level_str == "ubsocket") {
            level = level | SplitTraceLevel::LEVEL_UBSOCKET;
        } else if (level_str == "urma") {
            level = level | SplitTraceLevel::LEVEL_URMA;
        } else if (level_str == "all") {
            level = SplitTraceLevel::LEVEL_ALL;
        } else {
            // 4. 遇到未知配置项，打印警告日志
            UBS_VLOG_WARN("Unknown split trace level config: [%s], ignor.\n", level_str.c_str());
        }
    }

    if (level == SplitTraceLevel::LEVEL_NONE) {
        UBS_VLOG_WARN("no valid level config, use default trace level all.\n");
        level = SplitTraceLevel::LEVEL_ALL;
    }
    return level;
}

Result GlobalSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t envValue = 0;
    std::string strEnvValue;

    /* load from env */
    if (GetEnvAndValidate(ENV_CONNECTS_MODE, strEnvValue)) {
        UBS_CONNECTS_MODE = ConnModeFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_ASYNC_EPOLL, envValue)) {
        UBS_EPOLL_ASYNC_THREAD_COUNT = static_cast<int16_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_ENABLE_SHARE_JFR, strEnvValue)) {
        UBS_ENABLE_SHARE_JFR = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_SHARE_JFR_LOOP_POLL_ENABLED, strEnvValue)) {
        UBS_SHARE_JFR_LOOP_POLL_ENABLED = Func::BoolFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_UBS_TX_DEPTH, envValue)) {
        UBS_TX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_UBS_RX_DEPTH, envValue)) {
        UBS_RX_DEPTH = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_OPT_LINK_HANDSHAKE_ENABLE, strEnvValue)) {
        /* true = UB optimized-link handshake; false reserved (TFO removed, UB_SOCK_OPT is the only handshake mode) */
        UBS_HAND_SHAKE_MODE = HandShakeFromStr(strEnvValue);
    }

    if (GetEnvAndValidate(ENV_CONN_POLL_THREAD_NUM, envValue)) {
        UBS_CONNECTS_POLL_THREAD_NUM = static_cast<uint32_t>(envValue);
    } else if (GetEnvAndValidate(ENV_SERVER_HANDLER_NUM_DEPRECATED, envValue)) {
        UBS_CONNECTS_POLL_THREAD_NUM = static_cast<uint32_t>(envValue);
        UBS_VLOG_WARN("UBSOCKET_SERVER_ASYNC_HANDLER_NUM is deprecated, use UBS_CONNECTS_POLL_THREAD_NUM\n");
    }
    UBS_VLOG_INFO("Current conn poll thread num, UBS_CONNECTS_POLL_THREAD_NUM: %u\n", UBS_CONNECTS_POLL_THREAD_NUM);

    if (GetEnvAndValidate(ENV_PROF_ENABLE, strEnvValue)) {
        UBS_PROF_ENABLE = Func::BoolFromStr(strEnvValue);
    }
    if (GetEnvAndValidate(ENV_PROF_MODE, strEnvValue)) {
        UBS_PROF_MODE = strEnvValue;
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

    if (GetEnvAndValidate(ENV_PORT_COOLDOWN_SEC, envValue)) {
        UBS_PORT_COOLDOWN_SEC = static_cast<uint32_t>(envValue);
    }

    if (GetEnvAndValidate(ENV_PRE_ALLOC_SYSTEM_FD_NUM, envValue)) {
        UBS_PRE_ALLOC_SYSTEM_FD_NUM = static_cast<uint32_t>(envValue); /* 0 = off; range 0..1048576 by validator */
    }

    if (GetEnvAndValidate(ENV_DIRECT_IO_ENABLE, strEnvValue)) {
        UBS_DIRECT_IO_ENABLE = Func::BoolFromStr(strEnvValue);
    }
    return UBS_OK;
}
} // namespace ubs
} // namespace ock