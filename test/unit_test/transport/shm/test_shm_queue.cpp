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

#include "shm_worker.h"
#include "shm_handle.h"
#include "shm_queue.h"
#include "shm_common.h"

namespace ock {
namespace hcom {
class TestShmQueue : public testing::Test {
public:
    TestShmQueue();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string name = "TestShmQueue";
    ShmEvent event {};
    ShmEventQueuePtr queue;
    ShmQueueMeta *queueMeta;
};

TestShmQueue::TestShmQueue() {}

void TestShmQueue::SetUp()
{
    queueMeta = new (std::nothrow) ShmQueueMeta();
    ASSERT_NE(queueMeta, nullptr);
    queue = new (std::nothrow) ShmEventQueue(name, NN_NO2048, nullptr);
    ASSERT_NE(queue, nullptr);
    queue->mQueueMeta = queueMeta;
    queue->mQueueData = new (std::nothrow) ShmEvent[NN_NO2048];
    ASSERT_NE(queue->mQueueData, nullptr);
}

void TestShmQueue::TearDown()
{
    if (queueMeta != nullptr) {
        delete queueMeta;
        queue->mQueueMeta = nullptr;
    }
    if (queue->mQueueData != nullptr) {
        delete queue->mQueueData;
        queue->mQueueData = nullptr;
    }
    if (queue != nullptr) {
        queue.Set(nullptr);
    }
    GlobalMockObject::verify();
}

TEST_F(TestShmQueue, EnqueueFailed)
{
    queue->mInited = true;
    queue->mMaxEnqueueTimeout = 0;
    queue->mQueueMeta->prod.tail = 0;
    queue->mQueueMeta->prod.head = 1;
    queue->mQueueMeta->cons.tail = 0;
    queue->mQueueMeta->cons.head = 0;
    EXPECT_EQ(queue->Enqueue(event), static_cast<int>(ShmEventQueue::SHM_QUEUE_FULL));
}

TEST_F(TestShmQueue, DequeueFailed)
{
    queue->mInited = true;
    queue->mMaxEnqueueTimeout = 0;
    queue->mQueueMeta->prod.tail = 0;
    queue->mQueueMeta->prod.head = 0;
    queue->mQueueMeta->cons.tail = 0;
    queue->mQueueMeta->cons.head = 0;
    queue->mFailedProd = 0;
    EXPECT_EQ(queue->Dequeue(event), static_cast<int>(ShmEventQueue::SHM_QUEUE_EMPTY));
}

TEST_F(TestShmQueue, CheckState)
{
    queue->mInited = true;
    queue->mMaxFailedTime = 0;
    queue->mQueueMeta->prod.tail = 0;
    queue->mQueueMeta->prod.head = 1;
    queue->mTempProdIdx = UINT64_MAX;
    queue->mFailedProd = UINT64_MAX;
    EXPECT_NO_FATAL_FAILURE(queue->CheckAndMarkProducerState());
    EXPECT_EQ(queue->mTempProdIdx, queue->mQueueMeta->prod.tail);

    EXPECT_NO_FATAL_FAILURE(queue->CheckAndMarkProducerState());
    EXPECT_EQ(queue->mQueueMeta->prod.tail, NN_NO1);
}
}
}