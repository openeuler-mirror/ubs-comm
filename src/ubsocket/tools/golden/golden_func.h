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
#ifndef UBS_COMM_GOLDEN_FUNC_H
#define UBS_COMM_GOLDEN_FUNC_H

#include "golden_common.h"
#include "ubsocket.h"

namespace golden {
class Func {
public:
    static int ProtocolFromString(const std::string &src) noexcept;

    /*
     * @brief string utilities
     */
    static std::string StrTrim(const std::string &src) noexcept;
    static std::set<std::string> StrSplit(const std::string &src, const std::string &seperator) noexcept;
    static std::string StrLowerCase(const std::string &src) noexcept;
    static void StrLowerCaseDirect(std::string &src) noexcept;
    static bool BoolFromStr(const std::string &src) noexcept;
};

inline int Func::ProtocolFromString(const std::string &src) noexcept
{
    int allowedProtocol = 0;
    auto tmpSrc = StrTrim(src);
    auto split = StrSplit(tmpSrc, "|");
    for (auto &item : split) {
        if (item == "tcp") {
            allowedProtocol |= UBS_PROTOCOL_TCP;
        }
    }

    return allowedProtocol;
}

inline std::string Func::StrTrim(const std::string &src) noexcept
{
    /* empty if not consider \t\n\r\f\v */
    size_t start = src.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }

    /* trim end */
    size_t end = src.find_last_not_of(" \t\n\r\f\v");

    return src.substr(start, end - start + 1);
}

inline std::set<std::string> Func::StrSplit(const std::string &src, const std::string &seperator) noexcept
{
    std::set<std::string> result;

    /* no split */
    if (seperator.empty() || src.empty()) {
        result.insert(src);
        return result;
    }

    std::string::size_type pos = 0;
    std::string::size_type pre = 0;

    while ((pos = src.find(seperator, pre)) != std::string::npos) {
        std::string sub = src.substr(pre, pos - pre);
        if (!sub.empty()) {
            result.insert(sub);
        }
        pre = pos + seperator.length();
    }

    /* last part */
    std::string last = src.substr(pre);
    if (!last.empty()) {
        result.insert(last);
    }

    return result;
}

inline std::string Func::StrLowerCase(const std::string &src) noexcept
{
    std::string res = src;
    for (char &ch : res) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return res;
}

inline void Func::StrLowerCaseDirect(std::string &src) noexcept
{
    for (char &ch : src) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
}

inline bool Func::BoolFromStr(const std::string &src) noexcept
{
    return StrLowerCase(StrTrim(src)) == "true";
}
} // namespace golden

#endif // UBS_COMM_GOLDEN_FUNC_H
