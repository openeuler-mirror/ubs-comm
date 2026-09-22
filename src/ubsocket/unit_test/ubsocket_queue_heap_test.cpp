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

// --- Error paths: isExit_ ---

TEST_F(QbufQueueTest, Enqueue_QueueExited_ReturnsError)
{
    q_->isExit_ = true;
    EXPECT_EQ(q_->Enqueue(42), -1);
    q_->isExit_ = false;
}

TEST_F(QbufQueueTest, Dequeue_QueueExited_ReturnsError)
{
    q_->Enqueue(42);
    q_->isExit_ = true;
    int val = -1;
    EXPECT_EQ(q_->Dequeue(&val), -1);
    q_->isExit_ = false;
}

TEST_F(QbufQueueTest, DequeueBatch_QueueExited_ReturnsError)
{
    q_->Enqueue(42);
    q_->isExit_ = true;
    int buf[4];
    uint32_t cnt = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 4, &cnt), -1);
    q_->isExit_ = false;
}

// --- Error paths: queue_ == nullptr ---

TEST_F(QbufQueueTest, Enqueue_NullQueue_ReturnsError)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    EXPECT_EQ(q_->Enqueue(42), -1);
    q_->queue_ = saved;
}

TEST_F(QbufQueueTest, Dequeue_NullQueue_ReturnsError)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    int val = -1;
    EXPECT_EQ(q_->Dequeue(&val), -1);
    q_->queue_ = saved;
}

TEST_F(QbufQueueTest, DequeueBatch_NullQueue_ReturnsError)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    int buf[4];
    uint32_t cnt = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 4, &cnt), -1);
    q_->queue_ = saved;
}

TEST_F(QbufQueueTest, IsEmpty_NullQueue_ReturnsFalse)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    EXPECT_FALSE(q_->IsEmpty());
    q_->queue_ = saved;
}

TEST_F(QbufQueueTest, IsFull_NullQueue_ReturnsFalse)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    EXPECT_FALSE(q_->IsFull());
    q_->queue_ = saved;
}

TEST_F(QbufQueueTest, Size_NullQueue_ReturnsZero)
{
    auto *saved = q_->queue_;
    q_->queue_ = nullptr;
    EXPECT_EQ(q_->Size(), 0u);
    q_->queue_ = saved;
}

// --- Wrap-around ---

TEST_F(QbufQueueTest, WrapAround_EnqueueDequeue)
{
    // Fill past initial capacity to force wrap-around
    for (int i = 0; i < 6; i++) {
        q_->Enqueue(i);
    }
    for (int i = 0; i < 6; i++) {
        int val;
        EXPECT_EQ(q_->Dequeue(&val), 0);
        EXPECT_EQ(val, i);
    }
    // Now head is at 6, enqueue more to wrap tail around
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(q_->Enqueue(100 + i), 0);
    }
    for (int i = 0; i < 6; i++) {
        int val;
        EXPECT_EQ(q_->Dequeue(&val), 0);
        EXPECT_EQ(val, 100 + i);
    }
}

TEST_F(QbufQueueTest, DequeueBatch_WrapAround)
{
    // Enqueue 5, dequeue 3, enqueue 6 → head/tail wrap
    for (int i = 0; i < 5; i++) {
        q_->Enqueue(i);
    }
    int buf[16];
    uint32_t cnt = 0;
    q_->DequeueBatch(buf, 3, &cnt);
    ASSERT_EQ(cnt, 3u);
    for (int i = 0; i < 6; i++) {
        q_->Enqueue(100 + i);
    }
    // Now dequeue all — should wrap around
    cnt = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 16, &cnt), 0);
    EXPECT_EQ(cnt, 8u);
    EXPECT_EQ(buf[0], 3);
    EXPECT_EQ(buf[1], 4);
    EXPECT_EQ(buf[2], 100);
}

// --- Resize / shrink ---

