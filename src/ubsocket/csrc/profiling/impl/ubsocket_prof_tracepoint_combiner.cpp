#include "ubsocket_prof_tracepoint_combiner.h"

namespace ock {
namespace ubs {
namespace profiling {
constexpr int COL_WIDTH_MIN = 20;
constexpr int COL_WIDTH_MAX = 30;

int TraceCombiner::CombinerTracePoint(Tracepoint& outTracePoint, const Tracepoint& pointB)
{
    if (outTracePoint.id != pointB.id) {
        UBS_VLOG_ERR("Error to combiner Tracepoint, Tracepoint id %d and %d is different.\n",
            outTracePoint.id, pointB.id);
        return UBS_ERROR;
    }
    outTracePoint.data.success_count = outTracePoint.data.success_count + pointB.data.success_count;
    outTracePoint.data.failure_count = outTracePoint.data.failure_count + pointB.data.failure_count;
    outTracePoint.data.total_time = outTracePoint.data.total_time + pointB.data.total_time;
    outTracePoint.data.max_time = std::max(outTracePoint.data.max_time, pointB.data.max_time);
    outTracePoint.data.min_time = std::min(outTracePoint.data.min_time, pointB.data.min_time);
    return UBS_OK;
}

void TraceCombiner::OutputTracePointStats(std::ostringstream &oss, const Tracepoint &totalTracePoint)
{
    oss << std::left
        << std::setw(COL_WIDTH_MAX) <<
            ("[" + (totalTracePoint.pointName.empty() ? std::string("--") : totalTracePoint.pointName) + "]")
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.success_count
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.failure_count
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.total_time
        << std::setw(COL_WIDTH_MIN) <<
            (totalTracePoint.data.success_count ? totalTracePoint.data.total_time / totalTracePoint.data.success_count : 0)
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.max_time
        << std::setw(COL_WIDTH_MIN) << totalTracePoint.data.min_time
        << "\n";
}
}
}
}