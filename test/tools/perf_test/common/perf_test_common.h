/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_PERF_TEST_COMMON_H
#define HCOM_PERF_TEST_COMMON_H

#include "common/perf_test_config.h"

namespace hcom {
namespace perftest {
constexpr uint64_t MESSAGE_SIZE_BASE = 2;
constexpr uint64_t UB_MAX_SIZE = 65536;

class PerfTestContext {
public:
    uint64_t tposted[MAX_ITERATIONS] = {0};
    uint64_t cnt = 0;
    uint64_t mIterations = 0;
    uint32_t mSize = 0;
    uint64_t totrcnt = 0;
};

class MrInfo {
public:
    uintptr_t lAddress = 0;
    uint64_t lKey = 0;
    uint32_t size = 0;
};

}
}

#endif
