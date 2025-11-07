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
#include "test_net_execution_service.h"

#include "net_execution_service.h"

using namespace ock::hcom;

TestNetExecutionService::TestNetExecutionService() = default;

void TestNetExecutionService::SetUp() {}

void TestNetExecutionService::TearDown() {}

class Task : public NetRunnable {
public:
    void Run() override
    {
        std::cout << "task is executed" << std::endl;
    }
};

TEST_F(TestNetExecutionService, ExecutionService)
{
    NetExecutorServicePtr es = NetExecutorService::Create(1, 128);
    EXPECT_EQ(es.Get() != nullptr, true);

    es->SetThreadName("tt");

    EXPECT_EQ(es->Start(), true);

    auto t = new (std::nothrow) Task();

    EXPECT_EQ(es->Execute(t), true);

    sleep(1);

    es->Stop();
}