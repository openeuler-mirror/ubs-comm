/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "common/ubsocket_lock.h"
#include "common/ubsocket_ring_buffer.h"

using namespace ock::ubs;

class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        rb_.Initialize(16);
    }

    void TearDown() override
    {
        rb_.UnInitialize();
    }

    UbsocketRingBuffer<int> rb_;
};

TEST_F(RingBufferTest, Initialize_WithValidCapacity)
{
    UbsocketRingBuffer<int> rb;
    EXPECT_EQ(rb.Initialize(8), 0);
    EXPECT_EQ(rb.Capacity(), 8u);
}

TEST_F(RingBufferTest, Initialize_WithZeroCapacity_ReturnsError)
{
    UbsocketRingBuffer<int> rb;
    EXPECT_EQ(rb.Initialize(0), -1);
}

TEST_F(RingBufferTest, Initialize_DoubleInitReturnsOk)
{
    EXPECT_EQ(rb_.Initialize(32), 0);
    EXPECT_EQ(rb_.Capacity(), 16u); // capacity unchanged
}

TEST_F(RingBufferTest, PushBack_SingleItem)
{
    EXPECT_TRUE(rb_.PushBack(42));
    EXPECT_EQ(rb_.Size(), 1u);
}

TEST_F(RingBufferTest, PushBack_MultipleItems)
{
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(rb_.PushBack(i));
    }
    EXPECT_EQ(rb_.Size(), 10u);
}

TEST_F(RingBufferTest, PushBack_Full)
{
    for (int i = 0; i < 16; i++) {
        EXPECT_TRUE(rb_.PushBack(i));
    }
    EXPECT_TRUE(rb_.IsFull());
    EXPECT_FALSE(rb_.PushBack(100));
}

TEST_F(RingBufferTest, PushFront_SingleItem)
{
    EXPECT_TRUE(rb_.PushFront(42));
    EXPECT_EQ(rb_.Size(), 1u);
}

TEST_F(RingBufferTest, PushFront_ThenPopFront)
{
    rb_.PushFront(99);
    rb_.PushBack(100);
    int val;
    EXPECT_TRUE(rb_.PopFront(val));
    EXPECT_EQ(val, 99);
    EXPECT_TRUE(rb_.PopFront(val));
    EXPECT_EQ(val, 100);
}

TEST_F(RingBufferTest, PopFront_FromEmpty)
{
    int val;
    EXPECT_FALSE(rb_.PopFront(val));
}

TEST_F(RingBufferTest, PopFront_OrderPreserved)
{
    for (int i = 0; i < 5; i++) {
        rb_.PushBack(i);
    }
    for (int i = 0; i < 5; i++) {
        int val;
        EXPECT_TRUE(rb_.PopFront(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_EQ(rb_.Size(), 0u);
}

TEST_F(RingBufferTest, GetFront_WithoutPop)
{
    rb_.PushBack(42);
    int val;
    EXPECT_TRUE(rb_.GetFront(val));
    EXPECT_EQ(val, 42);
    EXPECT_EQ(rb_.Size(), 1u); // still there
}

TEST_F(RingBufferTest, GetFront_FromEmpty)
{
    int val;
    EXPECT_FALSE(rb_.GetFront(val));
}

TEST_F(RingBufferTest, PopFrontN_ValidBatch)
{
    for (int i = 0; i < 10; i++) {
        rb_.PushBack(i);
    }

    int items[5];
    EXPECT_TRUE(rb_.PopFrontN(items, 5));
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(items[i], i);
    }
    EXPECT_EQ(rb_.Size(), 5u);
}

TEST_F(RingBufferTest, PopFrontN_MoreThanSize)
{
    rb_.PushBack(1);
    rb_.PushBack(2);

    int items[5];
    EXPECT_FALSE(rb_.PopFrontN(items, 5));
    EXPECT_EQ(rb_.Size(), 2u);
}

TEST_F(RingBufferTest, PopFrontN_ZeroN)
{
    for (int i = 0; i < 5; i++) {
        rb_.PushBack(i);
    }
    int items[1];
    EXPECT_TRUE(rb_.PopFrontN(items, 0)); // n=0 special, should succeed
    EXPECT_EQ(rb_.Size(), 5u);
}

TEST_F(RingBufferTest, WrapAround)
{
    // Fill buffer to create wrap condition
    for (int i = 0; i < 8; i++) {
        rb_.PushBack(i);
    }
    for (int i = 0; i < 4; i++) {
        int val;
        rb_.PopFront(val);
    }
    for (int i = 100; i < 108; i++) {
        EXPECT_TRUE(rb_.PushBack(i));
    }
    // Now pop and verify order
    for (int i = 4; i < 8; i++) {
        int val;
        EXPECT_TRUE(rb_.PopFront(val));
        EXPECT_EQ(val, i);
    }
    for (int i = 100; i < 108; i++) {
        int val;
        EXPECT_TRUE(rb_.PopFront(val));
        EXPECT_EQ(val, i);
    }
}

TEST_F(RingBufferTest, Uninitialized_OperationsReturnFalse)
{
    UbsocketRingBuffer<int> ub;
    EXPECT_EQ(ub.Size(), 0u);
    EXPECT_FALSE(ub.PushBack(1));
    int val;
    EXPECT_FALSE(ub.PopFront(val));
    EXPECT_FALSE(ub.GetFront(val));
}

TEST_F(RingBufferTest, Size_And_IsFull)
{
    EXPECT_EQ(rb_.Size(), 0u);
    EXPECT_FALSE(rb_.IsFull());

    for (int i = 0; i < 16; i++) {
        rb_.PushBack(i);
    }

    EXPECT_EQ(rb_.Size(), 16u);
    EXPECT_TRUE(rb_.IsFull());
}

TEST_F(RingBufferTest, ThreadSafety_ConcurrentPushPop)
{
    UbsocketRingBuffer<int> rb;
    rb.Initialize(1024);

    const int items_per_thread = 100;
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; t++) {
        producers.emplace_back([&rb, t, items_per_thread]() {
            for (int i = 0; i < items_per_thread; i++) {
                rb.PushBack(t * items_per_thread + i);
            }
        });
    }

    for (auto &t : producers) {
        t.join();
    }

    EXPECT_EQ(rb.Size(), 400u);

    int popped = 0;
    int val;
    while (rb.PopFront(val)) {
        popped++;
    }

    EXPECT_EQ(popped, 400);
}
