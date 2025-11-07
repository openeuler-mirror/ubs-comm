/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <cstdint>
#include <cstring>
#include <iostream>

#include "hcom_securec.h"

namespace ock {
namespace hcom {
#ifndef SECUREC_MEM_MAX_LEN
#define SECUREC_MEM_MAX_LEN 0x7fffffffUL
#endif

#ifndef SECUREC_STRING_MAX_LEN
#define SECUREC_STRING_MAX_LEN 0x7fffffffUL
#endif

#define SECUREC_LIKELY(x) __builtin_expect(!!(x), 1)

#define SECUREC_MEMORY_NO_OVERLAP(dest, src, count)                           \
    (((src) < (dest) && ((const char *)(src) + (count)) <= (char *)(dest)) || \
        ((dest) < (src) && ((char *)(dest) + (count)) <= (const char *)(src)))

#define SECUREC_MEMORY_IS_OVERLAP(dest, src, count)                          \
    (((src) < (dest) && ((const char *)(src) + (count)) > (char *)(dest)) || \
        ((dest) < (src) && ((char *)(dest) + (count)) > (const char *)(src)))

#define SECUREC_MEMCPY_PARAM_OK(dest, destMax, src, count)                           \
    (SECUREC_LIKELY((count) <= (destMax) && (dest) != nullptr && (src) != nullptr && \
        (destMax) <= SECUREC_MEM_MAX_LEN && (count) > 0 && SECUREC_MEMORY_NO_OVERLAP((dest), (src), (count))))

#define SECUREC_STRCPY_PARAM_OK(strDest, destMax, strSrc)                                                   \
    ((destMax) > 0 && (destMax) <= SECUREC_STRING_MAX_LEN && (strDest) != nullptr && (strSrc) != nullptr && \
        (strDest) != (strSrc))

#define SECUREC_CALC_STR_LEN(str, maxLen, outLen) \
    do {                                          \
        *(outLen) = strnlen((str), (maxLen));     \
    } while (0)

#define SECUREC_STRCPY_OPT(dest, src, lenWithTerm) \
    do {                                           \
        memcpy((dest), (src), (lenWithTerm));      \
    } while (0)

typedef enum {
    SEC_EOK = 0,
    SEC_EINVAL = 22,
    SEC_ERANGE = 34,
    SEC_EINVAL_AND_RESET = 150,
    SEC_ERANGE_AND_RESET = 162,
    SEC_EOVERLAP_AND_RESET = 182,
} MEMCPY_S_CODE;

inline int SecMemcpyError(void *dest, size_t destMax, const void *src, size_t count)
{
    if (destMax == 0 || destMax > SECUREC_MEM_MAX_LEN) {
        return SEC_ERANGE;
    }
    if (dest == nullptr || src == nullptr) {
        if (dest != nullptr) {
            (void)memset(dest, 0, destMax);
            return SEC_EINVAL_AND_RESET;
        }
        return SEC_EINVAL;
    }
    if (count > destMax) {
        (void)memset(dest, 0, destMax);
        return SEC_ERANGE_AND_RESET;
    }
    if (SECUREC_MEMORY_IS_OVERLAP(dest, src, count)) {
        (void)memset(dest, 0, destMax);
        return SEC_EOVERLAP_AND_RESET;
    }
    /* Count is 0 or dest equal src also ret EOK */
    return SEC_EOK;
}

int memcpy_s(void *dest, size_t destMax, const void *src, size_t count)
{
    if (SECUREC_MEMCPY_PARAM_OK(dest, destMax, src, count)) {
        memcpy(dest, src, count);
        return SEC_EOK;
    }
    /* Meet some runtime violation, return error code */
    return SecMemcpyError(dest, destMax, src, count);
}

inline int CheckSrcRange(char *strDest, size_t destMax, const char *strSrc)
{
    size_t tmpDestMax = destMax;
    const char *tmpSrc = strSrc;
    /* Use destMax as boundary checker and destMax must be greater than zero */
    while (*tmpSrc != '\0' && tmpDestMax > 0) {
        ++tmpSrc;
        --tmpDestMax;
    }
    if (tmpDestMax == 0) {
        strDest[0] = '\0';
        return SEC_ERANGE_AND_RESET;
    }
    return SEC_EOK;
}

int strcpy_error(char *strDest, size_t destMax, const char *strSrc)
{
    if (destMax == 0 || destMax > SECUREC_STRING_MAX_LEN) {
        return SEC_ERANGE;
    }
    if (strDest == nullptr || strSrc == nullptr) {
        if (strDest != nullptr) {
            strDest[0] = '\0';
            return SEC_EINVAL_AND_RESET;
        }
        return SEC_EINVAL;
    }
    return CheckSrcRange(strDest, destMax, strSrc);
}

int strcpy_s(char *strDest, size_t destMax, const char *strSrc)
{
    if (SECUREC_STRCPY_PARAM_OK(strDest, destMax, strSrc)) {
        size_t srcStrLen;
        SECUREC_CALC_STR_LEN(strSrc, destMax, &srcStrLen);
        ++srcStrLen; /* The length include '\0' */

        if (srcStrLen <= destMax) {
            /* Use mem overlap check include '\0' */
            if (SECUREC_MEMORY_NO_OVERLAP(strDest, strSrc, srcStrLen)) {
                /* Performance optimization srcStrLen include '\0' */
                SECUREC_STRCPY_OPT(strDest, strSrc, srcStrLen);
                return SEC_EOK;
            } else {
                strDest[0] = '\0';
                return SEC_EOVERLAP_AND_RESET;
            }
        }
    }
    return strcpy_error(strDest, destMax, strSrc);
}
}
}