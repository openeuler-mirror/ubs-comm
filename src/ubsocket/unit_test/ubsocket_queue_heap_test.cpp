/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include <vector>

#include "common/ubsocket_fast_heap.h"
#include "common/ubsocket_qbuf_queue.h"

using namespace ock::ubs;

// ==================== QbufQueue ====================

class QbufQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        q_ = new QbufQueue<int>(8);
    }

    void TearDown() override
    {
        delete q_;
        q_ = nullptr;
    }

    QbufQueue<int> *q_;
};

TEST_F(QbufQueueTest, EnqueueDequeue_SingleItem)
{
    EXPECT_EQ(q_->Enqueue(42), 0);
    EXPECT_FALSE(q_->IsEmpty());

    int val;
    EXPECT_EQ(q_->Dequeue(&val), 0);
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q_->IsEmpty());
}

TEST_F(QbufQueueTest, EnqueueDequeue_MultipleItems)
{
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(q_->Enqueue(i), 0);
    }
    EXPECT_EQ(q_->Size(), 6u);

    for (int i = 0; i < 6; i++) {
        int val;
        EXPECT_EQ(q_->Dequeue(&val), 0);
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q_->IsEmpty());
}

TEST_F(QbufQueueTest, Enqueue_FullAndAutoExpand)
{
    const int init_cap = 8;
    for (int i = 0; i < init_cap; i++) {
        EXPECT_EQ(q_->Enqueue(i), 0);
    }
    EXPECT_TRUE(q_->IsFull());
    EXPECT_EQ(q_->Size(), 8u);

    // Auto expand on next enqueue
    EXPECT_EQ(q_->Enqueue(100), 0);
    EXPECT_EQ(q_->Size(), 9u);
    EXPECT_FALSE(q_->IsFull());
}

TEST_F(QbufQueueTest, Dequeue_EmptyReturnsError)
{
    int val;
    EXPECT_EQ(q_->Dequeue(&val), -1);
}

TEST_F(QbufQueueTest, Dequeue_NullPointer)
{
    q_->Enqueue(42);
    EXPECT_EQ(q_->Dequeue(nullptr), -1);
}

TEST_F(QbufQueueTest, DequeueBatch_SingleBatch)
{
    for (int i = 0; i < 5; i++) {
        q_->Enqueue(i);
    }

    int buf[10];
    uint32_t dequeued = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 5, &dequeued), 0);
    EXPECT_EQ(dequeued, 5u);
    for (uint32_t i = 0; i < dequeued; i++) {
        EXPECT_EQ(buf[i], (int)i);
    }
    EXPECT_TRUE(q_->IsEmpty());
}

TEST_F(QbufQueueTest, DequeueBatch_MoreThanAvailable)
{
    q_->Enqueue(1);
    q_->Enqueue(2);

    int buf[10];
    uint32_t dequeued = 0;
    // max_count > available, returns only what's there
    EXPECT_EQ(q_->DequeueBatch(buf, 10, &dequeued), 0);
    EXPECT_EQ(dequeued, 2u);
}

TEST_F(QbufQueueTest, DequeueBatch_InvalidParams)
{
    int buf[10];
    uint32_t dequeued = 0;
    EXPECT_EQ(q_->DequeueBatch(nullptr, 5, &dequeued), -1);
    EXPECT_EQ(q_->DequeueBatch(buf, 0, &dequeued), -1);
    EXPECT_EQ(q_->DequeueBatch(buf, 5, nullptr), -1);
}

TEST_F(QbufQueueTest, DequeueBatch_Empty)
{
    int buf[10];
    uint32_t dequeued = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 5, &dequeued), -1);
}

TEST_F(QbufQueueTest, IsEmpty_Initially)
{
    EXPECT_TRUE(q_->IsEmpty());
}

TEST_F(QbufQueueTest, QueueDefaults)
{
    // Verify the queue starts empty
    EXPECT_EQ(q_->Size(), 0u);
    EXPECT_TRUE(q_->IsEmpty());
    EXPECT_FALSE(q_->IsFull());
}

TEST_F(QbufQueueTest, Size_IncreasesAndDecreases)
{
    EXPECT_EQ(q_->Size(), 0u);

    q_->Enqueue(1);
    q_->Enqueue(2);
    EXPECT_EQ(q_->Size(), 2u);

    int val;
    q_->Dequeue(&val);
    EXPECT_EQ(q_->Size(), 1u);
}

