#include "ubsocket_prof_tracepoint_combiner.h"

namespace ock {
namespace ubs {
namespace profiling {
constexpr int COL_WIDTH_MIN = 20;
constexpr int COL_WIDTH_MAX = 30;

int TraceCombiner::CombinerTracePoint(Tracepoint &outTracePoint, const Tracepoint &pointB)
{
    if (outTracePoint.id != pointB.id) {
        UBS_VLOG_ERR("Error to combiner Tracepoint, Tracepoint id %d and %d is different.\n", outTracePoint.id,
                     pointB.id);
        return UBS_ERROR;
    }
    if ((outTracePoint.has_name == 0) && (pointB.has_name != 0)) {
        outTracePoint.has_name = pointB.has_name;
        outTracePoint.SetName(pointB.name);
    }

    outTracePoint.data.success_count = outTracePoint.data.success_count + pointB.data.success_count;
    outTracePoint.data.failure_count = outTracePoint.data.failure_count + pointB.data.failure_count;
    outTracePoint.data.total_time = outTracePoint.data.total_time + pointB.data.total_time;
    outTracePoint.data.max_time = std::max(outTracePoint.data.max_time, pointB.data.max_time);
    outTracePoint.data.min_time = std::min(outTracePoint.data.min_time, pointB.data.min_time);
    return UBS_OK;
}

void TraceCombiner::OutputTraceGroup(std::ostringstream &oss, const TraceGroupPtr &allTraceGroup)
{
    for (size_t i = 0; i < allTraceGroup->points_.size(); i++) {
        OutputTracePointStats(oss, allTraceGroup->points_[i]);
    }
}

int TraceCombiner::OutputTraceGroupCli(char **out_buf, const TraceGroupPtr &allTraceGroup)
{
    std::ostringstream oss;
    for (size_t i = 0; i < allTraceGroup->points_.size(); i++) {
        OutputTracePointCli(oss, allTraceGroup->points_[i]);
    }
    std::string out_str = oss.str();
    size_t data_len = out_str.size();
    char *buf = (char *)malloc(data_len);
    if (!buf) {
        return -1;
    }
    memcpy(buf, out_str.c_str(), data_len);
    *out_buf = buf;
    return data_len;
}

void TraceCombiner::OutputTracePointCli(std::ostringstream &oss, const Tracepoint &totalTracePoint)
{
    uint64_t maxTime = totalTracePoint.data.max_time;
    uint64_t minTime = totalTracePoint.data.min_time;
    // 防御性处理：如果没有成功样本，min_time保持UINT64_MAX，需要转换为0
    if (minTime == UINT64_MAX) {
        minTime = 0;
        maxTime = 0;
    }

    oss << ("[" + (totalTracePoint.has_name ? totalTracePoint.GetName() : std::string("--")) + "]") << ","
        << totalTracePoint.data.success_count << "," << totalTracePoint.data.failure_count << ","
        << totalTracePoint.data.total_time << ","
        << (totalTracePoint.data.success_count ? totalTracePoint.data.total_time / totalTracePoint.data.success_count :
                                                 0)
        << "," << std::setw(COL_WIDTH_MIN) << maxTime << "," << std::setw(COL_WIDTH_MIN) << minTime << ","
        << ";";
}

void TraceCombiner::OutputTracePointStats(std::ostringstream &oss, const Tracepoint &totalTracePoint)
{
    uint64_t maxTime = totalTracePoint.data.max_time;
    uint64_t minTime = totalTracePoint.data.min_time;
    // 防御性处理：如果没有成功样本，min_time保持UINT64_MAX，需要转换为0
    if (minTime == UINT64_MAX) {
        minTime = 0;
        maxTime = 0;
    }

    oss << std::left << std::setw(COL_WIDTH_MAX)
        << ("[" + (totalTracePoint.has_name ? totalTracePoint.GetName() : std::string("--")) + "]")
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.success_count << std::setw(COL_WIDTH_MIN)
        << totalTracePoint.data.failure_count << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.total_time
        << std::setw(COL_WIDTH_MIN)
        << (totalTracePoint.data.success_count ? totalTracePoint.data.total_time / totalTracePoint.data.success_count :
                                                 0)
        << std::setw(COL_WIDTH_MIN) << maxTime << std::setw(COL_WIDTH_MIN) << minTime << "\n";
}
} // namespace profiling
} // namespace ubs
} // namespace ock