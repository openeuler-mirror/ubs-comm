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
#include "test_memory_region.h"
#include "ut_helper.h"
#include "hcom_def.h"
#include "transport/net_memory_region.h"
#include "mockcpp/mockcpp.hpp"

using namespace ock::hcom;
TestMemoryRegion::TestMemoryRegion() {}
void TestMemoryRegion::SetUp()
{
    MOCK_VERSION
}

void TestMemoryRegion::TearDown() {}

TEST_F(TestMemoryRegion, OK)
{
    NResult result;
    NormalMemoryRegion *mr;
    result = NormalMemoryRegion::Create("mr", NN_NO4096, mr);
    EXPECT_EQ(result, NN_OK);
    EXPECT_EQ(mr->Size(), NN_NO4096);
    result = mr->Initialize();
    EXPECT_EQ(result, NN_OK);
    mr->UnInitialize();

    void *extMem = malloc(sizeof(NN_NO4096));
    result = NormalMemoryRegion::Create("mr1", (uintptr_t)extMem, NN_NO4096, mr);
    EXPECT_EQ(result, NN_OK);
    EXPECT_EQ(mr->Size(), NN_NO4096);
    EXPECT_EQ(mr->GetAddress(), (uintptr_t)extMem);

    NormalMemoryRegionFixedBuffer *fixedBuffer;
    result = NormalMemoryRegionFixedBuffer::Create("mr", NN_NO4096, NN_NO8, fixedBuffer);
    EXPECT_EQ(result, NN_OK);
    result = fixedBuffer->Initialize();
    EXPECT_EQ(result, NN_OK);
    EXPECT_EQ(fixedBuffer->Size(), NN_NO4096 * NN_NO8);
    EXPECT_EQ(fixedBuffer->GetFreeBufferCount(), NN_NO8);
    uintptr_t item;
    auto ret = fixedBuffer->GetFreeBuffer(item);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(fixedBuffer->GetFreeBufferCount(), NN_NO8 - 1);
    uintptr_t items[7];
    uintptr_t *itemsPtr = &items[0];
    ret = fixedBuffer->GetFreeBufferN(itemsPtr, 7);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(fixedBuffer->GetFreeBufferCount(), 0);
    ret = fixedBuffer->GetFreeBuffer(item);
    EXPECT_EQ(ret, false);
    fixedBuffer->UnInitialize();
}

TEST_F(TestMemoryRegion, Fail)
{
    NResult result;
    NormalMemoryRegion *mr = nullptr;
    result = NormalMemoryRegion::Create("mr", 0, mr);
    EXPECT_NE(result, NN_OK);

    NormalMemoryRegion *mr1 = (NormalMemoryRegion *)malloc(sizeof(NormalMemoryRegion));
    result = NormalMemoryRegion::Create("mr1", 0, NN_NO4096, mr1);
    EXPECT_NE(result, NN_OK);
    result = NormalMemoryRegion::Create("mr1", (uintptr_t)mr1, 0, mr1);
    EXPECT_NE(result, NN_OK);
}