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
#ifndef UBS_COMM_UBSOCKET_DEFINES_H
#define UBS_COMM_UBSOCKET_DEFINES_H

#include <cstddef>
#include <cstdint>
#include <cstdint>
#include <cstring>

namespace ock {
namespace ubs {

using Result = int32_t;

#define UBS_API __attribute__((visibility("default")))
#define ALWAYS_INLINE inline __attribute__((always_inline))

#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1) != 0)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0) != 0)
#endif

#define ALWAYS_INLINE inline __attribute__((always_inline))

constexpr uint32_t UMQ_BIND_INFO_SIZE_MAX = 512;
constexpr uint32_t DIVIDED_NUMBER  = 2;
constexpr uint32_t CACHE_LINE_ALIGNMENT = 64;
constexpr uint16_t TX_HANDLE_THRESHOLD = 2;
constexpr uint16_t TX_RETRIEVE_THRESHOLD = 1;
constexpr uint16_t TX_REPORT_THRESHOLD = 1;
constexpr uint16_t TX_REFILL_THRESHOLD = 32;
constexpr uint32_t TX_POST_BATCH_MAX = 64;
constexpr uint32_t TX_SGE_MAX = 1;

constexpr uint64_t SIZE_8K  = 8192;
constexpr uint64_t SIZE_16K = 16384;
constexpr uint64_t SIZE_32K = 32768;
constexpr uint64_t SIZE_64K = 65536;
constexpr uint64_t MASK_DIFF = 1;
constexpr uint64_t IOBUF_DIFF = 32;
constexpr uint16_t REFILL_THRESHOLD = 32;
constexpr int RETRY_NEEDED = 1;
// to improve the efficiency, do one ack event operation per GET_PER_ACK times get event operation(same as brpc)
constexpr uint32_t GET_PER_ACK = 32;
// currently, poll batch use 32 is for the balance of performance and efficiency
constexpr uint32_t POLL_BATCH_MAX = 32;

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
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DEFINES_H
