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

#include "ub_jetty_ptr_map.h"

namespace ock {
namespace hcom {
class TestUbJettyPtrMap : public ::testing::Test {
public:
    void SetUp() override
    {
        mJettyPtrMap = new (std::nothrow) JettyPtrMap;
        ASSERT_EQ(mJettyPtrMap, nullptr);
    }

    void TearDown() override
    {
        delete mJettyPtrMap;
        mJettyPtrMap = nullptr;

        GlobalMockObject::verify();
    }

private:
    JettyPtrMap *mJettyPtrMap;
};

TEST_F(TestUbJettyPtrMap, JetyyPtrMapInitFailed)
{
    MOCKER_CPP(&mmap).stubs().will(returnValue(MAP_FAILED));
    EXPECT_EQ(mJettyPtrMap->Initialize(), UB_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestUbJettyPtrMap, JetyyPtrMapLookup)
{
    mJettyPtrMap->Initialize();

    UBJetty *jetty = mJettyPtrMap->Lookup(-1);
    EXPECT_EQ(jetty, nullptr);

    mJettyPtrMap->Emplace(0, (UBJetty *)1);
    jetty = mJettyPtrMap->Lookup(0);
    EXPECT_EQ(jetty, (UBJetty *)1);
}

TEST_F(TestUbJettyPtrMap, JetyyPtrMapEmplace)
{
    mJettyPtrMap->Initialize();

    UResult result;

    result = mJettyPtrMap->Emplace(-1, nullptr);
    EXPECT_EQ(result, UB_ERROR);

    result = mJettyPtrMap->Emplace(0, (UBJetty *)0x1122);
    EXPECT_EQ(result, UB_OK);

    UBJetty *jetty = mJettyPtrMap->Lookup(0);
    EXPECT_EQ(jetty, (UBJetty *)0x1122);
}

TEST_F(TestUbJettyPtrMap, JetyyPtrMapModify)
{
    mJettyPtrMap->Initialize();

    UResult result;

    result = mJettyPtrMap->Modify(-1, nullptr);
    EXPECT_EQ(result, UB_ERROR);

    result = mJettyPtrMap->Modify(0, (UBJetty *)0x1122);
    EXPECT_EQ(result, UB_OK);

    UBJetty *jetty = mJettyPtrMap->Lookup(0);
    EXPECT_EQ(result, UB_OK);
    EXPECT_EQ(jetty, (UBJetty *)0x1122);
}
}  // namespace hcom
}  // namespace ock
#endif