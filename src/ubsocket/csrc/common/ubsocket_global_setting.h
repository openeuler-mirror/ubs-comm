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
    /**
     * @brief Load envs
     */
    static Result LoadEnv() noexcept;

    /* disable constructor */
    GlobalSetting() = delete;

    /**
      * @brief Get env with default value
      *
      * @tparam T            [in] type of value
      * @param name          [in] string of the env
      * @param out           [out] value to be set
      * @return true has env set
      */
    template <typename T>
    static bool GetEnv(const char *name, T &out) noexcept;

public:
    static std::mutex MUTEX;
    static uint32_t UBS_ALLOWED_PROTOCOL;         /* allowed protocol, from API */
    static bool UBS_TRACE_ENABLED;                /* if enable tracing, from env */
    static bool UBS_INITED;                       /* if ubsocket initialized, from API */
    static bool UBS_ACCEPTOR_ASYNC_ENABLED;       /* if enable async acceptor, from API override by env */
    static bool UBS_CONNECTOR_ASYNC_ENABLED;      /* if enable async connector, from API override by env */
    static bool UBS_EPOLL_ASYNC_ENABLED;          /* if enable async epoll_wait, from API override by env */
    static u_external_lock_ops_t *lock_ops;       /* external lock operations, from API */
    static u_external_rw_lock_ops_t *rw_lock_ops; /* external lock operations, from API */
    static u_external_semaphore_ops_t *sem_ops;   /* external lock operations, from API */

private:
    static void AddRules() noexcept;
};

template <typename T>
ALWAYS_INLINE bool GlobalSetting::GetEnv(const char *name, T &out) noexcept
{
    if (name == nullptr) {
        return false;
    }

    const char *envValue = getenv(name);
    if (envValue == nullptr) {
        return false;
    }

    std::string envStr(envValue);
    T result;
    try {
        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            out = static_cast<T>(std::stod(envStr));
        } else if constexpr (std::is_integral_v<T>) {
            out = static_cast<T>(std::stoi(envStr));
        } else if constexpr (std::is_same_v<T, std::string>) {
            out = envStr;
        } else {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_integral_v<T>,
                          "Unsupported type for GetEnv");
        }
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
