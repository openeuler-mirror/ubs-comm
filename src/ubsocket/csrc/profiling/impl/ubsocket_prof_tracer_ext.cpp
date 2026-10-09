/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket_prof_tracer_ext.h"

namespace ock {
namespace ubs {
namespace profiling {

Result TracerExt::InitExt(const TracerOptionsExt &options) noexcept
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

    // 创建 trace_combiner_
    trace_combiner_ = MakeRef<TraceCombinerExt>();
    if (trace_combiner_ == nullptr) {
        UBS_VLOG_ERR("Create trace combiner failed, probably out of memory");
        return UBS_ERROR;
    }

    // 创建 dump_thread_ if enabled
    if (options.enable_dump) {
        dump_thread_ = MakeRef<DumpThreadExt>();
        if (dump_thread_ == nullptr) {
            UBS_VLOG_ERR("Create trace dump thread failed, probably out of memory");
            return UBS_ERROR;
        }
        dump_thread_->DumpStartExt(options.dump_path, options.dump_interval_min);
    }

    options_ = options;
    inited_ = true;
    UBS_VLOG_INFO("Ubsocket tracer ext init success.");
    return UBS_OK;
}

void TracerExt::UnInitExt() noexcept
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!inited_) {
        UBS_VLOG_DEBUG("Tracer ext not initialized");
        return;
    }

    if (dump_thread_ != nullptr) {
        dump_thread_->DumpStopExt();
        dump_thread_ = nullptr;
    }

    // 清理所有代理
    for (size_t i = 0; i < MAX_CPU_AGENTS_EXT; i++) {
        TraceGroupExt *agent = agents_[i].load();
        if (agent != nullptr) {
            agent->DecreaseRef();
            agents_[i].store(nullptr);
        }
    }

    inited_ = false;
    UBS_VLOG_INFO("Ubsocket tracer ext uninit success.");
}

Result TracerExt::CreateAgentExt(int cpuId) noexcept
{
    // Double-checked locking
    TraceGroupExt *agent = agents_[cpuId].load(std::memory_order_acquire);
    if (agent != nullptr) {
        return UBS_OK;
    }

    std::lock_guard<std::mutex> guard(mutex_);

    // 再次检查
    agent = agents_[cpuId].load(std::memory_order_acquire);
    if (agent != nullptr) {
        return UBS_OK;
    }

    if (!inited_) {
        UBS_VLOG_ERR("Tracer ext has not been initialize");
        return UBS_ERROR;
    }

    // 创建并初始化代理
    auto group = MakeRef<TraceGroupExt>(options_.tracepoint_count);
    if (group == nullptr) {
        UBS_VLOG_ERR("Create trace group ext failed, probably out of memory");
        return UBS_ERROR;
    }

    auto result = group->InitExt();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Init trace group ext failed, probably out of memory");
        return UBS_ERROR;
    }

    // 增加引用计数并存储
    group->IncreaseRef();
    agents_[cpuId].store(group.Get(), std::memory_order_release);

    return UBS_OK;
}

