/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef PERF_TEST_REPORT_H
#define PERF_TEST_REPORT_H

#include <cstdint>

#include "common/perf_test_config.h"
#include "common/perf_test_common.h"


namespace hcom {
namespace perftest {

enum class PERF_TEST_REPORT_TYPE {
    LATENCY = 0,
    BAND_WIDTH = 1
};

#define PERF_TEST_RESULT_LINE "---------------------------------------------------------------------------------------"

class PerfTestReportBase {
public:
    explicit PerfTestReportBase(const PerfTestConfig& cfg) : mCfg(cfg) {};
    // 打印结果的头部
    virtual void PrintReportHead() = 0;
    // 打印单条结果项，每个包尺寸调用该接口打印一条
    virtual void PrintReportElement(PerfTestContext *ctx) = 0;
    // 打印结果的尾部
    void PrintReportTail();

protected:
    PerfTestConfig mCfg;
};

}
}

#endif
