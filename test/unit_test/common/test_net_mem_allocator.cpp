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

#include "net_util.h"
#include "net_addr_size_map.h"
#include "net_mem_allocator_cache.h"

namespace ock {
namespace hcom {

class TestNetMemAllocator : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    UBSHcomNetMemoryAllocatorOptions mOptions {};
    void *mAddress = nullptr;
};

void TestNetMemAllocator::SetUp()
{
    mAddress = memalign(NN_NO4096, 256UL << NN_NO24);
    mOptions.address = reinterpret_cast<uintptr_t>(mAddress);
    mOptions.size = 256UL << NN_NO24;
    mOptions.minBlockSize = NN_NO4096;
    mOptions.alignedAddress = true;
}

void TestNetMemAllocator::TearDown()
{
    GlobalMockObject::verify();
    free(mAddress);
}

TEST_F(TestNetMemAllocator, InitializeSuccess)
{
    NetLocalAutoDecreasePtr<NetMemAllocator> alloc(new (std::nothrow) NetMemAllocator());
    int ret = alloc.Get()->Initialize(mOptions.address, mOptions.size, mOptions.minBlockSize, mOptions.alignedAddress);
    EXPECT_EQ(ret, 0);
    NetLocalAutoDecreasePtr<NetAllocatorCache> cache(new (std::nothrow) NetAllocatorCache(alloc.Get()));
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, 0);
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetMemAllocator, InitializeFail_INVALID_PARAM)
{
    NetLocalAutoDecreasePtr<NetMemAllocator> alloc(new (std::nothrow) NetMemAllocator());
    int ret = alloc.Get()->Initialize(mOptions.address, mOptions.size, mOptions.minBlockSize, mOptions.alignedAddress);
    EXPECT_EQ(ret, 0);
    NetLocalAutoDecreasePtr<NetAllocatorCache> cache(new (std::nothrow) NetAllocatorCache(alloc.Get()));

    mOptions.cacheTierPolicy = TIER_POWER;
    mOptions.cacheTierCount = NN_NO32;
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    mOptions.cacheTierPolicy = TIER_TIMES;
    mOptions.cacheTierCount = NN_NO8;

    cache.Get()->mMajorAllocator = nullptr;
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    mOptions.cacheBlockCountPerTier = NN_NO0;
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    mOptions.cacheTierCount = NN_NO0;
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetMemAllocator, InitializeFail_NEW_OBJECT_FAILED)
{
    NetLocalAutoDecreasePtr<NetMemAllocator> alloc(new (std::nothrow) NetMemAllocator());
    int ret = alloc.Get()->Initialize(mOptions.address, mOptions.size, mOptions.minBlockSize, mOptions.alignedAddress);
    EXPECT_EQ(ret, 0);
    NetLocalAutoDecreasePtr<NetAllocatorCache> cache(new (std::nothrow) NetAllocatorCache(alloc.Get()));

    MOCKER_CPP(&NetAddress2SizeHashMap<NetHeapAllocator>::Initialize).stubs().will(returnValue(1));
    ret = cache.Get()->Initialize(mOptions);
    EXPECT_EQ(ret, NN_NEW_OBJECT_FAILED);
}

}  // namespace hcom
}  // namespace ock