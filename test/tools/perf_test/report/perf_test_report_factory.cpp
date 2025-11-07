/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "report/perf_test_report_factory.h"

namespace hcom {
namespace perftest {

PerfTestReportBase* PerfTestReportFactory::CreatePerfTestReport(const PerfTestConfig& cfg)
{
    PERF_TEST_REPORT_TYPE reportType = PERF_TEST_REPORT_TYPE::LATENCY;
    if (static_cast<uint32_t>(cfg.GetType()) % 2 != 0) {
        reportType = PERF_TEST_REPORT_TYPE::BAND_WIDTH;
    }

    for (auto it : m_createFuncs) {
        if (it.first == static_cast<uint32_t>(reportType)) {
            return it.second(cfg);
        }
    }

    LOG_ERROR("Can't find create function for perf test report(type=" << static_cast<uint32_t>(reportType) << ")!");
    return nullptr;
}

}
}
