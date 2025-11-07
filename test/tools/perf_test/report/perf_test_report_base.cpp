/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <iostream>

#include "perf_test_report_base.h"

namespace hcom {
namespace perftest {


void PerfTestReportBase::PrintReportTail()
{
    std::cout << PERF_TEST_RESULT_LINE << std::endl;
}

}
}