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
#ifndef UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
#define UBS_COMM_UBSOCKET_GLOBAL_SETTING_H

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <string>
#include <type_traits>

#include "include/ubsocket_def.h"
#include "ubsocket_defines.h"
#include "ubsocket_errno.h"
#include "ubsocket_logger.h"
#include "ubsocket_setting_validator.h"

namespace ock {
namespace ubs {

/* conn mode tier (enum parsed from env UBS_CONNECTS_MODE / gflag ubsocket_connects_mode via interface) */
enum class ConnMode : uint8_t {
    SYNC = 0,       /* sync establishment */
    HALF_ASYNC = 1, /* server half-async thread pool (client unsupported, falls back to sync) */
    FULL_ASYNC = 2  /* server full-async reactor + client full-async connect */
};

class GlobalSetting {
public:
    /* conn mode getter (server/client share one UBS_CONNECTS_MODE tier), callers compare with ConnMode::XXX directly */
    static ConnMode GetConnMode() noexcept;
    /* client-role policy: true only in full_async tier (no half-async impl on client connect path, half_async falls back to sync) */
    static bool ClientAsyncConnectEnabled() noexcept;
    static bool AsyncEpollEnabled() noexcept;

    /* interface: conn mode string (from gflag/env) <-> enum conversion */
    static ConnMode ConnModeFromStr(const std::string &modeStr) noexcept;
    static const char *ConnModeToStr(ConnMode mode) noexcept;

    static uint16_t GetTxDepth() noexcept;

public:
    /* disable constructor */
    GlobalSetting() = delete;

    /**
     * @brief verify setting
     */
    static Result VerifySetting() noexcept;

    /**
     * @brief Load envs
     */
    static Result LoadEnv() noexcept;

    /**
      * @brief Get env, only provide three types,
      * use int64_t for int16_t, uint16_t, int32_t, uint32_t, int64_t
      * use double for float and double
      */
    static bool GetEnv(const std::string &name, int64_t &out) noexcept;
    static bool GetEnv(const std::string &name, float &out) noexcept;
    static bool GetEnv(const std::string &name, std::string &out) noexcept;

    /**
     * @brief Get env and validate by rule
     *
     * @param name         [in] name of the env and rule
     * @param out          [in] converted value
     * @return true if have env and validate result is ok
     * false if no env
     * false if has env but validate failed
     */
    static bool GetEnvAndValidate(const std::string &name, int64_t &out) noexcept;
    static bool GetEnvAndValidate(const std::string &name, float &out) noexcept;
    static bool GetEnvAndValidate(const std::string &name, std::string &out) noexcept;
    static bool GetEnvAndValidateNotEmpty(const std::string &name, std::string &out) noexcept;

    /**
     * @brief Add setting verify rules
     */
    static void AddRules() noexcept;

public:
    static std::mutex MUTEX;
    static uint32_t UBS_ALLOWED_PROTOCOL;              /* allowed protocol, from API */
    static bool UBS_NATIVE_TCP_MODE;                   /* native tcp mode, pass all logic of this library, from API */
    static bool UBS_DIRECT_IO_ENABLE;                  /* if pass unmanaged fd through to libc directly, from env */
    static bool UBS_TRACE_ENABLED;                     /* if enable tracing, from env */
    static bool UBS_SPLIT_TRACE_ENABLED;               /* if enable split trace, from env */
    static uint32_t UBS_SPLIT_TRACE_BUF_CAPACITY;      /* split trace buffer capacity per socket, from env */
    static uint32_t UBS_SPLIT_TRACE_DRAIN_INTERVAL_MS; /* split trace drain interval in ms, from env */
    static SplitTraceLevel UBS_SPLIT_TRACE_LEVEL;      /* split trace level bitmask: ubsocket / urma */
    static bool UBS_CLI_ENABLED;
    static bool UBS_INITED;                          /* if ubsocket initialized, from API */
    static std::string UBS_TRANS_MODE;               /* transport mode, from env */
    static ConnMode UBS_CONNECTS_MODE;               /* conn mode enum (server/client share one tier): SYNC | HALF_ASYNC(client falls back to SYNC) | FULL_ASYNC */
    static int16_t UBS_EPOLL_ASYNC_THREAD_COUNT;     /* if enable async epoll_wait, from API override by env */
    static bool UBS_AUTO_FALLBACK_TCP;               /* if auto fallback to tcp, from API override by from env */
    static bool UBS_READV_UNLIMITED;                 /* if enable readv limit report, from env */
    static bool UBS_ENABLE_SHARE_JFR;                /* if enable share jfr, from env */
    static bool UBS_SHARE_JFR_LOOP_POLL_ENABLED;     /* if enable share jfr loop poll, from env */
    static bool UBS_ENABLE_DEGRADE;                  /* if enable degrade, from env */
    static bool UBS_BACKUP_LINK_ENABLED;             /* if enable primary-standby switch, from env */
    static uint32_t UBS_TX_DEPTH;                    /* tx queue depth, from env */
    static uint32_t UBS_RX_DEPTH;                    /* rx queue depth, from env */
    static bool UBS_PROF_ENABLE;
    static std::string UBS_PROF_MODE; /* profiling mode: "fast" or "ext" */
    static std::string UBS_PROF_DUMP_PATH;
    static uint16_t UBS_PROF_DUMP_INTERVAL_MIN;