int TracerExt::CombineExt(TraceGroupExtPtr &out) noexcept
{
    out = MakeRef<TraceGroupExt>(options_.tracepoint_count);
    if (out == nullptr) {
        return -1;
    }

    if (out->InitExt() != UBS_OK) {
        return -1;
    }

    // 收集所有非空代理
    std::vector<TraceGroupExt *> localAgents;
    localAgents.reserve(MAX_CPU_AGENTS_EXT);

    for (size_t i = 0; i < MAX_CPU_AGENTS_EXT; i++) {
        TraceGroupExt *agent = agents_[i].load(std::memory_order_acquire);
        if (agent != nullptr) {
            localAgents.push_back(agent);
        }
    }

    if (localAgents.empty()) {
        return 0;
    }

    // 合并每个跟踪点的数据
    for (uint32_t tpId = 0; tpId < options_.tracepoint_count; tpId++) {
        auto &combinedTp = out->points_[tpId];
        uint64_t totalSuccess = 0;
        uint64_t totalFailure = 0;
        uint64_t totalTime = 0;
        uint64_t maxTime = 0;
        uint64_t minTime = UINT64_MAX;
        uint64_t totalSamples = 0;

        // 收集所有蓄水池样本
        std::vector<uint64_t> allSamples;
        allSamples.reserve(localAgents.size() * RESERVOIR_SIZE_EXT);

        for (auto agent : localAgents) {
            const auto &agentTp = agent->points_[tpId];

            // 累加统计数据（普通变量直接访问）
            totalSuccess += agentTp.data.success_count;
            totalFailure += agentTp.data.failure_count;
            totalTime += agentTp.data.total_time;
            totalSamples += agentTp.data.total_samples;

            // 更新最大最小值
            if (agentTp.data.max_time > maxTime) {
                maxTime = agentTp.data.max_time;
            }

            if (agentTp.data.min_time < minTime) {
                minTime = agentTp.data.min_time;
            }

            // 收集蓄水池样本
            for (size_t i = 0; i < agentTp.data.reservoir_count; ++i) {
                allSamples.push_back(agentTp.data.reservoir[i]);
            }

            // 设置名称
            if (combinedTp.has_name == 0 && agentTp.has_name != 0) {
                combinedTp.SetNameExt(agentTp.GetNameExt());
                combinedTp.has_name = 1;
            }
        }

        // 设置合并后的统计数据（普通变量直接赋值）
        combinedTp.data.success_count = totalSuccess;
        combinedTp.data.failure_count = totalFailure;
        combinedTp.data.total_time = totalTime;
        combinedTp.data.max_time = maxTime;
        combinedTp.data.min_time = minTime;
        combinedTp.data.total_samples = totalSamples;

        // 蓄水池合并策略：随机采样保持固定大小
        size_t sampleCount = allSamples.size();
        if (sampleCount <= RESERVOIR_SIZE_EXT) {
            // 如果总样本数不超过蓄水池大小，直接复制
            for (size_t i = 0; i < sampleCount; ++i) {
                combinedTp.data.reservoir[i] = allSamples[i];
            }
            combinedTp.data.reservoir_count = sampleCount;
        } else {
            // 如果总样本数超过蓄水池大小，随机采样
            // 使用 Fisher-Yates 洗牌算法的变体进行随机选择
            size_t selected = 0;
            for (size_t i = 0; i < sampleCount && selected < RESERVOIR_SIZE_EXT; ++i) {
                // 以 RESERVOIR_SIZE_EXT/(i+1) 的概率选择当前元素
                uint64_t idx = fast_rand_ext() % (i + 1);
                if (idx < RESERVOIR_SIZE_EXT) {
                    if (idx < selected) {
                        // 替换已选中的元素
                        combinedTp.data.reservoir[idx] = allSamples[i];
                    } else {
                        // 添加新元素
                        combinedTp.data.reservoir[selected] = allSamples[i];
                        selected++;
                    }
                }
            }
            combinedTp.data.reservoir_count = RESERVOIR_SIZE_EXT;
        }

        // 计算百分位
        combinedTp.ComputePercentilesExt();
    }

    return 0;
}

int TracerExt::CombineExt(std::ostringstream &oss) noexcept
{
    TraceGroupExtPtr out;
    int ret = CombineExt(out);
    if (ret == UBS_OK) {
        trace_combiner_->OutputTraceGroupExt(oss, out);
    }

    return ret;
}

int TracerExt::CombineExt(char **out_buf) noexcept
{
    TraceGroupExtPtr out;
    int ret = CombineExt(out);
    if (ret == UBS_OK) {
        ret = trace_combiner_->OutputTraceGroupCliExt(out_buf, out);
    }

    return ret;
}

void TracerExt::ResetExt() noexcept
{
    for (size_t i = 0; i < MAX_CPU_AGENTS_EXT; i++) {
        TraceGroupExt *agent = agents_[i].load(std::memory_order_acquire);
        if (agent != nullptr) {
            agent->ResetExt();
        }
    }
}

} // namespace profiling
} // namespace ubs
} // namespace ock
