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
#include "ubsocket_functions.h"

namespace ock {
namespace ubs {

struct Int64Rule {
    std::string key;
    bool required = false;
    int64_t min = 0;
    int64_t max = 0;

    Int64Rule() = default;

    Int64Rule(const std::string &pKey, bool pRequired, int64_t pMin, int64_t pMax)
        : key(pKey),
          required(pRequired),
          min(pMin),
          max(pMax)
    {
    }

    friend std::ostream &operator<<(std::ostream &os, const Int64Rule &o)
    {
        os << "key: " << o.key << ", required: " << o.required << ", min: " << o.min << ", max: " << o.max;
        return os;
    }
};

struct FloatRule {
    std::string key;
    bool required = false;
    float min = 0.0;
    float max = 0.0;

    FloatRule() = default;

    FloatRule(const std::string &pKey, bool pRequired, float pMin, float pMax)
        : key(pKey),
          required(pRequired),
          min(pMin),
          max(pMax)
    {
    }

    friend std::ostream &operator<<(std::ostream &os, const FloatRule &o)
    {
        os << "key: " << o.key << ", required: " << o.required << ", min: " << o.min << ", max: " << o.max;
        return os;
    }
};

struct StrEnumRule {
    std::string key;
    bool required = false;
    std::string allEnum;

    StrEnumRule() = default;
    StrEnumRule(const std::string &pKey, bool pRequired, const std::string &pAllEnum)
        : key(pKey),
          required(pRequired),
          allEnum(pAllEnum)
    {
        /*
         * str enum rule: true|false
         * we added '|' at the start place and end place
         * when doing validate:
         * case1: single value, we do string::find with '|xx|'
         * case2: multiple values, we do split firstly, then do string::find firstly
         */
        MakeSortedAllEnum();
    }

    bool Validate(const std::string &value) noexcept;

    void MakeSortedAllEnum() noexcept;

    friend std::ostream &operator<<(std::ostream &os, const StrEnumRule &o)
    {
        os << "key: " << o.key << ", required: " << o.required << ", allEum: " << o.allEnum;
        return os;
    }
};

struct StrNotEmptyRule {
    std::string key;
    bool required = false;
    uint32_t maxLen = UINT32_MAX;

    StrNotEmptyRule() = default;
    StrNotEmptyRule(const std::string &pKey, bool pRequired) : key(pKey), required(pRequired) {}
    StrNotEmptyRule(const std::string &pKey, bool pRequired, uint32_t pMaxLen)
        : key(pKey),
          required(pRequired),
          maxLen(pMaxLen)
    {
    }

    bool Validate(const std::string &value) noexcept;

    friend std::ostream &operator<<(std::ostream &os, const StrNotEmptyRule &o)
    {
        os << "key: " << o.key << ", required: " << o.required;
        return os;
    }
};

ALWAYS_INLINE bool StrEnumRule::Validate(const std::string &value) noexcept
{
    /* split */
    auto ordered = Func::StrSplit(value, "|");

    /* match one by one in lower case */
    for (auto &item : ordered) {
        auto tmp = "|" + Func::StrTrim(item) + "|";
        Func::StrLowerCaseDirect(tmp);
        if (allEnum.find(tmp) == std::string::npos) {
            return false;
        }
    }

    return true;
}

ALWAYS_INLINE bool StrNotEmptyRule::Validate(const std::string &value) noexcept
{
    /* split */
    return value.size() <= maxLen;
}

ALWAYS_INLINE void StrEnumRule::MakeSortedAllEnum() noexcept
{
    /* change to lower case and split */
    Func::StrLowerCaseDirect(allEnum);
    auto ordered = Func::StrSplit(allEnum, "|");

    /* loop and compose, skip empty string */
    std::string result = "|";
    for (auto &item : ordered) {
        auto trimItem = Func::StrTrim(item);
        if (!trimItem.empty()) {
            result += trimItem + "|";
        }
    }

    /* here we already have sorted enum string, for example |aa|ab|cd|dd| */
    allEnum = result;
}

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
     * @param key          [in] key to find its rule
     * @param value        [in] value to be validated
     * @return true if passed, false if not passed, LastErrMsg() can be used to get the validation error message
     */
    bool Validate(const std::string &key, int64_t value, const std::string &key4log = "") noexcept;
    bool Validate(const std::string &key, float value, const std::string &key4log = "") noexcept;
    bool ValidateStrEnum(const std::string &key, const std::string &value, const std::string &key4log = "") noexcept;
    bool ValidateStrEmpty(const std::string &key, const std::string &value, const std::string &key4log = "") noexcept;

    /**
     * @brief Get last error message
     * @return err msg
     */
    const std::string &LastErrMsg() const noexcept;

    /**
     * @brief Add number rule
     *
     * @param rule         [in] rule
     */
    void AddNumRule(const Int64Rule &rule) noexcept;
    void AddFloatRule(const FloatRule &rule) noexcept;
    void AddStrEnumRule(const StrEnumRule &rule) noexcept;
    void AddStrNotEmtpyRule(const StrNotEmptyRule &rule) noexcept;

