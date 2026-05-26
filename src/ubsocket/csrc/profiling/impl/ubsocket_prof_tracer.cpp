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
#include "ubsocket_prof_tracepoint_dumper.h"

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

    /* create traceCombiner */
    trace_combiner_ = MakeRef<TraceCombiner>();
    if (trace_combiner_ == nullptr) {
        UBS_VLOG_ERR("Create trace combiner failed, probably out of memory");
        return UBS_ERROR;
    }

    dump_thread_ = new DumpThread();
    if (dump_thread_ == nullptr) {
        UBS_VLOG_ERR("Create trace dump thread failed, probably out of memory");
        return UBS_ERROR;
    }
    dump_thread_->IncreaseRef();

    options_ = options;
    inited_ = true;
    dump_thread_->DumpStart(options.dumpPath, options.dumpIntervalMin);
    UBS_VLOG_INFO("Ubsocket tracer init success.");
    return UBS_OK;
}

void Tracer::UnInit() noexcept
{
    dump_thread_->DumpStop();
    dump_thread_->DecreaseRef();
    UBS_VLOG_INFO("Ubsocket tracer uninit success.");
}

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

// combiner data and print
int Tracer::CombinerTraceGroups(std::ostringstream &oss) noexcept
{
    std::vector<TraceGroupPtr> localTraceGroup;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!inited_) {
            UBS_VLOG_ERR("Tracer has not been initialize");
            return UBS_ERROR;
        }
        localTraceGroup = trace_groups_;
    }
    
    for (int i = 0; i < options_.tracepoint_count; i++) {
        Tracepoint totalTracePoint;
        for (int j = 0; j < localTraceGroup.size(); j++) {
            if (j == 0) {
                totalTracePoint = localTraceGroup[j].Get()->points_[i];
                continue;
            }
            if (trace_combiner_->CombinerTracePoint(totalTracePoint, localTraceGroup[j].Get()->points_[i]) != UBS_OK) {
                UBS_VLOG_ERR("Error to trace combiner data. \n");
                return UBS_ERROR;
            }
        }
        trace_combiner_->OutputTracePointStats(oss, totalTracePoint);
    }
    return UBS_OK;
}
} // namespace profiling
} // namespace ubs
} // namespace ock