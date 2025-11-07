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
#include <dlfcn.h>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_log.h"
#include "verbs_api_dl.h"
#include "../../common/net_common.h"
namespace ock {
namespace hcom {

class TestVerbsApiDl : public testing::Test {
public:
    TestVerbsApiDl();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestVerbsApiDl::TestVerbsApiDl() {}

void TestVerbsApiDl::SetUp() {}

void TestVerbsApiDl::TearDown()
{
    GlobalMockObject::verify();
}

int g_apiNum = NN_NO26;

TEST_F(TestVerbsApiDl, TestLoadVerbsApiGLoaded)
{
    VerbsAPI::gLoaded = true;
    int res = VerbsAPI::LoadVerbsAPI();
    EXPECT_EQ(res, 0);
}

TEST_F(TestVerbsApiDl, TestLoadVerbsApiFail)
{
    void *ptr = nullptr;
    void *ptr1 = malloc(NN_NO64);

    for (int i = 0; i < g_apiNum; i++) {
        VerbsAPI::gLoaded = false;
        MOCKER(dlopen).stubs().will(repeat(ptr1, NN_NO2)).then(returnValue(ptr));
        MOCKER(dlsym).stubs().will(repeat(ptr1, i)).then(returnValue(ptr));
        int res = VerbsAPI::LoadVerbsAPI();
        EXPECT_EQ(res, -1);
        GlobalMockObject::verify();
    }
    free(ptr1);
}
}
}