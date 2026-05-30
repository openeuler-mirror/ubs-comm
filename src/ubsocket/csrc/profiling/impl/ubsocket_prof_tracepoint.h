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
#include "securec.h"

namespace ock {
namespace ubs {
namespace profiling {
/* make sure the size of Tracepoint is 64 bytes */
struct Tracepoint {
    uint32_t id = 0;
    uint32_t has_name = 0;
    struct Data {
        uint64_t success_count = 0;
        uint64_t failure_count = 0;
        uint64_t total_time = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
        uint64_t pp90_time = 0;
    } data;
    char *name = nullptr;

    void Record(uint64_t timestamp, bool good);

    void SetName(const char *newName)
    {
        if (name != nullptr) {
            free(name);
            name = nullptr;
        }

        if (newName == nullptr || *newName == '\0') {
            return;
        }

        size_t len = strlen(newName);
        name = (char *)malloc(len + 1);
        if (name != nullptr) {
            (void)strcpy(name, newName);
        }
    }

    const char *GetName() const
    {
        return name;
    }

    ~Tracepoint()
    {
        if (name != nullptr) {
            free(name);
            name = nullptr;
        }
    }

    Tracepoint()
    {
        id = 0;
        has_name = 0;
        name = nullptr;
    }

    // copy constructor
    Tracepoint(const Tracepoint &other)
    {
        id = other.id;
        has_name = other.has_name;
        data = other.data;
        name = nullptr;

        if (other.name != nullptr) {
            size_t len = strlen(other.name);
            name = (char *)malloc(len + 1);
            if (name != nullptr) {
                (void)strcpy(name, other.name);
            }
        }
    }

    // copy operator=
    Tracepoint &operator=(const Tracepoint &other)
    {
        if (this == &other) {
            return *this;
        }

        if (name != nullptr) {
            free(name);
        }

        id = other.id;
        has_name = other.has_name;
        data = other.data;

        name = nullptr;
        if (other.name != nullptr) {
            size_t len = strlen(other.name);
            name = (char *)malloc(len + 1);
            if (name != nullptr) {
                (void)strcpy(name, other.name);
            }
        }

        return *this;
    }

    // move constructor
    Tracepoint(Tracepoint &&other) noexcept
    {
        id = other.id;
        has_name = other.has_name;
        data = other.data;
        name = other.name;
        other.name = nullptr;
    }

    // move operator=
    Tracepoint &operator=(Tracepoint &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        if (name != nullptr) {
            free(name);
        }

        id = other.id;
        has_name = other.has_name;
        data = other.data;
        name = other.name;
        other.name = nullptr;

        return *this;
    }
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
