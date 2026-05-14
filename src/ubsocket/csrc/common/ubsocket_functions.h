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
    char buf[128L] = {0};
#if defined(_XOPEN_SOURCE) && defined(_POSIX_C_SOURCE) && defined(_GNU_SOURCE) && \
    (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
    strerror_r(errNum, buf, sizeof(buf) - 1);
    return buf;
#else
    return strerror_r(errNum, buf, sizeof(buf) - 1);
#endif
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_FUNCTIONS_H
