/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef PERF_TEST_REPORT_LATENCY_H
#define PERF_TEST_REPORT_LATENCY_H

#include "report/perf_test_report_base.h"

namespace hcom {
namespace perftest {
class PerfTestReportLat : public PerfTestReportBase {
public:
    PerfTestReportLat(const PerfTestConfig &cfg) : PerfTestReportBase(cfg){};
    void PrintReportElement(PerfTestContext *ctx) override;
    void PrintReportHead() override;
    bool isDuplex(const PERF_TEST_TYPE &type);
};
}
}

#endif
