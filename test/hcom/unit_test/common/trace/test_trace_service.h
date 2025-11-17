/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */


#ifndef DFS_TRACER_TEST_TRACE_SERVICE_H
#define DFS_TRACER_TEST_TRACE_SERVICE_H

#include <thread>
#include "gtest/gtest.h"
#include "htracer_service.h"
#include "htracer_service_helper.h"
#include "rpc_server.h"

namespace ock {
namespace hcom {
class TestService : public testing::Test {
public:
    TestService() {}
    ~TestService() {}

    // TestCase only enter once
    static void SetUpTestCase();
    static void TearDownTestCase();

    // every TEST_F macro will enter one
    void SetUp() const;
    void TearDown() const;

    ock::hcom::HTracerService *g_traceService = nullptr;
};
}
}

#endif // DFS_TRACER_TEST_TRACE_SERVICE_H
