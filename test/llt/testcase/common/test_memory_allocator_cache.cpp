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
#include <thread>

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_utils.h"
#include "test_memory_allocator_cache.h"

#define SIZE (256UL << 12)

using namespace ock::hcom;

TestMemoryAllocatorCache::TestMemoryAllocatorCache() {}

void TestMemoryAllocatorCache::SetUp() {}

void TestMemoryAllocatorCache::TearDown() {}

TEST_F(TestMemoryAllocatorCache, AllocateAndFree)
{
    NResult res = NN_OK;
    auto startAddress = memalign(NN_NO4096, SIZE);
    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(startAddress);
    options.size = SIZE;
    options.minBlockSize = NN_NO4096;
    options.alignedAddress = true;
    res = UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE_WITH_CACHE, options, ptr);
    ASSERT_EQ(res, NN_OK);
    uint64_t address = 0;
    auto expectSize = NN_NO4096;
    res = ptr->Allocate(expectSize, address);
    ASSERT_EQ(res, NN_OK);
    res = ptr->Free(address);
    ASSERT_EQ(res, NN_OK);
}

TEST_F(TestMemoryAllocatorCache, AllocateOverSizeAndFree)
{
    NResult res = NN_OK;
    auto startAddress = memalign(NN_NO4096, SIZE);
    UBSHcomNetMemoryAllocatorPtr ptr;
    UBSHcomNetMemoryAllocatorOptions options;
    options.address = reinterpret_cast<uintptr_t>(startAddress);
    options.size = SIZE;
    options.minBlockSize = NN_NO4096;
    options.alignedAddress = true;
    uint64_t maxBlockSize = options.minBlockSize * options.cacheTierCount;
    res = UBSHcomNetMemoryAllocator::Create(ock::hcom::DYNAMIC_SIZE_WITH_CACHE, options, ptr);
    ASSERT_EQ(res, NN_OK);
    uint64_t address = 0;
    auto expectSize = maxBlockSize + 1;
    res = ptr->Allocate(expectSize, address);
    ASSERT_EQ(res, NN_OK);
    res = ptr->Free(address);
    ASSERT_EQ(res, NN_OK);
}