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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACE_GROUP_H
#define UBS_COMM_UBSOCKET_PROF_TRACE_GROUP_H

#include "ubsocket_prof_tracepoint.h"

namespace ock {
namespace ubs {
namespace profiling {
class TraceGroup {
public:
    int Init(uint32_t tp_count) noexcept;

    int Record(uint32_t tp_id, uint64_t timestamp, bool good) noexcept;

    DEFINE_REF_OPERATION_FUNC
private:
    std::vector<Tracepoint> points_;
    DECLARE_REF_COUNT_VARIABLE;
};
using TraceGroupPtr = Ref<TraceGroup>;

inline int TraceGroup::Init(uint32_t tp_count) noexcept
{
    points_.reserve(tp_count);
    return UBS_OK;
}

inline int TraceGroup::Record(uint32_t tp_id, uint64_t timestamp, bool good) noexcept
{
    if (UNLIKELY(tp_id >= points_.size())) {
        // TODO log out of boundary
        return UBS_ERROR;
    }

    points_[tp_id].Record(timestamp, good);
    return UBS_OK;
}
} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACE_GROUP_H
