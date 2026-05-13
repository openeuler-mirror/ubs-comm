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
#ifndef UBS_COMM_UBSOCKET_SETTING_VALIDATION_H
#define UBS_COMM_UBSOCKET_SETTING_VALIDATION_H

#include <map>
#include <set>

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {

template <typename DType>
struct NumRule {
    bool required = false;
    DType min;
    DType max;
    DType defaultValue;
};

class Validator {
public:
    static Validator &Instance()
    {
        static Validator instance;
        return instance;
    }

public:
    /**
     * @brief Required setting
     *
     * @param key          [in] the key to be checked
     * @return true if required to be set
     */
    bool Required(const std::string &key) const noexcept;

    /**
     * @brief Validate the value with key
     *
     * @tparam T           [in] data type
     * @param key          [in] key to find its rule
     * @param value        [in] value to be validated
     * @return true if passed, false if not passed, LastErrMsg() can be used to get the validation error message
     */
    template <typename T>
    bool Validate(const std::string key, const T &value) noexcept;

    /**
     * @brief Get last error message
     * @return err msg
     */
    const std::string &LastErrMsg() const noexcept;

    /**
     * @brief Add number rule
     *
     * @tparam DType       [in] data type
     * @param key          [in] key name
     * @param rule         [in] rule
     */
    template <typename DType>
    void AddNumRule(const std::string &key, const NumRule<DType> &rule) noexcept;

private:
    std::map<std::string, NumRule<uint32_t>> uint32_rules_;
    std::map<std::string, NumRule<int32_t>> int32_rules_;
    std::map<std::string, NumRule<uint64_t>> uint64_rules_;
    std::map<std::string, NumRule<int64_t>> int64_rules_;

    std::set<std::string> required_set;

    std::string last_error_msg;
};

ALWAYS_INLINE bool Validator::Required(const std::string &key) const noexcept
{
    auto iter = required_set.find(key);
    if (iter == required_set.end()) {
        return false;
    }

    return true;
}

template <typename T>
ALWAYS_INLINE bool Validator::Validate(const std::string &key, const T &value) noexcept
{
    if constexpr (std::is_same_v<T, uint32_t>) {
        auto iter = uint32_rules_.find(key);
        if (iter == uint32_rules_.end()) {
            last_error_msg = "No rule exists for '" + key + "'";
            return false;
        }

        if (value < iter->second.min || value > iter->second.max) {
            last_error_msg = "Invalid value for '" + key + "', should be between " + std::to_string(iter->second.min) +
                             " and " + std::to_string(iter->second.max);
            return false;
        }

        return true;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        auto iter = int32_rules_.find(key);
        if (iter == int32_rules_.end()) {
            last_error_msg = "No rule exists for '" + key + "'";
            return false;
        }

        if (value < iter->second.min || value > iter->second.max) {
            last_error_msg = "Invalid value for '" + key + "', should be between " + std::to_string(iter->second.min) +
                             " and " + std::to_string(iter->second.max);
            return false;
        }

        return true;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        auto iter = uint64_rules_.find(key);
        if (iter == uint64_rules_.end()) {
            last_error_msg = "No rule exists for '" + key + "'";
            return false;
        }

        if (value < iter->second.min || value > iter->second.max) {
            last_error_msg = "Invalid value for '" + key + "', should be between " + std::to_string(iter->second.min) +
                             " and " + std::to_string(iter->second.max);
            return false;
        }

        return true;
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        auto iter = int64_rules_.find(key);
        if (iter == int64_rules_.end()) {
            last_error_msg = "No rule exists for '" + key + "'";
            return false;
        }

        if (value < iter->second.min || value > iter->second.max) {
            last_error_msg = "Invalid value for '" + key + "', should be between " + std::to_string(iter->second.min) +
                             " and " + std::to_string(iter->second.max);
            return false;
        }

        return true;
    }

    return false;
}

ALWAYS_INLINE const std::string &Validator::LastErrMsg() const noexcept
{
    return last_error_msg;
}

template <typename DType>
ALWAYS_INLINE void Validator::AddNumRule(const std::string &key, const NumRule<DType> &rule) noexcept
{
    if (rule.required) {
        required_set.emplace(key);
    }

    if constexpr (std::is_same_v<T, uint32_t>) {
        uint32_rules_.emplace(key, rule);
    } else if constexpr (std::is_same_v<T, int32_t>) {
        int32_rules_.emplace(key, rule);
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        uint64_rules_.emplace(key, rule);
    } else if constexpr (std::is_same_v<T, uint64_t>) {
        int64_rules_.emplace(key, rule);
    }
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SETTING_VALIDATION_H
