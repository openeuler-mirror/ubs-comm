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
#include "ubsocket_prof_tracer.h"

namespace ock {
namespace ubs {
namespace profiling {
thread_local TraceGroup *Tracer::tls_group = nullptr;

Result Tracer::Init(const TracerOptions &options) noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (inited_) {
        UBS_VLOG_DEBUG("Already initialized");
        return UBS_OK;
    }

    if (options.tracepoint_count > 1000L) {
        UBS_VLOG_ERR("Invalid options, tracepoint count %d is too large", options.tracepoint_count);
        return UBS_INVALID_PARAM;
    }

    /* for 1024 threads */
    trace_groups_.reserve(1024L);

    options_ = options;

    inited_ = true;
    return UBS_OK;
}

void Tracer::UnInit() noexcept {}

Result Tracer::CreateTraceGroup() noexcept
{
    if (tls_group != nullptr) {
        return UBS_OK;
    }

    auto group = MakeRef<TraceGroup>();
    if (group == nullptr) {
        UBS_VLOG_ERR("Create trace group failed, probably out of memory");
        return UBS_ERROR;
    }

    auto result = group->Init(options_.tracepoint_count);
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Init trace group failed, probably out of memory");
        return UBS_ERROR;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (!inited_) {
        UBS_VLOG_ERR("Tracer has not been initialize");
        return UBS_ERROR;
    }

    trace_groups_.push_back(group);

    /* increase ref and assign to tls */
    group->IncreaseRef();
    tls_group = group.Get();

    return UBS_OK;
}
} // namespace profiling
} // namespace ubs
} // namespace ock