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
#include "ubsocket_prof_tracepoint_combiner_ext.h"

namespace ock {
namespace ubs {
namespace profiling {
constexpr int COL_WIDTH_MIN_EXT = 20;
constexpr int COL_WIDTH_MAX_EXT = 30;

void TraceCombinerExt::OutputTraceGroupExt(std::ostringstream &oss, const TraceGroupExtPtr &allTraceGroup)
{
    for (size_t i = 0; i < allTraceGroup->points_.size(); i++) {
        OutputTracePointStatsExt(oss, allTraceGroup->points_[i]);
    }
}

int TraceCombinerExt::OutputTraceGroupCliExt(char **out_buf, const TraceGroupExtPtr &allTraceGroup)
{
    std::ostringstream oss;
    for (size_t i = 0; i < allTraceGroup->points_.size(); i++) {
        OutputTracePointCliExt(oss, allTraceGroup->points_[i]);
    }
    std::string outStr = oss.str();
    size_t dataLen = outStr.size();
    if (dataLen == 0 || dataLen > SIZE_MAX) {
        return -1;
    }
    char *buf = (char *)malloc(dataLen + 1);
    if (!buf) {
        return -1;
    }
    std::copy(outStr.begin(), outStr.end(), buf);
    buf[dataLen] = '\0';
    *out_buf = buf;
    return static_cast<int>(dataLen);
}

void TraceCombinerExt::OutputTracePointCliExt(std::ostringstream &oss, const TracepointExt &totalTracePoint)
{
    uint64_t avgTime = totalTracePoint.data.success_count > 0 ?
                           totalTracePoint.data.total_time / totalTracePoint.data.success_count :
                           0;

    uint64_t maxTime = totalTracePoint.data.max_time;
    uint64_t minTime = totalTracePoint.data.min_time;
    // 防御性处理：如果没有成功样本，min_time保持UINT64_MAX，需要转换为0
    if (minTime == UINT64_MAX) {
        minTime = 0;
        maxTime = 0;
    }

    oss << "[" << (totalTracePoint.has_name ? totalTracePoint.GetNameExt() : std::string("--")) << "],"
        << totalTracePoint.data.success_count << "," << totalTracePoint.data.failure_count << ","
        << totalTracePoint.data.total_time << "," << avgTime << "," << maxTime << "," << minTime << ","
        << totalTracePoint.data.pp50_time << "," << totalTracePoint.data.pp90_time << ","
        << totalTracePoint.data.pp95_time << "," << totalTracePoint.data.pp99_time << ","
        << totalTracePoint.data.pp999_time << ";";
}

void TraceCombinerExt::OutputTracePointStatsExt(std::ostringstream &oss, const TracepointExt &totalTracePoint)
{
    uint64_t avgTime = totalTracePoint.data.success_count > 0 ?
                           totalTracePoint.data.total_time / totalTracePoint.data.success_count :
                           0;

    uint64_t maxTime = totalTracePoint.data.max_time;
    uint64_t minTime = totalTracePoint.data.min_time;
    // 防御性处理：如果没有成功样本，min_time保持UINT64_MAX，需要转换为0
    if (minTime == UINT64_MAX) {
        minTime = 0;
        maxTime = 0;
    }

    std::string traceName = "[" + (totalTracePoint.has_name ? totalTracePoint.GetNameExt() : std::string("--")) + "]";
    oss << std::left << std::setw(COL_WIDTH_MAX_EXT) << traceName << std::setw(COL_WIDTH_MIN_EXT)
        << totalTracePoint.data.success_count << std::setw(COL_WIDTH_MIN_EXT) << totalTracePoint.data.failure_count
        << std::setw(COL_WIDTH_MIN_EXT) << totalTracePoint.data.total_time << std::setw(COL_WIDTH_MIN_EXT) << avgTime
        << std::setw(COL_WIDTH_MIN_EXT) << maxTime << std::setw(COL_WIDTH_MIN_EXT) << minTime
        << std::setw(COL_WIDTH_MIN_EXT) << totalTracePoint.data.pp50_time << std::setw(COL_WIDTH_MIN_EXT)
        << totalTracePoint.data.pp90_time << std::setw(COL_WIDTH_MIN_EXT) << totalTracePoint.data.pp95_time
        << std::setw(COL_WIDTH_MIN_EXT) << totalTracePoint.data.pp99_time << std::setw(COL_WIDTH_MIN_EXT)
        << totalTracePoint.data.pp999_time << "\n";
}
} // namespace profiling
} // namespace ubs
} // namespace ock
