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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACE_POINT_EXT_H
#define UBS_COMM_UBSOCKET_PROF_TRACE_POINT_EXT_H

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
namespace profiling {

// 蓄水池大小：每个 CPU 每个跟踪点 1024 个样本
constexpr size_t RESERVOIR_SIZE_EXT = 1024;

// 百分位常量定义
constexpr double PERCENTILE_P50_EXT = 50.0;  // 第50百分位（中位数）
constexpr double PERCENTILE_P90_EXT = 90.0;  // 第90百分位
constexpr double PERCENTILE_P95_EXT = 95.0;  // 第95百分位
constexpr double PERCENTILE_P99_EXT = 99.0;  // 第99百分位
constexpr double PERCENTILE_P999_EXT = 99.9; // 第99.9百分位

// 快速随机数生成器（参考 brpc 的实现）
// 使用线性同余生成器，无锁且高性能
ALWAYS_INLINE uint64_t fast_rand_ext() noexcept
{
    // 使用原子计数器确保每个线程获得唯一的种子
    static std::atomic<uint64_t> globalSeed{0};

    // 混合变量地址和全局计数器，确保多线程初始化时种子唯一
    thread_local uint64_t seed = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&seed)) ^
                                 globalSeed.fetch_add(1, std::memory_order_relaxed);

    // 线性同余生成器参数（来自 glibc）
    seed = seed * 1103515245 + 12345;
    return seed;
}

struct TracepointExt {
    uint32_t id = 0;
    uint32_t has_name = 0;
    char *name = nullptr;

    // 基础统计数据（普通变量，允许数据竞争，牺牲精度换取性能）
    struct DataExt {
        uint64_t success_count = 0;
        uint64_t failure_count = 0;
        uint64_t total_time = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;

        // 蓄水池样本（无锁写入，允许覆盖）
        uint64_t reservoir[RESERVOIR_SIZE_EXT] = {0};
        size_t reservoir_count = 0;
        uint64_t total_samples = 0;

        // 计算后的百分位值（缓存）
        uint64_t pp50_time = 0;
        uint64_t pp90_time = 0;
        uint64_t pp95_time = 0;
        uint64_t pp99_time = 0;
        uint64_t pp999_time = 0;
    } data;

    void RecordExt(uint64_t timestamp, bool good) noexcept;

    void ComputePercentilesExt() noexcept;

    void SetNameExt(const char *newName)
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

    const char *GetNameExt() const
    {
        return name;
    }

    ~TracepointExt()
    {
        if (name != nullptr) {
            free(name);
            name = nullptr;
        }
    }

    TracepointExt() : id(0), has_name(0), name(nullptr) {}

    // copy constructor
    TracepointExt(const TracepointExt &other)
    {
        id = other.id;
        has_name = other.has_name;
        name = nullptr;

        // 复制基础统计数据（普通变量直接赋值）
        data.success_count = other.data.success_count;
        data.failure_count = other.data.failure_count;
        data.total_time = other.data.total_time;
        data.min_time = other.data.min_time;
        data.max_time = other.data.max_time;
        data.reservoir_count = other.data.reservoir_count;
        data.total_samples = other.data.total_samples;

        // 复制蓄水池数据
        for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
            data.reservoir[i] = other.data.reservoir[i];
        }

        // 复制百分位数据
        data.pp50_time = other.data.pp50_time;
        data.pp90_time = other.data.pp90_time;
        data.pp95_time = other.data.pp95_time;
        data.pp99_time = other.data.pp99_time;
        data.pp999_time = other.data.pp999_time;

        if (other.name != nullptr) {
            size_t len = strlen(other.name);
            name = (char *)malloc(len + 1);
            if (name != nullptr) {
                (void)strcpy(name, other.name);
            }
        }
    }

    // copy operator=
    TracepointExt &operator=(const TracepointExt &other)
    {
        if (this == &other) {
            return *this;
        }

        if (name != nullptr) {
            free(name);
        }

        id = other.id;
        has_name = other.has_name;

        // 复制基础统计数据（普通变量直接赋值）
        data.success_count = other.data.success_count;
        data.failure_count = other.data.failure_count;
        data.total_time = other.data.total_time;
        data.min_time = other.data.min_time;
        data.max_time = other.data.max_time;
        data.reservoir_count = other.data.reservoir_count;
        data.total_samples = other.data.total_samples;

        // 复制蓄水池数据
        for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
            data.reservoir[i] = other.data.reservoir[i];
        }

        // 复制百分位数据
        data.pp50_time = other.data.pp50_time;
        data.pp90_time = other.data.pp90_time;
        data.pp95_time = other.data.pp95_time;
        data.pp99_time = other.data.pp99_time;
        data.pp999_time = other.data.pp999_time;

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
    TracepointExt(TracepointExt &&other) noexcept
    {
        id = other.id;
        has_name = other.has_name;
        name = other.name;
        other.name = nullptr;

        // 移动数据（普通变量直接赋值）
        data.success_count = other.data.success_count;
        data.failure_count = other.data.failure_count;
        data.total_time = other.data.total_time;
        data.min_time = other.data.min_time;
        data.max_time = other.data.max_time;
        data.reservoir_count = other.data.reservoir_count;
        data.total_samples = other.data.total_samples;

        for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
            data.reservoir[i] = other.data.reservoir[i];
        }

        data.pp50_time = other.data.pp50_time;
        data.pp90_time = other.data.pp90_time;
        data.pp95_time = other.data.pp95_time;
        data.pp99_time = other.data.pp99_time;
        data.pp999_time = other.data.pp999_time;
    }

    // move operator=
    TracepointExt &operator=(TracepointExt &&other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        if (name != nullptr) {
            free(name);
        }

        id = other.id;
        has_name = other.has_name;
        name = other.name;
        other.name = nullptr;

        // 移动数据（普通变量直接赋值）
        data.success_count = other.data.success_count;
        data.failure_count = other.data.failure_count;
        data.total_time = other.data.total_time;
        data.min_time = other.data.min_time;
        data.max_time = other.data.max_time;
        data.reservoir_count = other.data.reservoir_count;
        data.total_samples = other.data.total_samples;

        for (size_t i = 0; i < RESERVOIR_SIZE_EXT; i++) {
            data.reservoir[i] = other.data.reservoir[i];
        }

        data.pp50_time = other.data.pp50_time;
        data.pp90_time = other.data.pp90_time;
        data.pp95_time = other.data.pp95_time;
        data.pp99_time = other.data.pp99_time;
        data.pp999_time = other.data.pp999_time;

        return *this;
    }

    void ResetExt() noexcept;
};

