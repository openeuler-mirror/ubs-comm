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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACE_POINT_H
#define UBS_COMM_UBSOCKET_PROF_TRACE_POINT_H

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace profiling {
/* make sure the size of Tracepoint is 64 bytes */
struct Tracepoint {
    uint64_t id = 0;
    struct Data {
        uint64_t success_count = 0;
        uint64_t failure_count = 0;
        uint64_t total_time = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
        uint64_t pp90_time = 0;
        uint64_t pp99_time = 0;
    } data;
    std::string pointName;

    void Record(uint64_t timestamp, bool good);
};

inline void Tracepoint::Record(uint64_t timestamp, bool good)
{
    data.success_count += good;
    data.failure_count += !good;
    if (LIKELY(good)) {
        data.total_time += timestamp;
        data.max_time = std::max(data.max_time, timestamp);
        data.min_time = std::min(data.min_time, timestamp);
        /* TODO pp90 pp99 */
    }
}
} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACE_POINT_H
