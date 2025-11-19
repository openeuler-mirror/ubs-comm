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
#include <malloc.h>

#include "ub_common.h"
#include "ub_mr_fixed_buf.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {
class TestUbMemRegion : public testing::Test {
public:
    TestUbMemRegion();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string mName = "TestUbMemRegion";
    UBMemoryRegion *MemRegion = nullptr;
    UBContext *ctx = nullptr;
    UBEId eid{};
    urma_context_t mUrmaContext{};
    urma_target_seg_t mMemSeg{};
    char mem[NN_NO8]{};
};

TestUbMemRegion::TestUbMemRegion() {}

void TestUbMemRegion::SetUp()
{
    ctx = new (std::nothrow) UBContext("ubTest", eid);
    ASSERT_NE(ctx, nullptr);
    ctx->mUrmaContext = &mUrmaContext;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MemRegion = new (std::nothrow) UBMemoryRegion(mName, ctx, reinterpret_cast<uintptr_t>(mem), NN_NO8);
    ASSERT_NE(MemRegion, nullptr);
    MemRegion->mMemSeg = &mMemSeg;
    mMemSeg.seg.ubva.va = 0;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
}

void TestUbMemRegion::TearDown()
{
    ctx->mUrmaContext = nullptr;
    MemRegion->mMemSeg = nullptr;

    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }

    if (MemRegion != nullptr) {
        delete MemRegion;
        MemRegion = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestUbMemRegion, GetProtocol)
{
    EXPECT_EQ(MemRegion->GetProtocol(), UBSHcomNetDriverProtocol::UBC);
}

TEST_F(TestUbMemRegion, GetVa)
{
    uint64_t va;
    uint64_t vaLen;
    uint32_t tokenId;

    EXPECT_NO_FATAL_FAILURE(MemRegion->GetVa(va, vaLen, tokenId));
    EXPECT_EQ(va, 0);
}

TEST_F(TestUbMemRegion, Create)
{
    UBMemoryRegion *tmpBuf = nullptr;
    UBMemoryRegion::Create(mName, ctx, NN_NO8, tmpBuf);
    EXPECT_NE(tmpBuf, nullptr);
    tmpBuf->mUBContext = nullptr;
    if (tmpBuf != nullptr) {
        delete tmpBuf;
        tmpBuf = nullptr;
    }
}

TEST_F(TestUbMemRegion, CreateExtMem)
{
    UBMemoryRegion *tmpBuf = nullptr;
    UBMemoryRegion::Create(mName, ctx, reinterpret_cast<uintptr_t>(mem), NN_NO8, tmpBuf);
    EXPECT_NE(tmpBuf, nullptr);
    tmpBuf->mUBContext = nullptr;
    if (tmpBuf != nullptr) {
        delete tmpBuf;
        tmpBuf = nullptr;
    }
}

TEST_F(TestUbMemRegion, InitializeParamErr)
{
    EXPECT_EQ(MemRegion->Initialize(), UB_OK);
    MemRegion->mMemSeg = nullptr;
    MemRegion->mUBContext = nullptr;
    EXPECT_EQ(MemRegion->Initialize(), UB_PARAM_INVALID);
}

TEST_F(TestUbMemRegion, InitializeExtMem)
{
    urma_target_seg_t tmpMr{};
    urma_target_seg_t *tmpPtr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = true;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpPtr)).then(returnValue(&tmpMr));
    EXPECT_EQ(MemRegion->Initialize(), UB_MR_REG_FAILED);
    EXPECT_EQ(MemRegion->Initialize(), UB_OK);
}

TEST_F(TestUbMemRegion, InitializeFail)
{
    void *tmpPtr = nullptr;
    urma_target_seg_t *tmpMr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = false;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpMr));
    MOCKER(memalign).stubs().will(returnValue(tmpPtr));
    EXPECT_EQ(MemRegion->Initialize(), UB_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestUbMemRegion, InitializeFailTwo)
{
    urma_target_seg_t *tmpMr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = false;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpMr));
    EXPECT_EQ(MemRegion->Initialize(), UB_MR_REG_FAILED);
}

TEST_F(TestUbMemRegion, InitializeForOneSideParamErr)
{
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_OK);
    MemRegion->mMemSeg = nullptr;
    MemRegion->mUBContext = nullptr;
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_PARAM_INVALID);
}

TEST_F(TestUbMemRegion, InitializeForOneSideExtMem)
{
    urma_target_seg_t tmpMr{};
    urma_target_seg_t *tmpPtr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = true;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpPtr)).then(returnValue(&tmpMr));
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_MR_REG_FAILED);
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_OK);
}

TEST_F(TestUbMemRegion, InitializeForOneSideFail)
{
    void *tmpPtr = nullptr;
    urma_target_seg_t *tmpMr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = false;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpMr));
    MOCKER(memalign).stubs().will(returnValue(tmpPtr));
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestUbMemRegion, InitializeForOneSideFailTwo)
{
    urma_target_seg_t *tmpMr = nullptr;

    MemRegion->mMemSeg = nullptr;
    MemRegion->mExternalMemory = false;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(tmpMr));
    EXPECT_EQ(MemRegion->InitializeForOneSide(), UB_MR_REG_FAILED);
}

}
}
#endif