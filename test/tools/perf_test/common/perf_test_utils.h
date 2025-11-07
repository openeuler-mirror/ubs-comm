/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef PERF_TEST_UTILS_H
#define PERF_TEST_UTILS_H

#include <string>

namespace hcom {
namespace perftest {

class PerfTestUtils {
public:
    static bool IsStringCaseInsensitiveEqual(const std::string& left, const std::string& right);
};

}
}
#endif