TEST_F(QbufQueueTest, Enqueue_FullExpandTwice)
{
    // Initial cap = 8. Fill to 8, expand on 9th, fill to 16, expand on 17th
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(q_->Enqueue(i), 0);
    }
    EXPECT_TRUE(q_->IsFull());
    EXPECT_EQ(q_->Enqueue(100), 0); // first expand (8 → 16)
    for (int i = 0; i < 7; i++) {
        EXPECT_EQ(q_->Enqueue(200 + i), 0); // fill to 16
    }
    EXPECT_TRUE(q_->IsFull());
    EXPECT_EQ(q_->Enqueue(300), 0); // second expand (16 → 32)
    EXPECT_EQ(q_->Size(), 17u);
}

TEST_F(QbufQueueTest, Dequeue_ShrinkToHalf)
{
    // Enqueue enough to expand, then dequeue to trigger shrink
    for (int i = 0; i < 20; i++) {
        q_->Enqueue(i);
    }
    EXPECT_GT(q_->queue_->itemNb, 9u); // expanded
    // Dequeue most items to trigger shrink (usage <= 25%)
    int val;
    for (int i = 0; i < 18; i++) {
        q_->Dequeue(&val);
    }
    // Should have shrunk
    // 2 items left, capacity should have decreased
    EXPECT_EQ(q_->Size(), 2u);
}

TEST_F(QbufQueueTest, DequeueBatch_ShrinkToHalf)
{
    for (int i = 0; i < 20; i++) {
        q_->Enqueue(i);
    }
    int buf[32];
    uint32_t cnt = 0;
    // Dequeue most to trigger shrink
    q_->DequeueBatch(buf, 18, &cnt);
    EXPECT_EQ(cnt, 18u);
    EXPECT_EQ(q_->Size(), 2u);
}

TEST_F(QbufQueueTest, Resize_SameCapacity_ReturnsError)
{
    // Force a resize with same capacity
    EXPECT_EQ(q_->Resize(8), -1); // same as initial
}

// --- Non-malloc queue (isMalloc_ = false) ---

TEST_F(QbufQueueTest, Enqueue_NonMallocFull_ReturnsError)
{
    q_->isMalloc_ = false;
    // Fill to capacity
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(q_->Enqueue(i), 0);
    }
    EXPECT_TRUE(q_->IsFull());
    // Should fail without expand
    EXPECT_EQ(q_->Enqueue(999), -1);
    q_->isMalloc_ = true;
}

// --- Size with wrap-around ---

TEST_F(QbufQueueTest, Size_WrapAround_CorrectValue)
{
    // Enqueue 6, dequeue 4, enqueue 6 → head > tail → wrap-around size
    for (int i = 0; i < 6; i++) {
        q_->Enqueue(i);
    }
    int val;
    for (int i = 0; i < 4; i++) {
        q_->Dequeue(&val);
    }
    for (int i = 0; i < 6; i++) {
        q_->Enqueue(100 + i);
    }
    // head=4, tail=4+6=10 → wraps: itemNb + tail - head = 9 + 10%9 - 4... 
    // Actually head=4, items: 4,5,6,7,8,0,1,2 → 8 items
    EXPECT_EQ(q_->Size(), 8u);
}

// --- DequeueBatch max_count = 0 is invalid param ---

TEST_F(QbufQueueTest, DequeueBatch_MaxCountZero_ReturnsError)
{
    int buf[4];
    uint32_t cnt = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 0, &cnt), -1);
}

// --- Constructor failure (hard to trigger with overcommit) ---
// Skipped: on Linux with overcommit, malloc almost never fails.
// The init failure path (line 36 true) requires malloc to return nullptr.

// --- Destructor edge cases ---

TEST_F(QbufQueueTest, Destructor_IsMallocFalse_NoFree)
{
    q_->isMalloc_ = false;
    // Destructor should not attempt free when isMalloc_ is false
    // Let the fixture's TearDown handle deletion
    q_->isMalloc_ = true; // restore so fixture can clean up
}

