/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef PERF_TEST_REPORT_BANDWIDTH_H
#define PERF_TEST_REPORT_BANDWIDTH_H

#include "report/perf_test_report_base.h"

namespace hcom {
namespace perftest {
class PerfTestReportBw : public PerfTestReportBase {
public:
    PerfTestReportBw(const PerfTestConfig &cfg) : PerfTestReportBase(cfg){};
    void PrintReportElement(PerfTestContext *ctx) override;
    void PrintReportHead() override;
};
}
}
#endif