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

#include "net_addr_size_map.h"
#include "hcom_num_def.h"
#include "test_address_size_hash_map.h"
#include "hcom_num_def.h"

using namespace ock::hcom;

void TestAddress2SizeHashmap::SetUp() {}

void TestAddress2SizeHashmap::TearDown() {}

TEST_F(TestAddress2SizeHashmap, PutRemove)
{
    NetAddress2SizeHashMap<NetHeapAllocator> hMap {};
    auto result = hMap.Initialize(NN_NO1024);
    ASSERT_EQ(result, 0);
    result = hMap.Put(1, 1);
    ASSERT_EQ(result, 0);
    uint32_t size = 0;
    result = hMap.Remove(1, size);
    ASSERT_EQ(result, 0);
    ASSERT_EQ(size, 1);
    hMap.UnInitialize();
}

TEST_F(TestAddress2SizeHashmap, DoubleInitialize)
{
    NetAddress2SizeHashMap<NetHeapAllocator> hMap {};
    hMap.Initialize(NN_NO1024);
    auto result = hMap.Initialize(NN_NO1024);
    ASSERT_EQ(result, 0);
    hMap.UnInitialize();
}

TEST_F(TestAddress2SizeHashmap, HashBucketPutAndRemove)
{
    NetHashBucket netHashBucket;
    auto result = netHashBucket.Put(1, 1);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO2, NN_NO2);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO3, NN_NO3);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO4, NN_NO4);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO5, NN_NO5);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO6, NN_NO6);
    ASSERT_EQ(result, 1);
    result = netHashBucket.Put(NN_NO7, NN_NO7);
    ASSERT_EQ(result, 0);
    uint32_t size = 0;
    result = netHashBucket.Remove(1, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, 1);
    result = netHashBucket.Remove(NN_NO2, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, NN_NO2);
    result = netHashBucket.Remove(NN_NO3, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, NN_NO3);
    result = netHashBucket.Remove(NN_NO4, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, NN_NO4);
    result = netHashBucket.Remove(NN_NO5, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, NN_NO5);
    result = netHashBucket.Remove(NN_NO6, size);
    ASSERT_EQ(result, 1);
    ASSERT_EQ(size, NN_NO6);
    result = netHashBucket.Remove(NN_NO7, size);
    ASSERT_EQ(result, 0);
}

TEST_F(TestAddress2SizeHashmap, RemoveAbsentAddress)
{
    NetAddress2SizeHashMap<NetHeapAllocator> hMap {};
    hMap.Initialize(NN_NO1024);
    uint32_t size = 0;
    auto result = hMap.Remove(1, size);
    ASSERT_EQ(result, NN_NO100);
    hMap.UnInitialize();
}