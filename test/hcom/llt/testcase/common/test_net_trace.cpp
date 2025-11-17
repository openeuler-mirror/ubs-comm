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
#include "test_net_trace.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_service.h"
#include "net_trace.h"

using namespace ock::hcom;
TestCaseNetTrace::TestCaseNetTrace() {}

void TestCaseNetTrace::SetUp() {}

void TestCaseNetTrace::TearDown() {}

TEST_F(TestCaseNetTrace, TestTraceLevel2)
{
    //    NetService::Instance(RDMA, "trace", false);
    for (uint32_t i = 0; i < 10; i++) {
        TRACE_DELAY_BEGIN(SOCK_WORKER_HANDLE_EPOLL_WRNORM_EVENT);
        TRACE_DELAY_END(SOCK_WORKER_HANDLE_EPOLL_WRNORM_EVENT, 0);
    }

    TRACE_DELAY_BEGIN(SERVICE_RECONNECT_COMFIRM);
    TRACE_DELAY_END(SERVICE_RECONNECT_COMFIRM, 0);

    TRACE_DELAY_BEGIN(SOCK_DRIVER_CREATE_WORKER_RESOURCE);
    TRACE_DELAY_END(SOCK_DRIVER_CREATE_WORKER_RESOURCE, 0);

    std::string dumpStr = NetTrace::TraceDump();

    NN_LOG_INFO(dumpStr);
}