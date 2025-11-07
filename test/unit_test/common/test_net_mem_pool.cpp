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
#include "net_mem_pool_fixed.h"

namespace ock {
namespace hcom {

class TestNetMemPool : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    NetMemPoolFixedOptions options {};
    NetMemPoolFixedPtr globalPool = nullptr;
};

void TestNetMemPool::SetUp() {}

void TestNetMemPool::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetMemPool, Fixed)
{
    /* init global pool options */
    options.superBlkSizeMB = NN_NO4;
    options.tcExpandBlkCnt = NN_NO64;
    options.minBlkSize = NN_NO64;

    NetLocalAutoDecreasePtr<NetMemPoolFixed> netPtr(new (std::nothrow) NetMemPoolFixed("test", options));
    globalPool = netPtr.Get();
    EXPECT_NE(globalPool, nullptr);
    int ret = globalPool->Initialize();
    EXPECT_EQ(ret, 0);
    globalPool->mFreeCount = 0;

    thread_local NetTCacheFixed tc(globalPool.Get());
    NN_LOG_INFO("mem pool mFreeCount " << globalPool->mFreeCount);
    char *pointer = tc.Allocate<char>();
    EXPECT_NE(pointer, nullptr);
    NN_LOG_INFO(tc.ToString());
    tc.Free<char>(pointer);
}

TEST_F(TestNetMemPool, KeyedThreadLocalCache)
{
    NetMemPoolFixedOptions options{};
    options.superBlkSizeMB = NN_NO1;
    options.tcExpandBlkCnt = NN_NO8;  // 每个扩容时小块个数
    options.minBlkSize = NN_NO64;     // 每个小块大小

    NetLocalAutoDecreasePtr<NetMemPoolFixed> mempool(new (std::nothrow) NetMemPoolFixed("keyed", options));
    mempool.Get()->Initialize();

    KeyedThreadLocalCache<4> cache;

    // Allocate
    // key 超过最大值，更新失败
    EXPECT_NO_THROW(cache.UpdateIf(11, mempool.Get()));
    EXPECT_EQ(cache.Allocate<int>(11), nullptr);

    // key 更新成功，使用 0 号 cache 分配内存
    EXPECT_NO_THROW(cache.UpdateIf(0, mempool.Get()));
    int *arr[16] = {nullptr};
    for (auto &a : arr) {
        a = cache.Allocate<int>(0);
        EXPECT_NE(a, nullptr);
    }

    // key 超过最大值，alloc 失败
    EXPECT_EQ(cache.Allocate<int>(11), nullptr);

    // key 对应的 thread_local cache 不存在
    EXPECT_EQ(cache.Allocate<int>(1), nullptr);

    // Free
    // key 超过最大值，归还失败
    EXPECT_NO_THROW(cache.Free<int>(11, nullptr));

    // key 对应的 thread_local cache 不存在
    EXPECT_NO_THROW(cache.Free<int>(1, nullptr));

    // 归还成功，由于一次性归还了 16 个小块，达到了回收至上层 mempool 的要求，当前 cache[0] 本地的 freelist 将归还一半，
    // 剩余 8 个小块
    for (auto &a : arr) {
        EXPECT_NO_THROW(cache.Free<int>(0, a));
    }
    EXPECT_EQ(cache.mTCacheFixeds[0]->mCurrentFree, 8);
}

}  // namespace hcom
}  // namespace ock
