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

using namespace ock::ubs;
using namespace ock::ubs::urma;

class UrmaPortSelectorTest : public ::testing::Test {
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
};

TEST_F(UrmaPortSelectorTest, INIT_URMA_CONTEXT_POOL)
{
    UrmaContextPool context_pool;
    /* add bonding dev with index */
    auto result = context_pool.AutoAddEidIndex();
    EXPECT_TRUE(result == UBS_OK);
    /* add non bonding dev with index */
    context_pool.ClearEidIndex();
    result = context_pool.AutoAddEidIndex(false);
    EXPECT_TRUE(result == UBS_OK);
    context_pool.ClearEidIndex();

    /* test init and uninit */
    UBS_VLOG_TRACE("first init");
    result = context_pool.AutoAddEidIndex(false);
    result = context_pool.Init();
    context_pool.UnInit();

    /* test get best context */
    UBS_VLOG_TRACE("second init");
    result = context_pool.AutoAddEidIndex(false);
    result = context_pool.Init();
    UrmaContextPtr best_context;
    result = context_pool.GetBestContext(best_context);
    context_pool.UnInit();

    /* test get best context for bonding */
    UBS_VLOG_TRACE("third init");
    result = context_pool.AutoAddEidIndex();
    result = context_pool.Init();
    result = context_pool.GetBestContext(best_context);
    context_pool.UnInit();
}