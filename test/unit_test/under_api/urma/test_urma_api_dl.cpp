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
#ifdef UB_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <dlfcn.h>

#if defined(TEST_LLT) && defined(MOCK_URMA)
#include "fake_urma.h"
#endif
#include "hcom_log.h"
#include "urma_api_dl.h"

namespace ock {
namespace hcom {

constexpr uint32_t NN_NO67 = 67;

class TestUrmaApiDl : public testing::Test {
public:
    TestUrmaApiDl();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestUrmaApiDl::TestUrmaApiDl() {}

void TestUrmaApiDl::SetUp() {}

void TestUrmaApiDl::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestUrmaApiDl, TestLoadUrmaApiDlFail)
{
    int apiNum = NN_NO67;
    void *ptr = nullptr;
    void *ptr1 = malloc(NN_NO64);
    UrmaAPI::gLoaded = false;

    for (int i = 0; i < apiNum; i++) {
        MOCKER(dlopen).stubs().will(repeat(ptr1, NN_NO2)).then(returnValue(ptr));
        MOCKER(dlsym).stubs().will(repeat(ptr1, i)).then(returnValue(ptr));
        MOCKER(dlclose).stubs().will(returnValue(0));
        int res = UrmaAPI::LoadUrmaAPI();
        EXPECT_EQ(res, -1);
        GlobalMockObject::verify();
    }
    free(ptr1);
}

TEST_F(TestUrmaApiDl, TestLoadUrmaApiDlSuccess)
{
    void *ptr1 = malloc(NN_NO64);
    UrmaAPI::gLoaded = false;
    MOCKER(dlopen).stubs().will(returnValue(ptr1));
    MOCKER(dlsym).stubs().will(returnValue(ptr1));
    int res = UrmaAPI::LoadUrmaAPI();
    EXPECT_EQ(res, 0);
    free(ptr1);
}

}
}
#endif