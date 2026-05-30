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
#ifndef UBS_COMM_UBSOCKET_FUNCTIONS_H
#define UBS_COMM_UBSOCKET_FUNCTIONS_H

#include <fstream>

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {

constexpr float EPS_F = 1e-6f;

class Func {
public:
    /**
     * @brief Float compare functions
     */
    static bool FloatLargerThan(float a, float b) noexcept;
    static bool FloatLessThan(float a, float b) noexcept;
    static bool FloatEqual(float a, float b) noexcept;

    /**
     * @brief Safe way to get err string for os errno
     * @param errNum       [in] errno from os
     * @return string
     */
    static char *Error2Str(int errNum);

    /**
     * @brief trim a string
     *
     * @param src          [in] the string to be trimmed
     * @return string after trim
     */
    static std::string StrTrim(const std::string &src) noexcept;

    /**
     * @brief Split a string to string set by seperator
     *
     * @param src          [in] source string
     * @param seperator    [in] seperator
     * @return split set
     */
    static std::set<std::string> StrSplit(const std::string &src, const std::string &seperator) noexcept;

    /*
     * @brief lower case of string
     */
    static std::string StrLowerCase(const std::string &src) noexcept;
    static void StrLowerCaseDirect(std::string &src) noexcept;

    /**
     * @brief if src is 'true' in lower case, return true, other return false,
     * in it, we do lower case and trim, then compare with string 'true'
     */
    static bool BoolFromStr(const std::string &src) noexcept;

    /**
     * @brief Generate random uint32
     *
     * @return a random uint32 if successful, 0 if failed
     */
    uint32_t SecureRandUInt32() noexcept;
};

ALWAYS_INLINE bool Func::FloatLargerThan(float a, float b) noexcept
{
    return (a - b) > EPS_F;
}

ALWAYS_INLINE bool Func::FloatLessThan(float a, float b) noexcept
{
    return (b - a) > EPS_F;
}

ALWAYS_INLINE bool Func::FloatEqual(float a, float b) noexcept
{
    return (a - b) < EPS_F;
}

ALWAYS_INLINE char *Func::Error2Str(int errNum)
{
    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
#if defined(_XOPEN_SOURCE) && defined(_POSIX_C_SOURCE) && defined(_GNU_SOURCE) && \
    (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
    strerror_r(errNum, buf, sizeof(buf) - 1);
    return buf;
#else
    return strerror_r(errNum, buf, sizeof(buf) - 1);
#endif
}

ALWAYS_INLINE std::string Func::StrTrim(const std::string &src) noexcept
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

ALWAYS_INLINE std::set<std::string> Func::StrSplit(const std::string &src, const std::string &seperator) noexcept
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

ALWAYS_INLINE std::string Func::StrLowerCase(const std::string &src) noexcept
{
    std::string res = src;
    for (char &ch : res) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return res;
}

ALWAYS_INLINE void Func::StrLowerCaseDirect(std::string &src) noexcept
{
    for (char &ch : src) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
}

ALWAYS_INLINE bool Func::BoolFromStr(const std::string &src) noexcept
{
    return StrLowerCase(StrTrim(src)) == "true";
}

ALWAYS_INLINE uint32_t Func::SecureRandUInt32() noexcept
{
    uint32_t rand = 0;
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if (!urandom.is_open()) {
        return 0;
    }

    urandom.read(reinterpret_cast<char *>(&rand), sizeof(uint32_t));
    if (!urandom) {
        urandom.close();
        return 0;
    }

    urandom.close();
    return rand;
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_FUNCTIONS_H
