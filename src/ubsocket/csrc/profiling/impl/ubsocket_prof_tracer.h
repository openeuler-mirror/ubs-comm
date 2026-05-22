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
#ifndef UBS_COMM_UBSOCKET_PROF_TYPES_H
#define UBS_COMM_UBSOCKET_PROF_TYPES_H

#include "common/ubsocket_common_includes.h"
#include "ubsocket_prof_tracepoint_group.h"

namespace ock {
namespace ubs {
namespace profiling {
struct TracerOptions {
    uint32_t tracepoint_count = 0;
};

class Tracer {
public:
    ALWAYS_INLINE static Tracer &Instance()
    {
        static Tracer instance;
        return instance;
    }

public:
    Result Init(const TracerOptions &options) noexcept;

    void UnInit() noexcept;

    int Record(uint32_t tp_id, uint64_t timestamp, bool good) noexcept;

private:
    Result CreateTraceGroup() noexcept;

private:
    static thread_local TraceGroup *tls_group; /* thread local trace group ptr to fast record */
    std::mutex mutex_;                         /* mutex for init and trace group creation */
    bool inited_;                              /* inited or not */
    TracerOptions options_;                    /* options */
    std::vector<TraceGroupPtr> trace_groups_;  /* all trace groups for all thread */
};

ALWAYS_INLINE int Tracer::Record(uint32_t tp_id, uint64_t timestamp, bool good) noexcept
{
    if (UNLIKELY(tls_group == nullptr)) {
        auto result = CreateTraceGroup();
        if (result != UBS_OK) {
            return result;
        }
    }

    return tls_group->Record(tp_id, timestamp, good);
}
} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TYPES_H
