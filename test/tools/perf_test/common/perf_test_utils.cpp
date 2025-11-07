/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "common/perf_test_utils.h"

namespace hcom {
namespace perftest {


bool PerfTestUtils::IsStringCaseInsensitiveEqual(const std::string& left, const std::string& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(left[i]) != std::tolower(right[i])) {
            return false;
        }
    }
    return true;
}

}
}