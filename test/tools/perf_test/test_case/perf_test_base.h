/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_CASE_H
#define HCOM_PERF_TEST_CASE_H

#include "common/perf_test_common.h"
#include "common/perf_test_config.h"

namespace hcom {
namespace perftest {

class PerfTestBase {
public:
    explicit PerfTestBase(const PerfTestConfig& cfg) : mCfg(cfg) {};
    virtual ~PerfTestBase() {};
    virtual bool Initialize() = 0;
    virtual bool RunTest(PerfTestContext* ctx) = 0;
    virtual void UnInitialize() = 0;

protected:
    PerfTestConfig mCfg;
};

}
}
#endif