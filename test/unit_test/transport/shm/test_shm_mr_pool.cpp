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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "shm_mr_pool.h"

namespace ock {
namespace hcom {

class TestShmMemoryRegion : public testing::Test {
public:
    TestShmMemoryRegion();
    virtual void SetUp(void);
    virtual void TearDown(void);

    ShmMemoryRegion *mr = nullptr;
    ShmMemoryRegion *noExternalMr = nullptr;
};

TestShmMemoryRegion::TestShmMemoryRegion()
{}

void TestShmMemoryRegion::SetUp()
{
    mr = new (std::nothrow) ShmMemoryRegion("shmMr", true, 0, 0);
    ASSERT_NE(mr, nullptr);
    noExternalMr = new (std::nothrow) ShmMemoryRegion("noExternalshmMr", false, 0, 0);
    ASSERT_NE(mr, nullptr);
}

void TestShmMemoryRegion::TearDown()
{
    if (mr != nullptr) {
        delete mr;
        mr = nullptr;
    }
    if (noExternalMr != nullptr) {
        delete noExternalMr;
        noExternalMr = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmMemoryRegion, UnInitializeFail)
{
    mr->UnInitialize();
}

TEST_F(TestShmMemoryRegion, CreateAndInitialize)
{
    ShmMemoryRegion *tmpShmMr = nullptr;
    int ret = ShmMemoryRegion::Create("tmpShmMr", 1, 1, tmpShmMr);
    EXPECT_EQ(ret, NN_OK);

    ret = tmpShmMr->Initialize();
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestShmMemoryRegion, InitializeInvalidParam)
{
    int ret = mr->Initialize();
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestShmMemoryRegion, InitializeHandlerNoInitializedFail)
{
    MOCKER_CPP(ShmHandle::Initialize).stubs().will(returnValue(300));
    int ret = noExternalMr->Initialize();
    EXPECT_EQ(ret, NN_NOT_INITIALIZED);
}

TEST_F(TestShmMemoryRegion, InitializeHandlerMallocFail)
{
    MOCKER_CPP(ShmHandle::Initialize).stubs().will(returnValue(0));
    uintptr_t mAddress = 0;
    MOCKER_CPP(ShmHandle::ShmAddress).stubs().will(returnValue(mAddress));
    int ret = noExternalMr->Initialize();
    EXPECT_EQ(ret, NN_MALLOC_FAILED);
}

}  // namespace hcom
}  // namespace ock