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
#include <gtest/gtest.h>

#include "urma_backend.h"
#include "urma_context_pool.h"
#include "urma_poller_agg.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

class UrmaPollerAggTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        /* init urma only */
        UrmaBackend::Instance().Init(true);
    }

    void TearDown() override
    {
        UrmaBackend::Instance().UnInit();
    }

    int32_t handle_cqe(uint32_t flags, int cr_count, urma_cr_t *cr)
    {
        std::cout << "cqe handler" << std::endl;
    }
};

TEST_F(UrmaPollerAggTest, AGG_POLLER_THREAD)
{
    UrmaContextPoolPtr context_pool = MakeRef<UrmaContextPool>();
    auto result = context_pool->AutoAddEidIndex();
    result = context_pool->Init();
    EXPECT_TRUE(result == UBS_OK);

    UrmaAggPollerOptions options;
    options.run_in_thread = true;
    options.polling_type = EVENT_POLLING;
    options.poller_count_per_context = 2;
    options.jfc_depth = 16000;
    options.cqe_handler = std::bind(&UrmaPollerAggTest::handle_cqe, this, std::placeholders::_1, std::placeholders::_2,
                                    std::placeholders::_3);
    options.context_pool = context_pool;

    UrmaAggPollerSet poller;
    poller.Init(options);
}