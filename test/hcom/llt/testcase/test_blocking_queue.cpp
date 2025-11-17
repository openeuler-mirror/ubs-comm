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

#include "hcom_def.h"
#include "net_obj_pool.h"
#include "ut_helper.h"
#include "test_blocking_queue.h"

using namespace ock::hcom;
TestCaseBlockingQueue::TestCaseBlockingQueue() {}

void TestCaseBlockingQueue::SetUp() {}

void TestCaseBlockingQueue::TearDown() {}

TEST_F(TestCaseBlockingQueue, Serial)
{
    setenv("HCOM_TRACE_LEVEL", "2", 1);
    NResult result;
    bool ret;
    NetBlockingQueue<DummyObj> queue(2);
    result = queue.Initialize();
    EXPECT_EQ(result, NN_OK);
    DummyObj obj0(0);
    DummyObj obj1(1);
    DummyObj obj2(2);
    ret = queue.Enqueue(obj1);
    EXPECT_EQ(ret, true);
    ret = queue.EnqueueFirst(obj0);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj2);
    EXPECT_EQ(ret, false);
    ret = queue.EnqueueFirst(obj2);
    EXPECT_EQ(ret, false);

    DummyObj obj3;
    DummyObj obj4;
    ret = queue.Dequeue(obj3);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(obj3.tag, 0);
    ret = queue.Dequeue(obj4);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(obj4.tag, 1);
}

TEST_F(TestCaseBlockingQueue, Concurrency)
{
    NResult result;
    bool ret;
    NetBlockingQueue<DummyObj> queue(3);
    result = queue.Initialize();
    EXPECT_EQ(result, NN_OK);
    DummyObj obj0(0);
    DummyObj obj1(1);
    DummyObj obj2(2);
    DummyObj obj3(3);

    std::thread th([&]() {
        DummyObj obj0;
        DummyObj obj1;
        DummyObj obj2;
        bool ret;
        ret = queue.Dequeue(obj0);
        EXPECT_EQ(ret, true);
        EXPECT_EQ(obj0.tag, 0);
        ret = queue.Dequeue(obj1);
        EXPECT_EQ(ret, true);
        EXPECT_EQ(obj1.tag, 1);
        ret = queue.Dequeue(obj2);
        EXPECT_EQ(ret, true);
        EXPECT_EQ(obj2.tag, 2);
    });

    ret = queue.Enqueue(obj0);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj1);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj2);
    EXPECT_EQ(ret, true);
    th.join();
    ret = queue.Enqueue(obj0);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj1);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj2);
    EXPECT_EQ(ret, true);
    ret = queue.Enqueue(obj3);
    EXPECT_EQ(ret, false);
}