/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_DL_HELPER_H
#define UBS_COMM_DL_HELPER_H

#include <mockcpp/mockcpp.hpp>
#include <dlfcn.h>

/**
 * dl_helper.h — dlopen/dlsym测试常量。
 *
 * 提供TEST_LIB_PATH、TEST_SYM_NAME、TEST_DL_HANDLE常量，用于dl_api/dl_libc_api/dl_urma_api测试。
 * 复杂场景(多库/多符号/多handle)可在test case中直接定义局部常量。
 */

namespace ock {
namespace ubs {
namespace test {

constexpr const char *TEST_LIB_PATH = "/fake/lib/test.so";
constexpr const char *TEST_SYM_NAME = "test_symbol";
constexpr void *TEST_DL_HANDLE = reinterpret_cast<void *>(0xDEADBEEF);

} // namespace test
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_DL_HELPER_H