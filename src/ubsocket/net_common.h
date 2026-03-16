/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <atomic>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <ifaddrs.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <strings.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <regex>
#include <arpa/inet.h>

constexpr const uint32_t NET_STR_ERROR_BUF_SIZE = 128;

class NetCommon {
public:
    static char *NN_GetStrError(int errNum, char *buf, size_t bufSize)
    {
#if defined(_XOPEN_SOURCE) && defined(_POSIX_C_SOURCE) && defined(_GNU_SOURCE) && \
    (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
        strerror_r(errNum, buf, bufSize - 1);
        return buf;
#else
        return strerror_r(errNum, buf, bufSize - 1);
#endif
    }
};

#endif
