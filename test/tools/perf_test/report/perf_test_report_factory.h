/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_REPORT_FACTORY_H
#define HCOM_PERF_TEST_REPORT_FACTORY_H

#include <map>

#include "common/perf_test_logger.h"
#include "report/perf_test_report_base.h"

namespace hcom {
namespace perftest {

using ReportCreateFunc = PerfTestReportBase* (*)(const PerfTestConfig& cfg);

class PerfTestReportFactory {
public:
    ~PerfTestReportFactory() = default;
    static PerfTestReportFactory &GetInstance()
    {
        static PerfTestReportFactory instance;
        return instance;
    }

    PerfTestReportBase* CreatePerfTestReport(const PerfTestConfig& cfg);

    void RegistCreateFunc(PERF_TEST_REPORT_TYPE type, ReportCreateFunc func)
    {
        LOG_DEBUG("RegistCreateFunc for PERF_TEST_REPORT_TYPE(" << static_cast<uint32_t>(type) << ")");
        m_createFuncs.emplace(static_cast<uint32_t>(type), func);
    }

private:
    PerfTestReportFactory() = default;
    std::map<uint32_t, ReportCreateFunc> m_createFuncs;
};


class PerfTestReportRegister {
public:
    PerfTestReportRegister(PERF_TEST_REPORT_TYPE type, ReportCreateFunc func)
    {
        PerfTestReportFactory::GetInstance().RegistCreateFunc(type, func);
    }
};

#define REGIST_PERF_TEST_REPORT_CREATOR(ReportType, ReportClass)                             \
    static PerfTestReportBase* Create##ReportClass(const PerfTestConfig& cfg)                \
    {                                                                                        \
        return new (std::nothrow) ReportClass(cfg);                                          \
    }                                                                                        \
    static PerfTestReportRegister g_register_##ReportClass(ReportType, Create##ReportClass)

}
}
#endif