// ==================== FastHeap ====================

struct IntGreater {
    bool operator()(const int &a, const int &b) const
    {
        return a < b; // min-heap
    }
};

class FastHeapTest : public ::testing::Test {
protected:
    FastHeap<int, IntGreater> heap_{8};
};

TEST_F(FastHeapTest, PushPop_SingleItem)
{
    EXPECT_EQ(heap_.Push(42), 0);
    EXPECT_FALSE(heap_.IsEmpty());
    EXPECT_EQ(heap_.Top(), 42);
    heap_.Pop();
    EXPECT_TRUE(heap_.IsEmpty());
}

TEST_F(FastHeapTest, PushMultiple_MinHeapProperty)
{
    heap_.Push(5);
    heap_.Push(3);
    heap_.Push(8);
    heap_.Push(1);
    heap_.Push(4);

    EXPECT_EQ(heap_.Top(), 1);

    int expected[] = {1, 3, 4, 5, 8};
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(heap_.Top(), expected[i]);
        heap_.Pop();
    }
    EXPECT_TRUE(heap_.IsEmpty());
}

TEST_F(FastHeapTest, Push_ExceedsInitialCapacity)
{
    for (int i = 100; i >= 1; i--) {
        EXPECT_EQ(heap_.Push(i), 0);
    }
    EXPECT_EQ(heap_.Size(), 100u);
    EXPECT_EQ(heap_.Top(), 1);
}

TEST_F(FastHeapTest, Pop_EmptyHeap)
{
    heap_.Pop(); // should not crash
    EXPECT_TRUE(heap_.IsEmpty());
}

TEST_F(FastHeapTest, Size_And_IsEmpty)
{
    EXPECT_TRUE(heap_.IsEmpty());
    EXPECT_EQ(heap_.Size(), 0u);

    heap_.Push(1);
    EXPECT_FALSE(heap_.IsEmpty());
    EXPECT_EQ(heap_.Size(), 1u);

    heap_.Pop();
    EXPECT_EQ(heap_.Size(), 0u);
}

TEST_F(FastHeapTest, Clear)
{
    heap_.Push(1);
    heap_.Push(2);
    heap_.Push(3);
    heap_.clear();
    EXPECT_TRUE(heap_.IsEmpty());
    EXPECT_EQ(heap_.Size(), 0u);
}

TEST_F(FastHeapTest, Contains)
{
    heap_.Push(10);
    heap_.Push(20);
    heap_.Push(30);

    EXPECT_TRUE(heap_.Contains([&](int v) { return v == 20; }));
    EXPECT_FALSE(heap_.Contains([&](int v) { return v == 99; }));
}

TEST_F(FastHeapTest, Contains_EmptyHeap)
{
    EXPECT_FALSE(heap_.Contains([&](int v) { return true; }));
}

TEST_F(FastHeapTest, MoveSemantics)
{
    heap_.Push(1);
    heap_.Push(2);

    FastHeap<int, IntGreater> heap2(std::move(heap_));
    EXPECT_FALSE(heap2.IsEmpty());
    EXPECT_EQ(heap2.Top(), 1);
    heap2.Pop();
    EXPECT_EQ(heap2.Top(), 2);
}

TEST_F(FastHeapTest, MoveAssignment)
{
    FastHeap<int, IntGreater> heap1(8);
    heap1.Push(100);

    FastHeap<int, IntGreater> heap2(8);
    heap2 = std::move(heap1);

    EXPECT_EQ(heap2.Top(), 100);
}

TEST_F(FastHeapTest, LargeDataset_MaintainsHeapProperty)
{
    FastHeap<int, IntGreater> h(64);

    std::vector<int> data;
    std::mt19937 rng(42);
    for (int i = 0; i < 200; i++) {
        int val = rng() % 10000;
        data.push_back(val);
        h.Push(val);
    }

    std::sort(data.begin(), data.end());

    for (int i = 0; i < 200; i++) {
        EXPECT_EQ(h.Top(), data[i]);
        h.Pop();
    }
    EXPECT_TRUE(h.IsEmpty());
}
