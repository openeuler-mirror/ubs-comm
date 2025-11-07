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
#include "test_load_balance.h"
#include "ut_helper.h"
#include "hcom_def.h"
#include "mockcpp/mockcpp.hpp"

using namespace ock::hcom;
TestLoadBalance::TestLoadBalance() {}
uint16_t TestLoadBalance::basePort = 8899;
void TestLoadBalance::SetUp()
{
    MOCK_VERSION
}

void TestLoadBalance::TearDown() {}

TEST_F(TestLoadBalance, OK)
{
    sem_t sem;
    bool result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr, *client = nullptr;
    std::unordered_map<uintptr_t, sem_t *> semMap;
    Handlers handlers {};
    handlers.receivedHandler = [&](const UBSHcomNetRequestContext &ctx) -> int {
        sem_post(&sem);
        return 0;
    };
    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 8192;
    options.lbPolicy = ock::hcom::NET_HASH_IP_PORT;
    strcpy(options.workerGroups, "1,3,3");
    strcpy(options.workerGroupsCpuSet, "10-10,11-13,na");
    options.SetNetDeviceIpMask(IP_SEG);
    result = UTHelper::ServerCreateDriver(server, handlers, options, ++basePort);
    ASSERT_EQ(result, true);
    result = UTHelper::ClientCreateDriver(client, handlers, options, basePort);
    ASSERT_EQ(result, true);
    result = UTHelper::ClientConnect(client, ep, 0);
    ASSERT_EQ(result, true);
    result = UTHelper::ClientSend(ep, &sem);
    ASSERT_EQ(result, true);
}

TEST_F(TestLoadBalance, WrongGroups)
{
    sem_t sem;
    bool result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr, *client = nullptr;
    std::unordered_map<uintptr_t, sem_t *> semMap;
    Handlers handlers {};
    handlers.receivedHandler = [&](const UBSHcomNetRequestContext &ctx) -> int {
        sem_post(&sem);
        return 0;
    };
    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 8192;
    options.lbPolicy = ock::hcom::NET_HASH_IP_PORT;
    strcpy(options.workerGroups, "1,3");
    strcpy(options.workerGroupsCpuSet, "10-11,12-20,12-20,12-20,1-20");
    options.SetNetDeviceIpMask(IP_SEG);
    result = UTHelper::ServerCreateDriver(server, handlers, options, ++basePort);
    EXPECT_EQ(result, false);
    result = UTHelper::ClientCreateDriver(client, handlers, options, basePort);
    EXPECT_EQ(result, false);
}

TEST_F(TestLoadBalance, WrongPolicy)
{
    sem_t sem;
    bool result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriver *server = nullptr, *client = nullptr;
    std::unordered_map<uintptr_t, sem_t *> semMap;
    Handlers handlers {};
    handlers.receivedHandler = [&](const UBSHcomNetRequestContext &ctx) -> int {
        sem_post(&sem);
        return 0;
    };
    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegSize = 1024;
    options.mrSendReceiveSegCount = 8192;
    options.lbPolicy = (UBSHcomNetDriverLBPolicy)3;
    options.SetNetDeviceIpMask(IP_SEG);
    // wrong policy check will fail and result is false
    result = UTHelper::ClientCreateDriver(client, handlers, options, ++basePort);
    ASSERT_EQ(result, false);
    result = UTHelper::ServerCreateDriver(server, handlers, options, basePort);
    ASSERT_EQ(result, false);
    result = UTHelper::ClientConnect(client, ep, 0);
    EXPECT_EQ(result, false);
}