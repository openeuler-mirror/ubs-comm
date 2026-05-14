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
#include <cstdlib>
#include <mutex>
#include <string>
#include <type_traits>

#include "ubsocket_def.h"
#include "ubsocket_errno.h"

namespace ock {
namespace ubs {
class GlobalSetting {
public:
    /* if force fallback native os */
    static bool NativeTcpMode() noexcept;

    static bool AsyncAcceptorEnabled() noexcept;
    static bool AsyncConnectorEnabled() noexcept;
    static bool AsyncEpollEnabled() noexcept;

    static uint16_t GetTxDepth() noexcept;

public:
    /**
     * @brief verify setting
     */
    static Result VerifySetting() noexcept;

    /**
     * @brief Load envs
     */
    static Result LoadEnv() noexcept;

    /* disable constructor */
    GlobalSetting() = delete;

    /**
      * @brief Get env, only provide three types,
      * use int64_t for int16_t, uint16_t, int32_t, uint32_t, int64_t
      * use double for float and double
      */
    static bool GetEnv(const std::string &name, int64_t &out) noexcept;
    static bool GetEnv(const std::string &name, double &out) noexcept;
    static bool GetEnv(const std::string &name, std::string &out) noexcept;

    /**
     * @brief Add setting verify rules
     */
    static void AddRules() noexcept;

public:
    static std::mutex MUTEX;
    static uint32_t UBS_ALLOWED_PROTOCOL;            /* allowed protocol, from API */
    static bool UBS_NATIVE_TCP_MODE;                 /* native tcp mode, pass all logic of this library, from API */
    static bool UBS_TRACE_ENABLED;                   /* if enable tracing, from env */
    static bool UBS_INITED;                          /* if ubsocket initialized, from API */
    static int16_t UBS_ACCEPTOR_ASYNC_THREAD_COUNT;  /* if enable async acceptor, from API override by env */
    static int16_t UBS_CONNECTOR_ASYNC_THREAD_COUNT; /* if enable async connector, from API override by env */
    static int16_t UBS_EPOLL_ASYNC_THREAD_COUNT;     /* if enable async epoll_wait, from API override by env */
};

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, int64_t &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    try {
        out = static_cast<int64_t>(std::stol(envStr));
        return true;
    } catch (...) {
        return false;
    }
}

ALWAYS_INLINE bool GlobalSetting::GetEnv(const std::string &name, double &out) noexcept
{
    const char *envValue = getenv(name.c_str());
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    try {
        out = static_cast<int64_t>(std::stod(envStr));
        return true;
    } catch (...) {
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
}

ALWAYS_INLINE bool GlobalSetting::NativeTcpMode() noexcept
{
    return UBS_NATIVE_TCP_MODE;
}

ALWAYS_INLINE bool GlobalSetting::AsyncAcceptorEnabled() noexcept
{
    return UBS_ACCEPTOR_ASYNC_THREAD_COUNT > 0;
}

ALWAYS_INLINE bool GlobalSetting::AsyncConnectorEnabled() noexcept
{
    return UBS_CONNECTOR_ASYNC_THREAD_COUNT > 0;
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
