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
#ifndef UBS_COMM_GOLDEN_COMMON_H
#define UBS_COMM_GOLDEN_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "golden_log.h"

namespace golden {

using ParamMap = std::map<std::string, std::string>;
static std::string program = "golden";

struct SubCmd {
    std::string name;
    std::string help_str;
};

#define SUB_CMD_MINUS_H "-h"
#define SUB_CMD_PINGPONG "pp"
#define SUB_CMD_DATA "data"

static SubCmd cmds[] = {{SUB_CMD_PINGPONG, "pingpong test to check if network is ok"},
                        {SUB_CMD_DATA, "pingpong test to check if data transfer is correct"}};

#define PARAM_ROLE "role"           /* server or client */
#define PARAM_IP "ip"               /* ip */
#define PARAM_PORT "port"           /* port */
#define PARAM_PROTOCOL "protocol"   /* protocol, tcp/ub_tp/ etc */
#define PARAM_DATA_SIZE "data-size" /* data size of single send */
#define PARAM_TIMES "times"         /* loop times */

enum ParamDataType : int8_t {
    PDT_NONE = 0,
    PDT_INT64,
    PDT_STR,
    PDT_STR_ENUM,
    PDT_FLOAT,
    PDT_COUNT
};

struct Param {
    std::string name;
    ParamDataType dataType = PDT_NONE;
    bool required = false;

    struct {
        int64_t defaultValue = 0;
        int64_t min = 0;
        int64_t max = 0;
        int64_t value;
    } int64Rule;

    struct {
        float defaultValue = 0.0f;
        float min = 0.0f;
        float max = 0.0f;
        float value = 0.0f;
    } floatRule;

    struct {
        std::string defaultValue;
        std::string allEnum;
        std::string value;
    } strRule;

    std::string help;

    Param() = default;
    Param(const std::string &n, ParamDataType t, bool r, int64_t dv, int64_t mi, int64_t ma, const std::string &h)
        : name(n),
          dataType(t),
          required(r),
          help(h)
    {
        int64Rule.defaultValue = dv;
        int64Rule.min = mi;
        int64Rule.max = ma;
        int64Rule.value = dv;
    }

    Param(const std::string &n, ParamDataType t, bool r, float dv, float mi, float ma, const std::string &h)
        : name(n),
          dataType(t),
          required(r),
          help(h)
    {
        floatRule.defaultValue = dv;
        floatRule.min = mi;
        floatRule.max = ma;
        floatRule.value = dv;
    }

    Param(const std::string &n, ParamDataType t, bool r, const std::string &defaultVal, const std::string &enumList,
          const std::string &h)
        : name(n),
          dataType(t),
          required(r),
          help(h)
    {
        strRule.defaultValue = defaultVal;
        strRule.allEnum = enumList;
        strRule.value = defaultVal;
    }

    bool IsValueValid() const noexcept
    {
        switch (dataType) {
            case PDT_INT64:
                return IsInt64ValueValid();

            case PDT_FLOAT:
                return IsFloatValueValid();
            case PDT_STR:
                return !strRule.value.empty();

            case PDT_STR_ENUM:
                return IsStrEnumValueValid(strRule.value);

            default:
                return false;
        }
    }

    bool SetValueFromString(const std::string &str) noexcept
    {
        switch (dataType) {
            case PDT_INT64:
                return SetInt64ValueFromString(str);
            case PDT_FLOAT:
                return SetFloatValueFromString(str);
            case PDT_STR:
            case PDT_STR_ENUM: {
                strRule.value = ToLowerCase(str);
                return true;
            }
            default:
                return false;
        }
    }

    std::string RequiredError() noexcept
    {
        switch (dataType) {
            case PDT_INT64:
                return "param '" + name + "' is required, type is number, range [" + std::to_string(int64Rule.min) +
                       "-" + std::to_string(int64Rule.max) + "]";
            case PDT_FLOAT:
                return "param '" + name + "' is required, type is float, range [" + std::to_string(floatRule.min) +
                       "-" + std::to_string(floatRule.max) + "]";
            case PDT_STR:
                return "param '" + name + "' is required, type is str, which should not be empty";
            case PDT_STR_ENUM: {
                return "param '" + name + "' is required, type is str, which should be one of '" + strRule.allEnum +
                       "'";
            }
            default:
                return "";
        }
    }

    std::string InvalidError() noexcept
    {
        switch (dataType) {
            case PDT_INT64:
                return "invalid value for param '" + name + "', type is number, range [" +
                       std::to_string(int64Rule.min) + "-" + std::to_string(int64Rule.max) + "]";
            case PDT_FLOAT:
                return "invalid value for param '" + name + "', type is float, range [" +
                       std::to_string(floatRule.min) + "-" + std::to_string(floatRule.max) + "]";
            case PDT_STR:
                return "invalid value for param '" + name + "', type is str, which should not be empty";
            case PDT_STR_ENUM: {
                return "invalid value for param '" + name + "',  type is str, which should be one of '" +
                       strRule.allEnum + "'";
            }
            default:
                return "";
        }
    }

    std::string HelpString() noexcept
    {
        std::string required = this->required ? "required, " : "optional, ";
        switch (dataType) {
            case PDT_INT64:
                return required + "type: number, range: [" + std::to_string(int64Rule.min) + "-" +
                       std::to_string(int64Rule.max) + "], " + help;
            case PDT_FLOAT:
                return required + "type: number, range: [" + std::to_string(floatRule.min) + "-" +
                       std::to_string(floatRule.max) + "], " + help;
            case PDT_STR:
                return required + "type: str, which should not be empty, " + help;
            case PDT_STR_ENUM: {
                return required + "type: str, which should be one of '" + strRule.allEnum + "', " + help;
            }
            default:
                return "";
        }
    }

private:
    bool SetInt64ValueFromString(const std::string &src) noexcept
    {
        std::string lower_str = ToLowerCase(src);
        int64_t val = 0;

        if (!StringToInt64(lower_str, val)) {
            return false;
        }

        int64Rule.value = val;
        return true;
    }

    bool SetFloatValueFromString(const std::string &src) noexcept
    {
        std::string lower_str = ToLowerCase(src);
        float val = 0.0f;

        if (!StringToFloat(lower_str, val)) {
            return false;
        }

        floatRule.value = val;
        return true;
    }

    bool IsInt64ValueValid() const noexcept
    {
        return int64Rule.value >= int64Rule.min && int64Rule.value <= int64Rule.max;
    }

    bool IsFloatValueValid() const noexcept
    {
        return floatRule.value >= floatRule.min && floatRule.value <= floatRule.max;
    }

    bool IsStrEnumValueValid(const std::string &src) const noexcept
    {
        std::string target = "|" + src + "|";

        std::string enumStr = "|" + strRule.allEnum + "|";

        return (enumStr.find(target) != std::string::npos);
    }

    static std::string ToLowerCase(const std::string &src)
    {
        std::string res = src;
        for (char &ch : res) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return res;
    }

    static bool StringToInt64(const std::string &src, int64_t &outVal) noexcept
    {
        try {
            outVal = std::stoll(src);
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool StringToFloat(const std::string &src, float &outVal) noexcept
    {
        try {
            outVal = std::stof(src);
            return true;
        } catch (...) {
            return false;
        }
    }
};

using ParamRuleMap = std::map<std::string, Param>;

} // namespace golden

#endif // UBS_COMM_GOLDEN_COMMON_H