    std::string DumpString() noexcept;

private:
    std::map<std::string, Int64Rule> int64_rules_;
    std::map<std::string, FloatRule> float_rules_;
    std::map<std::string, StrEnumRule> str_enum_rules_;
    std::map<std::string, StrNotEmptyRule> str_not_empty_rules_;

    std::set<std::string> required_set_;

    std::string last_error_msg_;
};

ALWAYS_INLINE bool Validator::Required(const std::string &key) const noexcept
{
    auto iter = required_set_.find(key);
    if (iter == required_set_.end()) {
        return false;
    }

    return true;
}

ALWAYS_INLINE bool Validator::Validate(const std::string &key, int64_t value, const std::string &key4log) noexcept
{
    auto &tmpKey = key4log.empty() ? key : key4log;

    auto iter = int64_rules_.find(key);
    if (iter == int64_rules_.end()) {
        last_error_msg_ = "No rule exists for '" + tmpKey + "'";
        return false;
    }

    if (value < iter->second.min || value > iter->second.max) {
        last_error_msg_ = "Invalid value for '" + tmpKey + "', should be between " + std::to_string(iter->second.min) +
                          " and " + std::to_string(iter->second.max);
        return false;
    }

    return true;
}

ALWAYS_INLINE bool Validator::Validate(const std::string &key, float value, const std::string &key4log) noexcept
{
    auto &tmpKey = key4log.empty() ? key : key4log;

    auto iter = float_rules_.find(key);
    if (iter == float_rules_.end()) {
        last_error_msg_ = "No rule exists for '" + tmpKey + "'";
        return false;
    }

    if (Func::FloatLessThan(value, iter->second.min) || Func::FloatLargerThan(value, iter->second.max)) {
        last_error_msg_ = "Invalid value for '" + tmpKey + "', should be between " + std::to_string(iter->second.min) +
                          " and " + std::to_string(iter->second.max);
        return false;
    }

    return true;
}

ALWAYS_INLINE bool Validator::ValidateStrEnum(const std::string &key, const std::string &value,
                                              const std::string &key4log) noexcept
{
    auto &tmpKey = key4log.empty() ? key : key4log;

    auto iter = str_enum_rules_.find(key);
    if (iter == str_enum_rules_.end()) {
        last_error_msg_ = "No rule exists for '" + tmpKey + "'";
        return false;
    }

    if (!(iter->second.Validate(value))) {
        last_error_msg_ = "Invalid value for '" + tmpKey + "', should be one of '" + iter->second.allEnum + "'";
        return false;
    }

    return true;
}

ALWAYS_INLINE bool Validator::ValidateStrEmpty(const std::string &key, const std::string &value,
                                               const std::string &key4log) noexcept
{
    auto &tmpKey = key4log.empty() ? key : key4log;

    auto iter = str_not_empty_rules_.find(key);
    if (iter == str_not_empty_rules_.end()) {
        last_error_msg_ = "No rule exists for '" + tmpKey + "'";
        return false;
    }

    if (value.empty()) {
        last_error_msg_ = "Invalid value for '" + tmpKey + "', which should be not emtpy";
        return false;
    }

    if (!(iter->second.Validate(value))) {
        last_error_msg_ =
            "Invalid value for '" + tmpKey + "', should be max length " + std::to_string(iter->second.maxLen);
        return false;
    }

    return true;
}

ALWAYS_INLINE const std::string &Validator::LastErrMsg() const noexcept
{
    return last_error_msg_;
}

ALWAYS_INLINE void Validator::AddNumRule(const Int64Rule &rule) noexcept
{
    const std::string &key = rule.key;
    if (rule.required) {
        required_set_.emplace(key);
    }

    int64_rules_.emplace(key, rule);
}

ALWAYS_INLINE void Validator::AddFloatRule(const FloatRule &rule) noexcept
{
    const std::string &key = rule.key;
    if (rule.required) {
        required_set_.emplace(key);
    }

    float_rules_.emplace(key, rule);
}

ALWAYS_INLINE void Validator::AddStrEnumRule(const StrEnumRule &rule) noexcept
{
    const std::string &key = rule.key;
    if (rule.required) {
        required_set_.emplace(key);
    }

    str_enum_rules_.emplace(key, rule);
}

ALWAYS_INLINE void Validator::AddStrNotEmtpyRule(const ock::ubs::StrNotEmptyRule &rule) noexcept
{
    const std::string &key = rule.key;
    if (rule.required) {
        required_set_.emplace(key);
    }

    str_not_empty_rules_.emplace(key, rule);
}

ALWAYS_INLINE std::string Validator::DumpString() noexcept
{
    std::ostringstream oss;
    oss << "\n  int rules:";
    for (auto &item : int64_rules_) {
        oss << std::endl << "    " << item.second;
    }

    oss << "\n float rules:";
    for (auto &item : float_rules_) {
        oss << std::endl << "    " << item.second;
    }

    oss << "\n str enum rules:";
    for (auto &item : str_enum_rules_) {
        oss << std::endl << "    " << item.second;
    }

    oss << "\n str not empty rules:";
    for (auto &item : str_not_empty_rules_) {
        oss << std::endl << "    " << item.second;
    }

    return oss.str();
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SETTING_VALIDATION_H
