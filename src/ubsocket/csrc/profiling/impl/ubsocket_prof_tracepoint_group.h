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
    explicit TraceGroup(uint16_t tpCount) : max_tp_count_(tpCount) {}
    ~TraceGroup() = default;

    int Init() noexcept;

    int Record(uint32_t tp_id, const char *tp_name, uint64_t timestamp, bool good) noexcept;

    Tracepoint Get(uint32_t index) noexcept;

    DEFINE_REF_OPERATION_FUNC

private:
    DECLARE_REF_COUNT_VARIABLE;
    const uint32_t max_tp_count_;
    std::vector<Tracepoint> points_;

    friend class Tracer;
};
using TraceGroupPtr = Ref<TraceGroup>;

inline int TraceGroup::Init() noexcept
{
    points_.resize(max_tp_count_);
    for (uint32_t i = 0; i < max_tp_count_; i++) {
        points_[i].id = i;
    }
    return UBS_OK;
}

inline int TraceGroup::Record(uint32_t tp_id, const char *tp_name, uint64_t timestamp, bool good) noexcept
{
    if (UNLIKELY(tp_id >= max_tp_count_)) {
        UBS_VLOG_ERR("Tracer point not exist");
        return UBS_ERROR;
    }

    if (UNLIKELY(tp_name == nullptr)) {
        UBS_VLOG_ERR("Tracer point name is null.");
        return UBS_ERROR;
    }

    if (UNLIKELY(points_[tp_id].has_name == 0)) {
        points_[tp_id].SetName(tp_name);
        points_[tp_id].has_name = 1;
    }

    points_[tp_id].Record(timestamp, good);
    return UBS_OK;
}

inline Tracepoint TraceGroup::Get(uint32_t index) noexcept
{
    if (UNLIKELY(index >= max_tp_count_)) {
        return Tracepoint();
    }

    return points_[index];
}

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACE_GROUP_H
