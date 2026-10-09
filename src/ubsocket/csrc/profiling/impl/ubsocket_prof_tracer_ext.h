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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACER_EXT_H
#define UBS_COMM_UBSOCKET_PROF_TRACER_EXT_H

#include <sched.h>
#include "common/ubsocket_common_includes.h"
#include "ubsocket_prof_tracepoint_combiner_ext.h"
#include "ubsocket_prof_tracepoint_dumper_ext.h"
#include "ubsocket_prof_tracepoint_group_ext.h"

namespace ock {
namespace ubs {
namespace profiling {

// 最大 CPU 核心数（支持最多 1024 个 CPU）
constexpr size_t MAX_CPU_AGENTS_EXT = 1024;

struct TracerOptionsExt {
    uint32_t tracepoint_count = 0;  // 跟踪点数量上限
    bool enable_dump = false;       // 是否启用定时dump
    uint16_t dump_interval_min = 1; // dump间隔(分钟)
    std::string dump_path;          // dump文件路径
};

class TracerExt {
public:
    ALWAYS_INLINE static TracerExt &Instance()
    {
        static TracerExt instance;
        return instance;
    }

public:
    Result InitExt(const TracerOptionsExt &options) noexcept;

    void UnInitExt() noexcept;

    int RecordExt(uint32_t tp_id, const char *tp_name, uint64_t timestamp, bool good) noexcept;

    int CombineExt(TraceGroupExtPtr &out) noexcept;

    int CombineExt(std::ostringstream &oss) noexcept;

    int CombineExt(char **out_buf) noexcept;

    void ResetExt() noexcept;

private:
    Result CreateAgentExt(int cpuId) noexcept;

    ALWAYS_INLINE int GetCurrentCpuIdExt() noexcept;

private:
    std::mutex mutex_;                                        // 初始化和创建代理时的锁
    bool inited_ = false;                                     // 是否已初始化
    TracerOptionsExt options_;                                // 配置选项
    std::atomic<TraceGroupExt *> agents_[MAX_CPU_AGENTS_EXT]; // 按 CPU 索引的代理数组
    TraceCombinerExtPtr trace_combiner_;                      // 数据合并器
    DumpThreadExtPtr dump_thread_;                            // 定时转储线程
};

ALWAYS_INLINE int TracerExt::GetCurrentCpuIdExt() noexcept
{
    // 使用 sched_getcpu() 获取当前 CPU 核心编号（Linux 专用）
    // 比 getcpu() 更快，无需参数传递
    return sched_getcpu();
}

ALWAYS_INLINE int TracerExt::RecordExt(uint32_t tp_id, const char *tp_name, uint64_t timestamp, bool good) noexcept
{
    // 获取当前 CPU 核心编号
    int cpuId = GetCurrentCpuIdExt();

    // 检查 CPU ID 是否有效
    if (UNLIKELY(cpuId < 0 || cpuId >= MAX_CPU_AGENTS_EXT)) {
        return UBS_ERROR;
    }

    // 获取该 CPU 的代理（无锁读取）
    TraceGroupExt *agent = agents_[cpuId].load(std::memory_order_acquire);

    // 如果代理不存在，尝试创建
    if (UNLIKELY(agent == nullptr)) {
        auto result = CreateAgentExt(cpuId);
        if (result != UBS_OK) {
            return result;
        }
        agent = agents_[cpuId].load(std::memory_order_acquire);
        if (agent == nullptr) {
            return UBS_ERROR;
        }
    }

    // 记录数据（无锁写入，允许覆盖）
    return agent->RecordExt(tp_id, tp_name, timestamp, good);
}

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACER_EXT_H
