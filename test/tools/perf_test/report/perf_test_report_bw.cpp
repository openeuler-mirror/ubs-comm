/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <sstream>
#include <iomanip>
#include "report/perf_test_report_factory.h"
#include "common/perf_test_logger.h"
#include "report/perf_test_report_bw.h"

namespace hcom {
namespace perftest {
constexpr uint32_t MIN_ITERATIONS = 1; // min Iterations num
constexpr double NS_TO_S = 1000000000;
constexpr double TO_M = 1000000;
constexpr double BYTE_TO_MB = 1048576;
constexpr char FILL_CHAR = ' ';
constexpr uint32_t DOUBLE_WIDTH_6 = 6;
constexpr uint32_t OUTPUT_WIDTH_8 = 8;
constexpr uint32_t OUTPUT_WIDTH_13 = 13;
constexpr uint32_t OUTPUT_WIDTH_18 = 18;
constexpr uint32_t OUTPUT_WIDTH_20 = 20;
constexpr uint32_t OUTPUT_PRECISION = 2;

// 与ib_send_bw打印保持一致
#define RESULT_BW_HEADER " #bytes #iterations    BW peak[MB/sec]    BW average[MB/sec]  MsgRate[Mpps]"

void PerfTestReportBw::PrintReportElement(PerfTestContext *ctx)
{
    if (ctx == nullptr) {
        LOG_ERROR("ctx is nullptr!");
        return;
    }

    uint64_t iters = ctx->mIterations;
    if (iters < MIN_ITERATIONS + 1) {
        LOG_ERROR("iteration is less than 1!");
        return;
    }

    double delta = static_cast<double>(ctx->tposted[iters] - ctx->tposted[0]) / NS_TO_S;
    double run_inf_bi_factor = 1;
    uint32_t tSize = ctx->mSize * run_inf_bi_factor;
    double bw_avg = (double)(tSize * iters) / delta / BYTE_TO_MB;        // MB/s
    double msgRate = (double)(run_inf_bi_factor * iters) / delta / TO_M; // Mpps
    std::stringstream sstream;
    // 统一设置左对齐，统一设置填充字符（配合位宽使用，通过修改填充字符方便调整打印格式）
    sstream << std::left << std::setfill(FILL_CHAR);
    sstream << " " << std::setw(OUTPUT_WIDTH_8) << ctx->mSize;
    sstream << " " << std::setw(OUTPUT_WIDTH_13) << ctx->mIterations;
    // 固定保留小数点后两位
    sstream << std::fixed << std::setprecision(OUTPUT_PRECISION);
    sstream << " " << std::setw(OUTPUT_WIDTH_20) << "NA";
    sstream << " " << std::setw(OUTPUT_WIDTH_20) << bw_avg;
    // 固定保留小数点后六位
    sstream << " " << std::setprecision(DOUBLE_WIDTH_6) << std::setw(OUTPUT_WIDTH_18) << msgRate;
    std::cout << sstream.str() << std::endl;
}

void PerfTestReportBw::PrintReportHead()
{
    std::stringstream sstream;
    sstream << PERF_TEST_RESULT_LINE << std::endl;
    sstream << "                    BandWidth Test" << std::endl;
    sstream << " Cpu id      : " << mCfg.GetCpuId() << std::endl;
    if (mCfg.GetIsTestAllSize() == false) {
        sstream << " Datasize       : " << mCfg.GetSize();
        sstream << ",          Iterations     : " << mCfg.GetIterations() << std::endl;
    }
    sstream << PERF_TEST_RESULT_LINE << std::endl;
    sstream << RESULT_BW_HEADER << std::endl;
    std::cout << sstream.str();
}

REGIST_PERF_TEST_REPORT_CREATOR(PERF_TEST_REPORT_TYPE::BAND_WIDTH, PerfTestReportBw);
}
}