/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "common/ubsocket_mpsc_ring_queue.h"
#include "common/ubsocket_spsc_ring_queue.h"

using namespace ock::ubs;

// ==================== SPSC Ring Queue ====================

class SPSCRingQueueTest : public ::testing::Test {
protected:
    SPSCRingQueue<int> q_{64};
};

TEST_F(SPSCRingQueueTest, PushPop_SingleItem)
{
    EXPECT_TRUE(q_.Push(42));
    int val;
    EXPECT_TRUE(q_.Pop(val));
    EXPECT_EQ(val, 42);
}

TEST_F(SPSCRingQueueTest, PushPop_MultipleItems)
{
    for (int i = 0; i < 32; i++) {
        EXPECT_TRUE(q_.Push(i));
    }
    for (int i = 0; i < 32; i++) {
        int val;
        EXPECT_TRUE(q_.Pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST_F(SPSCRingQueueTest, Push_RvalueRef)
{
    EXPECT_TRUE(q_.Push(99));
    int val;
    EXPECT_TRUE(q_.Pop(val));
    EXPECT_EQ(val, 99);
}

TEST_F(SPSCRingQueueTest, Pop_FromEmpty)
{
    int val;
    EXPECT_FALSE(q_.Pop(val));
}

TEST_F(SPSCRingQueueTest, Push_ToFull)
{
    for (int i = 0; i < 64; i++) {
        EXPECT_TRUE(q_.Push(i));
    }
    EXPECT_FALSE(q_.Push(100)); // full
}

TEST_F(SPSCRingQueueTest, Size_And_Empty)
{
    EXPECT_TRUE(q_.Empty());
    EXPECT_EQ(q_.Size(), 0u);

    q_.Push(1);
    EXPECT_FALSE(q_.Empty());
    EXPECT_EQ(q_.Size(), 1u);

    int val;
    q_.Pop(val);
    EXPECT_TRUE(q_.Empty());
}

TEST_F(SPSCRingQueueTest, MultiPush)
{
    std::vector<int> data = {10, 20, 30, 40, 50};
    auto pushed = q_.MultiPush(data.begin(), data.end());
    EXPECT_EQ(pushed, 5u);

    EXPECT_EQ(q_.Size(), 5u);
}

TEST_F(SPSCRingQueueTest, MultiPush_Partial)
{
    // Fill the queue first
    for (int i = 0; i < 60; i++) {
        q_.Push(i);
    }

    std::vector<int> big_data(20, 999);
    auto pushed = q_.MultiPush(big_data.begin(), big_data.end());
    EXPECT_LT(pushed, 20u);
    EXPECT_GT(pushed, 0u);
}

TEST_F(SPSCRingQueueTest, MultiPop)
{
    for (int i = 0; i < 32; i++) {
        q_.Push(i);
    }

    std::vector<int> out(64);
    auto popped = q_.MultiPop(out.begin(), 20);
    EXPECT_EQ(popped, 20u);
    for (int i = 0; i < 20; i++) {
        EXPECT_EQ(out[i], i);
    }
    EXPECT_EQ(q_.Size(), 12u);
}

TEST_F(SPSCRingQueueTest, MultiPop_MoreThanAvailable)
{
    q_.Push(1);
    q_.Push(2);

    std::vector<int> out(10);
    auto popped = q_.MultiPop(out.begin(), 10);
    EXPECT_EQ(popped, 2u);
    EXPECT_TRUE(q_.Empty());
}

TEST_F(SPSCRingQueueTest, Concurrent_SingleProducerSingleConsumer)
{
    SPSCRingQueue<int> q(1024);
    std::atomic<bool> done{false};
    std::vector<int> consumed;

    std::thread consumer([&]() {
        std::vector<int> buf(64);
        size_t total = 0;
        while (total < 1000 || !done) {
            auto n = q.MultiPop(buf.begin(), 64);
            for (uint64_t i = 0; i < n; i++) {
                consumed.push_back(buf[i]);
            }
            total += n;
        }
        // drain remaining
        while (true) {
            int val;
            if (!q.Pop(val))
                break;
            consumed.push_back(val);
        }
    });

    for (int i = 0; i < 1000; i++) {
        while (!q.Push(i)) {
            // spin
        }
    }
    done = true;
    consumer.join();

    EXPECT_EQ(consumed.size(), 1000u);
    for (int i = 0; i < 1000; i++) {
        EXPECT_EQ(consumed[i], i);
    }
}

TEST_F(SPSCRingQueueTest, InvalidCapacity_Throws)
{
    EXPECT_THROW(SPSCRingQueue<int>(15), std::invalid_argument);
    EXPECT_NO_THROW(SPSCRingQueue<int>(16));
    EXPECT_NO_THROW(SPSCRingQueue<int>(128));
}

// ==================== MPSC Ring Queue ====================

class MPSCRingQueueTest : public ::testing::Test {
protected:
    MPSCRingQueue<int> q_{64};
};

TEST_F(MPSCRingQueueTest, PushPop_SingleItem)
{
    EXPECT_TRUE(q_.Push(42));
    int val;
    EXPECT_TRUE(q_.Pop(val));
    EXPECT_EQ(val, 42);
}

TEST_F(MPSCRingQueueTest, PushPop_MultipleItems)
{
    for (int i = 0; i < 32; i++) {
        EXPECT_TRUE(q_.Push(i));
    }
    for (int i = 0; i < 32; i++) {
        int val;
        EXPECT_TRUE(q_.Pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST_F(MPSCRingQueueTest, Pop_FromEmpty)
{
    int val;
    EXPECT_FALSE(q_.Pop(val));
}

TEST_F(MPSCRingQueueTest, Push_ToFull)
{
    for (int i = 0; i < 64; i++) {
        EXPECT_TRUE(q_.Push(i));
    }
    EXPECT_FALSE(q_.Push(100));
}

TEST_F(MPSCRingQueueTest, Size_And_Empty)
{
    EXPECT_TRUE(q_.Empty());
    EXPECT_EQ(q_.Size(), 0u);

    q_.Push(1);
    EXPECT_FALSE(q_.Empty());
    EXPECT_EQ(q_.Size(), 1u);

    int val;
    q_.Pop(val);
    EXPECT_TRUE(q_.Empty());
}

TEST_F(MPSCRingQueueTest, MultiPop)
{
    for (int i = 0; i < 20; i++) {
        q_.Push(i);
    }

    std::vector<int> out(64);
    auto popped = q_.MultiPop(out.begin(), 10);
    EXPECT_EQ(popped, 10u);
    EXPECT_EQ(q_.Size(), 10u);
}

TEST_F(MPSCRingQueueTest, MultiProducer_SingleConsumer)
{
    MPSCRingQueue<int> q(1024);
    std::atomic<int> total_pushed{0};

    auto producer = [&](int start, int count) {
        for (int i = start; i < start + count; i++) {
            while (!q.Push(i)) {
                std::this_thread::yield();
            }
            total_pushed++;
        }
    };

    std::thread p1(producer, 0, 300);
    std::thread p2(producer, 300, 300);
    std::thread p3(producer, 600, 400);

    p1.join();
    p2.join();
    p3.join();

    EXPECT_EQ(total_pushed, 1000);
    EXPECT_EQ(q.Size(), 1000u);

    // Drain and verify all values received
    bool seen[1000] = {false};
    int popped = 0;
    int val;
    while (q.Pop(val)) {
        ASSERT_LT(val, 1000);
        EXPECT_FALSE(seen[val]) << "Duplicate value: " << val;
        seen[val] = true;
        popped++;
    }
    EXPECT_EQ(popped, 1000);
    // Verify all values 0-999 received exactly once
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(seen[i]) << "Missing value: " << i;
    }
}

TEST_F(MPSCRingQueueTest, InvalidCapacity_Throws)
{
    EXPECT_THROW(MPSCRingQueue<int>(15), std::invalid_argument);
    EXPECT_NO_THROW(MPSCRingQueue<int>(16));
    EXPECT_NO_THROW(MPSCRingQueue<int>(256));
}
