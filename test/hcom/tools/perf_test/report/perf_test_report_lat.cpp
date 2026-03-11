/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "report/perf_test_report_factory.h"
#include "common/perf_test_logger.h"
#include "report/perf_test_report_lat.h"

namespace hcom {
namespace perftest {
constexpr uint32_t HALF = 2;
constexpr uint32_t NS_TO_US = 1000;
constexpr uint32_t LAT_MEASURE_TAIL = 2; // Remove the two max value
constexpr double PERF_TEST_ITERS_99 = 0.99;
constexpr double PERF_TEST_ITERS_99_9 = 0.999;
constexpr char FILL_CHAR = ' ';

constexpr uint32_t OUTPUT_WIDTH_2 = 2;
constexpr uint32_t OUTPUT_WIDTH_7 = 7;
constexpr uint32_t OUTPUT_WIDTH_12 = 12;
constexpr uint32_t OUTPUT_WIDTH_13 = 13;
constexpr uint32_t OUTPUT_WIDTH_14 = 14;
constexpr uint32_t OUTPUT_WIDTH_15 = 15;
constexpr uint32_t OUTPUT_WIDTH_18 = 18;
constexpr uint32_t OUTPUT_WIDTH_22 = 22;
constexpr uint32_t OUTPUT_PRECISION = 2;

// 与ib_send_lat打印保持一致
#define RESULT_LAT_HEADER                                                                                \
    " #bytes #iterations    t_min[usec]    t_max[usec]  t_typical[usec]    t_avg[usec]    t_stdev[usec]" \
        "   99\% percentile[usec]   99.9\% percentile[usec]"

static inline double GetMedian(uint64_t num, double *deltaArr)
{
    if ((num - 1) % HALF != 0) {
        return (deltaArr[num / HALF] + deltaArr[num / HALF - 1]) / HALF;
    } else {
        return deltaArr[num / HALF];
    }
}

bool PerfTestReportLat::isDuplex(const PERF_TEST_TYPE &type)
{
    if (type == PERF_TEST_TYPE::TRANSPORT_SEND_LAT || type == PERF_TEST_TYPE::TRANSPORT_WRITE_LAT ||
        type == PERF_TEST_TYPE::SERVICE_WRITE_LAT || type == PERF_TEST_TYPE::SERVICE_SEND_LAT ||
        type == PERF_TEST_TYPE::SERVICE_RNDV_LAT) {
        return true;
    }
    return false;
}

void PerfTestReportLat::PrintReportElement(PerfTestContext *ctx)
{
    if (ctx == nullptr) {
        LOG_ERROR("ctx is nullptr!");
        return;
    }

    uint64_t iters = ctx->mIterations;
    if (iters < LAT_MEASURE_TAIL + 1) {
        LOG_ERROR("iteration is less than 3!");
        return;
    }

    double *delta = new double[iters];
    if (delta == nullptr) {
        LOG_ERROR("Failed to allocate memory for delta!");
        return;
    }

    double biSend = 1;
    PERF_TEST_TYPE type = mCfg.GetType();
    if (isDuplex(type)) {
        biSend = 2;
    }

    for (uint64_t i = 0; i < iters; i++) {
        // 纳秒(ns)转微秒(us), 单向时延需要RTT/2
        delta[i] = static_cast<double>(ctx->tposted[i + 1] - ctx->tposted[i]) / NS_TO_US / biSend;
    }

    std::sort(delta, delta + iters);
    iters = iters - LAT_MEASURE_TAIL; // Remove the two largest values

    double median = GetMedian(iters, delta);
    double average = 0.0;
    for (uint64_t i = 0; i < iters; i++) {
        average += delta[i];
    }
    average /= iters;

    /* variance lat */
    double stdev_sum = 0;
    for (uint64_t i = 0; i < iters; i++) {
        stdev_sum += pow(average - delta[i], 2);
    }
    double stdev = sqrt(stdev_sum / iters);

    uint64_t iters_99 = static_cast<uint64_t>(ceil(iters * PERF_TEST_ITERS_99));
    uint64_t iters_99_9 = static_cast<uint64_t>(ceil(iters * PERF_TEST_ITERS_99_9));

    std::stringstream sstream;
    // 统一设置左对齐，统一设置填充字符（配合位宽使用，通过修改填充字符方便调整打印格式）
    sstream << std::left << std::setfill(FILL_CHAR);
    sstream << " " << std::setw(OUTPUT_WIDTH_7) << ctx->mSize;
    sstream << " " << std::setw(OUTPUT_WIDTH_13) << ctx->mIterations;
    // 固定保留小数点后两位
    sstream << std::fixed << std::setprecision(OUTPUT_PRECISION);
    sstream << " " << std::setw(OUTPUT_WIDTH_14) << delta[0];
    sstream << " " << std::setw(OUTPUT_WIDTH_12) << delta[iters];
    sstream << " " << std::setw(OUTPUT_WIDTH_18) << median;
    sstream << " " << std::setw(OUTPUT_WIDTH_14) << average;
    sstream << " " << std::setw(OUTPUT_WIDTH_15) << stdev;
    sstream << " " << std::setw(OUTPUT_WIDTH_22) << delta[iters_99];
    sstream << " " << std::setw(OUTPUT_WIDTH_22) << delta[iters_99_9];
    std::cout << sstream.str() << std::endl;

    delete[] delta;
    delta = nullptr;
}

void PerfTestReportLat::PrintReportHead()
{
    std::stringstream sstream;
    sstream << PERF_TEST_RESULT_LINE << std::endl;
    sstream << "                    Latency Test" << std::endl;
    sstream << " Cpu id      : " << mCfg.GetCpuId() << std::endl;
    if (mCfg.GetIsTestAllSize() == false) {
        sstream << " Datasize       : " << mCfg.GetSize();
        sstream << ",          Iterations     : " << mCfg.GetIterations() << std::endl;
    }
    sstream << PERF_TEST_RESULT_LINE << std::endl;
    sstream << RESULT_LAT_HEADER << std::endl;
    std::cout << sstream.str();
}

REGIST_PERF_TEST_REPORT_CREATOR(PERF_TEST_REPORT_TYPE::LATENCY, PerfTestReportLat);
}
}