    static UBHandshakeMode UBS_HAND_SHAKE_MODE;
    static uint32_t UBS_CONNECTS_POLL_THREAD_NUM; /* unified conn-establishment thread count, shared by half-async ExecutorService and full-async ConnDispatcher */

    /* trace statistic related */
    static uint64_t UBS_TRACE_TIME;
    static uint64_t UBS_TRACE_FILE_SIZE;
    static std::string UBS_TRACE_FILE_PATH;

    // 通信链路选择，不直接由外部环境变量控制
    static LinkSelectionPolicy LINK_SELECTION_POLICY;

    // 故障 port 口冷却时间
    static uint32_t UBS_PORT_COOLDOWN_SEC;

    /* fd 表预扩容目标（见 ubsocket_fd_table.h）：ubsocket_init 一开始就把进程 fd 表撑到能容纳这个 fd 号，
     * 避免建链途中多线程扩容等 RCU 宽限期。按"单进程最大链接数 × 每条链的 fd 数"取值；45000 对应 65536 档。
     * 0 = 关闭。env UBSOCKET_PRE_ALLOC_SYSTEM_FD_NUM，范围 0..1048576。 */
    static uint32_t UBS_PRE_ALLOC_SYSTEM_FD_NUM;
};

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, int64_t &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    // Regex mismatch: not an integer, e.g. "2min" will fail validation.
    static const std::regex num_regex("^-?[0-9]+$");
    if (!std::regex_match(envStr, num_regex)) {
        UBS_VLOG_ERR("Invalid value for %s: %s, should be an integer.\n", name.c_str(), envStr.c_str());
        return false;
    }
    try {
        out = static_cast<int64_t>(std::stol(envStr));
        return true;
    } catch (...) {
        UBS_VLOG_ERR("Invalid value for %s: %s, should be an integer.\n", name.c_str(), envStr.c_str());
        return false;
    }
}

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, float &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    // Validate number format (supports integers and decimals)
    // If regex mismatch, it is not a valid number → fail (e.g., "2.31min")
    static const std::regex float_regex("^-?[0-9]+(\\.[0-9]+)?$");
    if (!std::regex_match(envStr, float_regex)) {
        UBS_VLOG_ERR("Invalid value for %s: %s, should be an number.\n", name.c_str(), envStr.c_str());
        return false;
    }

    try {
        out = static_cast<int64_t>(std::stod(envStr));
        return true;
    } catch (...) {
        UBS_VLOG_ERR("Invalid value for %s: %s, should be an number.\n", name.c_str(), envStr.c_str());
        return false;
    }
}

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, std::string &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    out = envValue;
    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, int64_t &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().Validate(name, static_cast<int64_t>(out))) {
        /* invalid env value is non-fatal: value not assigned, valid default kept, so warn instead of err */
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, float &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().Validate(name, static_cast<float>(out))) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidate(const std::string &name, std::string &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().ValidateStrEnum(name, out)) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE bool GlobalSetting::GetEnvAndValidateNotEmpty(const std::string &name, std::string &out) noexcept
{
    if (!GetEnv(name, out)) {
        return false;
    }

    if (!Validator::Instance().ValidateStrEmpty(name, out)) {
        UBS_SLOG_WARN(Validator::Instance().LastErrMsg());
        return false;
    }

    return true;
}

ALWAYS_INLINE ConnMode GlobalSetting::GetConnMode() noexcept
{
    return UBS_CONNECTS_MODE;
}

ALWAYS_INLINE bool GlobalSetting::ClientAsyncConnectEnabled() noexcept
{
    /* no half-async establishment on client (ExecutorService consumed by acceptor only), half_async tier falls back to sync, so check full_async only */
    return GetConnMode() == ConnMode::FULL_ASYNC;
}

ALWAYS_INLINE bool GlobalSetting::AsyncEpollEnabled() noexcept
{
    return UBS_EPOLL_ASYNC_THREAD_COUNT > 0;
}

ALWAYS_INLINE uint16_t GlobalSetting::GetTxDepth() noexcept
{
    // TODO
    return 0;
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