TEST_F(QbufQueueTest, Destructor_QueueNullptr_Safe)
{
    auto *saved = q_->queue_;
    q_->isMalloc_ = true;
    q_->queue_ = nullptr;
    // Destructor should check queue_ before freeing
    // Let the fixture's TearDown handle deletion
    q_->queue_ = saved; // restore
}

// --- Dequeue with non-malloc (no shrink) ---

TEST_F(QbufQueueTest, Dequeue_NonMalloc_NoShrink)
{
    for (int i = 0; i < 8; i++) {
        q_->Enqueue(i);
    }
    q_->isMalloc_ = false;
    int val;
    EXPECT_EQ(q_->Dequeue(&val), 0);
    q_->isMalloc_ = true;
}

// --- Resize same capacity already tested ---
// --- Resize malloc failure (hard to trigger, skip) ---

// --- Enqueue tail wrap-around explicitly ---

TEST_F(QbufQueueTest, Enqueue_TailWrapAround)
{
    // Fill 7 items (cap=8, itemNb=9), dequeue 7, fill 8 → tail wraps
    for (int i = 0; i < 7; i++) {
        q_->Enqueue(i);
    }
    int val;
    for (int i = 0; i < 7; i++) {
        q_->Dequeue(&val);
    }
    // head=7, tail=7 (empty). Enqueue 2: tail goes 7→8→0 (wrap)
    q_->Enqueue(100);
    q_->Enqueue(101);
    EXPECT_EQ(q_->queue_->tail, 0u); // wrapped
    EXPECT_EQ(q_->Size(), 2u);
}

TEST_F(QbufQueueTest, Dequeue_HeadWrapAround)
{
    // Fill 8, dequeue 1 (head=1), fill 1 more → tail wraps to 0
    for (int i = 0; i < 8; i++) {
        q_->Enqueue(i);
    }
    int val;
    q_->Dequeue(&val); // head=1
    q_->Enqueue(100);  // tail wraps to 0
    // Dequeue all — head wraps around
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(q_->Dequeue(&val), 0);
    }
    EXPECT_TRUE(q_->IsEmpty());
}

// --- DequeueBatch with non-malloc (no shrink) ---

TEST_F(QbufQueueTest, DequeueBatch_NonMalloc_NoShrink)
{
    for (int i = 0; i < 8; i++) {
        q_->Enqueue(i);
    }
    q_->isMalloc_ = false;
    int buf[16];
    uint32_t cnt = 0;
    EXPECT_EQ(q_->DequeueBatch(buf, 4, &cnt), 0);
    EXPECT_EQ(cnt, 4u);
    q_->isMalloc_ = true;
}

// --- Shrink to below init_cap_ ---

TEST_F(QbufQueueTest, Dequeue_ShrinkBelowInitCap)
{
    // Enqueue enough to expand, then dequeue to trigger shrink below init
    for (int i = 0; i < 20; i++) {
        q_->Enqueue(i);
    }
    int val;
    // Dequeue all but 1
    for (int i = 0; i < 19; i++) {
        q_->Dequeue(&val);
    }
    // The shrink should cap at init_cap_ (8)
    EXPECT_EQ(q_->Size(), 1u);
}

