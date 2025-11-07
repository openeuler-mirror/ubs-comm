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
#include "test_ringbuffer.h"

using namespace ock::hcom;

TestCaseRingBuffer::TestCaseRingBuffer() {}

void TestCaseRingBuffer::SetUp() {}

void TestCaseRingBuffer::TearDown() {}

TEST_F(TestCaseRingBuffer, NetRingBuffer_Ser_OK)
{
    NResult result;
    bool ret;
    NetRingBuffer<int> ringBuffer(4);
    EXPECT_EQ(ringBuffer.Capacity(), 4);
    result = ringBuffer.Initialize();
    EXPECT_EQ(result, NN_OK);
    ret = ringBuffer.PushBack(1);
    EXPECT_EQ(ret, true);
    ringBuffer.PushBack(2);
    EXPECT_EQ(ringBuffer.Size(), 2);
    int a = 0;
    ringBuffer.PopFront(a);
    EXPECT_EQ(a, 1);
    ringBuffer.PopFront(a);
    EXPECT_EQ(a, 2);
    EXPECT_EQ(ringBuffer.Size(), 0);
    ringBuffer.PushFront(2);
    ringBuffer.PushFront(1);
    ringBuffer.PushFront(0);
    int *b = new int[2];
    ret = ringBuffer.PopFrontN(b, 2);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(b[0], 0);
    EXPECT_EQ(b[1], 1);
    EXPECT_EQ(ringBuffer.Size(), 1);
    ringBuffer.UnInitialize();
    delete[] b;
}

TEST_F(TestCaseRingBuffer, NetRingBuffer_Ser_Fail)
{
    NResult result;
    bool ret;

    NetRingBuffer<int> zringBuffer(0);
    result = zringBuffer.Initialize();
    EXPECT_NE(result, NN_OK);

    NetRingBuffer<int> ringBuffer(2);
    result = ringBuffer.Initialize();
    EXPECT_EQ(result, NN_OK);

    ret = ringBuffer.PushBack(0);
    EXPECT_EQ(ret, true);
    ret = ringBuffer.PushBack(1);
    EXPECT_EQ(ret, true);
    ret = ringBuffer.PushBack(2);
    EXPECT_EQ(ret, false);
    EXPECT_EQ(ringBuffer.Size(), 2);

    int a = -1;
    ret = ringBuffer.PopFront(a);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(a, 0);
    ret = ringBuffer.PopFront(a);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(a, 1);
    ret = ringBuffer.PopFront(a);
    EXPECT_EQ(ret, false);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(ringBuffer.Size(), 0);

    ringBuffer.PushBack(0);
    ringBuffer.PushBack(1);
    int *b = new int[2];
    b[0] = -1;
    ret = ringBuffer.PopFrontN(b, 3);
    EXPECT_EQ(ret, false);
    EXPECT_EQ(b[0], -1);

    delete[] b;
}


TEST_F(TestCaseRingBuffer, NetRingBuffer_Con_OK)
{
    NResult result;
    bool ret = true;

    int count = 100;
    int vsum = 0;

    NetRingBuffer<int> ringBuffer(count);
    result = ringBuffer.Initialize();
    EXPECT_EQ(result, NN_OK);

    std::vector<std::thread> ths;
    for (int i = 0; i < count; ++i) {
        vsum += i;
        auto v = i;
        std::thread th([&, v]() { ret = ringBuffer.PushBack(v) && ret; });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
    EXPECT_EQ(ret, true);
    int sum = 0;
    int dup = 0;
    std::unordered_set<int> reads;
    for (int i = 0; i < count; ++i) {
        int r = -1;
        ret = ringBuffer.PopFront(r) && ret;
        if (reads.count(r) > 0) {
            dup++;
        }
        reads.insert(r);
        sum += r;
    }
    EXPECT_EQ(ret, true);
    EXPECT_EQ(sum, vsum);
    EXPECT_EQ(dup, 0);

    for (int i = 0; i < count; ++i) {
        ret = ringBuffer.PushBack(i) && ret;
    }
    EXPECT_EQ(ret, true);

    ths.clear();
    EXPECT_EQ(ringBuffer.Size(), count);
    sum = 0;
    dup = 0;
    std::mutex locker;
    reads.clear();
    ret = true;
    for (int i = 0; i < count; ++i) {
        std::thread th([&]() {
            int r = -1;
            ret = ringBuffer.PopFront(r) && ret;
            locker.lock();
            if (reads.count(r)) {
                dup++;
            }
            sum += r;
            locker.unlock();
        });
        ths.push_back(std::move(th));
    }
    for (int i = 0; i < count; ++i) {
        ths[i].join();
    }
    EXPECT_EQ(dup, 0);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(sum, vsum);
}