inline void TracepointExt::RecordExt(uint64_t timestamp, bool good) noexcept
{
    // 统计成功/失败次数（普通变量操作，允许数据竞争）
    if (good) {
        data.success_count++;
        data.total_time += timestamp;

        // 更新最大值（普通操作）
        if (timestamp > data.max_time) {
            data.max_time = timestamp;
        }

        // 更新最小值（普通操作）
        if (timestamp < data.min_time) {
            data.min_time = timestamp;
        }

        // 蓄水池采样（无锁，允许覆盖）
        // 算法说明：
        // - 当样本数 <= RESERVOIR_SIZE_EXT 时，直接放入蓄水池
        // - 当样本数 > RESERVOIR_SIZE_EXT 时，以 RESERVOIR_SIZE_EXT/total 的概率随机替换一个元素
        // - 使用 fast_rand_ext() 实现无锁随机选择，允许写覆盖
        uint64_t total = data.total_samples++;
        if (total < RESERVOIR_SIZE_EXT) {
            // 蓄水池未满，直接添加（无锁写入，允许覆盖）
            data.reservoir[total] = timestamp;
            // reservoir_count 用于合并时确定有效样本数量
            data.reservoir_count = total + 1;
        } else {
            // 蓄水池已满，随机替换
            // 使用快速随机数生成器，以 O(RESERVOIR_SIZE_EXT/total) 的概率替换
            uint64_t idx = fast_rand_ext() % (total + 1);
            if (idx < RESERVOIR_SIZE_EXT) {
                data.reservoir[idx] = timestamp;
            }
        }
    } else {
        data.failure_count++;
    }
}

inline void TracepointExt::ComputePercentilesExt() noexcept
{
    // 直接读取普通变量
    size_t count = data.reservoir_count;
    if (count == 0) {
        data.pp50_time = 0;
        data.pp90_time = 0;
        data.pp95_time = 0;
        data.pp99_time = 0;
        data.pp999_time = 0;
        return;
    }

    // 创建临时数组并排序
    std::vector<uint64_t> samples(count);
    // 复制蓄水池数据（无锁读取，可能存在轻微的数据不一致，但可接受）
    for (size_t i = 0; i < count; ++i) {
        samples[i] = data.reservoir[i];
    }
    std::sort(samples.begin(), samples.end());

    // 计算百分位（使用线性插值获取更精确的结果）
    auto get_percentile = [&](double p) -> uint64_t {
        // 使用 0-indexed 计算
        double idx_double = (p / 100.0) * static_cast<double>(count - 1);
        size_t idx = static_cast<size_t>(idx_double);

        if (idx >= count) {
            idx = count - 1;
        }

        // 线性插值
        double frac = idx_double - static_cast<double>(idx);
        if (frac > 0.0 && idx + 1 < count) {
            return static_cast<uint64_t>(static_cast<double>(samples[idx]) * (1.0 - frac) +
                                         static_cast<double>(samples[idx + 1]) * frac);
        }

        return samples[idx];
    };

    data.pp50_time = get_percentile(PERCENTILE_P50_EXT);
    data.pp90_time = get_percentile(PERCENTILE_P90_EXT);
    data.pp95_time = get_percentile(PERCENTILE_P95_EXT);
    data.pp99_time = get_percentile(PERCENTILE_P99_EXT);
    data.pp999_time = get_percentile(PERCENTILE_P999_EXT);
}

inline void TracepointExt::ResetExt() noexcept
{
    // 直接赋值普通变量
    data.success_count = 0;
    data.failure_count = 0;
    data.total_time = 0;
    data.min_time = UINT64_MAX;
    data.max_time = 0;
    data.reservoir_count = 0;
    data.total_samples = 0;
    data.pp50_time = 0;
    data.pp90_time = 0;
    data.pp95_time = 0;
    data.pp99_time = 0;
    data.pp999_time = 0;
}

} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACE_POINT_EXT_H