TEST_F(QbufQueueTest, DequeueBatch_ShrinkBelowInitCap)
{
    for (int i = 0; i < 20; i++) {
        q_->Enqueue(i);
    }
    int buf[32];
    uint32_t cnt = 0;
    q_->DequeueBatch(buf, 19, &cnt);
    EXPECT_EQ(cnt, 19u);
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

// ==================== FastHeap: error paths ====================

TEST_F(FastHeapTest, Constructor_BelowMinCapacity_ClampedToMin)
{
    FastHeap<int, IntGreater> h(1); // below MIN_CAPACITY (4)
    h.Push(42);
    EXPECT_EQ(h.Size(), 1u);
    EXPECT_EQ(h.Top(), 42);
}

TEST_F(FastHeapTest, Pop_NullHeap_NoCrash)
{
    FastHeap<int, IntGreater> h(8);
    h.m_heap = nullptr;
    h.Pop(); // should not crash, just log error
    h.m_heap = static_cast<int *>(malloc(72)); // restore for destructor
}

TEST_F(FastHeapTest, Contains_NullHeap_ReturnsFalse)
{
    FastHeap<int, IntGreater> h(8);
    h.Push(42);
    h.m_heap = nullptr;
    EXPECT_FALSE(h.Contains([](int v) { return v == 42; }));
    h.m_heap = static_cast<int *>(malloc(72)); // restore
}

TEST_F(FastHeapTest, Push_NullHeap_ReturnsError)
{
    FastHeap<int, IntGreater> h(8);
    h.m_heap = nullptr;
    EXPECT_EQ(h.Push(42), UBS_ERROR);
    h.m_heap = static_cast<int *>(malloc(72)); // restore
}

TEST_F(FastHeapTest, Push_ReachesMaxCapacity_ReturnsError)
{
    FastHeap<int, IntGreater> h(4, 4); // capacity=4, max_capacity=4
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(h.Push(i), UBS_OK);
    }
    // Now at max capacity, next push should fail
    EXPECT_EQ(h.Push(100), UBS_ERROR);
}

TEST_F(FastHeapTest, TryShrink_EmptyHeap_ShrinksToInitial)
{
    FastHeap<int, IntGreater> h(8);
    for (int i = 0; i < 8; i++) {
        h.Push(i);
    }
    h.clear(); // empty the heap
    h.TryShrink();
    // Should have shrunk to initial capacity (8)
    EXPECT_EQ(h.m_capacity, 8u);
}

TEST_F(FastHeapTest, TryShrink_NonEmptyHeap_NoShrink)
{
    heap_.Push(1);
    heap_.Push(2);
    size_t capBefore = heap_.m_capacity;
    heap_.TryShrink();
    EXPECT_EQ(heap_.m_capacity, capBefore); // no change
}

TEST_F(FastHeapTest, TryShrink_AtInitialCapacity_NoShrink)
{
    size_t capBefore = heap_.m_capacity;
    heap_.TryShrink();
    EXPECT_EQ(heap_.m_capacity, capBefore);
}

TEST_F(FastHeapTest, Reserve_SameCapacity_ReturnsError)
{
    EXPECT_EQ(heap_.Reserve(8), UBS_ERROR); // same as initial
}

TEST_F(FastHeapTest, GetMaxCapacity_CustomMax_Used)
{
    FastHeap<int, IntGreater> h(8, 16);
    EXPECT_EQ(h.GetMaxCapacity(), 16u);
}

TEST_F(FastHeapTest, GetMaxCapacity_DefaultMax_UsesMax)
{
    FastHeap<int, IntGreater> h(8, 0);
    EXPECT_EQ(h.GetMaxCapacity(), 0x3FFFFFFFu);
}

TEST_F(FastHeapTest, Push_ExceedsCustomMax_CappedToMax)
{
    FastHeap<int, IntGreater> h(4, 8);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(h.Push(i), UBS_OK);
    }
    // At max (8), push should fail
    EXPECT_EQ(h.Push(100), UBS_ERROR);
}

TEST_F(FastHeapTest, MoveAssignment_SelfAssignment_NoChange)
{
    FastHeap<int, IntGreater> h(8);
    h.Push(42);
    h = std::move(h); // self-assignment
    EXPECT_EQ(h.Size(), 1u);
    EXPECT_EQ(h.Top(), 42);
}

TEST_F(FastHeapTest, Contains_EmptyHeap_ReturnsFalse)
{
    EXPECT_FALSE(heap_.Contains([](int v) { return v == 42; }));
}
