/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <stdexcept>

#include "urma_socket_buf.h"

using namespace ock::ubs::urma;

static constexpr uint64_t kTestCap = 4096;
static constexpr uint64_t kLargeCap = 65536;

// ---- Constructor Tests ----

TEST(UrmaSockRingBufTest, ConstructValid)
{
    UrmaSockRingBuf buf1(64);
    UrmaSockRingBuf buf2(1024);
    UrmaSockRingBuf buf3(kTestCap);
    UrmaSockRingBuf buf4(kLargeCap);
    UrmaSockRingBuf buf5(UrmaSockBuf::kMaxCapacity);
    SUCCEED();
}

TEST(UrmaSockRingBufTest, ConstructZeroCapacity)
{
    EXPECT_THROW(UrmaSockRingBuf(0), std::invalid_argument);
}

TEST(UrmaSockRingBufTest, ConstructTooLarge)
{
    EXPECT_THROW(UrmaSockRingBuf(UrmaSockBuf::kMaxCapacity * 2), std::invalid_argument);
}

TEST(UrmaSockRingBufTest, ConstructNotPowerOfTwo)
{
    EXPECT_THROW(UrmaSockRingBuf(96), std::invalid_argument);
    EXPECT_THROW(UrmaSockRingBuf(100), std::invalid_argument);
    EXPECT_THROW(UrmaSockRingBuf(4096 + 64), std::invalid_argument);
}

TEST(UrmaSockRingBufTest, ConstructNot64BAligned)
{
    EXPECT_THROW(UrmaSockRingBuf(32), std::invalid_argument);
}

// ---- Capacity / Available / Used Tests ----

TEST(UrmaSockRingBufTest, CapacityMatchesConstructor)
{
    UrmaSockRingBuf buf(kTestCap);
    EXPECT_EQ(buf.Capacity(), kTestCap);
}

TEST(UrmaSockRingBufTest, FreshBufferFullyAvailable)
{
    UrmaSockRingBuf buf(kTestCap);
    EXPECT_EQ(buf.Available(), kTestCap);
    EXPECT_EQ(buf.Used(), 0);
}

TEST(UrmaSockRingBufTest, AvailableUsedAfterAlloc)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    buf.Allocate(128, off);
    EXPECT_EQ(buf.Available(), kTestCap - 128);
    EXPECT_EQ(buf.Used(), 128);
}

// ---- Allocate Basic Tests ----

TEST(UrmaSockRingBufTest, AllocateSingleChunk)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(64, off));
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 64);
}

TEST(UrmaSockRingBufTest, AllocateMultipleChunksSequentialOffsets)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off1 = 0, off2 = 0, off3 = 0;
    EXPECT_TRUE(buf.Allocate(64, off1));
    EXPECT_EQ(off1, 0);
    EXPECT_TRUE(buf.Allocate(128, off2));
    EXPECT_EQ(off2, 64);
    EXPECT_TRUE(buf.Allocate(192, off3));
    EXPECT_EQ(off3, 64 + 128);
}

TEST(UrmaSockRingBufTest, AllocateRoundingUp)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(1, off));
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 64);
}

TEST(UrmaSockRingBufTest, AllocateRoundingBoundary)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(63, off));
    EXPECT_EQ(buf.Used(), 64);
    EXPECT_TRUE(buf.Allocate(65, off));
    EXPECT_EQ(buf.Used(), 64 + 128);
}

// ---- Allocate Exhaustion Tests ----

TEST(UrmaSockRingBufTest, AllocateFillExactly)
{
    UrmaSockRingBuf buf(kTestCap);
    for (uint64_t i = 0; i < kTestCap / UrmaSockBuf::kChunkSize; ++i) {
        uint64_t off = 0;
        EXPECT_TRUE(buf.Allocate(UrmaSockBuf::kChunkSize, off));
    }
    EXPECT_EQ(buf.Available(), 0);
    EXPECT_EQ(buf.Used(), kTestCap);
}

TEST(UrmaSockRingBufTest, AllocateExceedsAvailable)
{
    UrmaSockRingBuf buf(kTestCap);
    for (uint64_t i = 0; i < kTestCap / UrmaSockBuf::kChunkSize; ++i) {
        uint64_t off = 0;
        buf.Allocate(UrmaSockBuf::kChunkSize, off);
    }
    uint64_t off = 0;
    EXPECT_FALSE(buf.Allocate(UrmaSockBuf::kChunkSize, off));
    EXPECT_EQ(buf.Available(), 0);
}

// ---- Release In-Order Tests ----

TEST(UrmaSockRingBufTest, ReleaseInOrderSingleChunk)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    buf.Allocate(64, off);
    buf.Release(off, 64);
    EXPECT_EQ(buf.Available(), kTestCap);
    EXPECT_EQ(buf.Used(), 0);
}

TEST(UrmaSockRingBufTest, ReleaseInOrderMultipleChunks)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a);
    buf.Allocate(128, b);
    buf.Allocate(64, c);
    EXPECT_EQ(buf.Used(), 256);

    buf.Release(a, 64);
    EXPECT_EQ(buf.Used(), 192);
    buf.Release(b, 128);
    EXPECT_EQ(buf.Used(), 64);
    buf.Release(c, 64);
    EXPECT_EQ(buf.Used(), 0);
    EXPECT_EQ(buf.Available(), kTestCap);
}

// ---- Release Out-of-Order Tests ----

TEST(UrmaSockRingBufTest, ReleaseOutOfOrderTailFirstDoesNotAdvance)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a); // chunk 0
    buf.Allocate(64, b); // chunk 1
    buf.Allocate(64, c); // chunk 2
    EXPECT_EQ(buf.Used(), 192);

    buf.Release(c, 64);
    // tail released, but head not released -> Available does not change
    EXPECT_EQ(buf.Available(), kTestCap - 192);
    EXPECT_EQ(buf.Used(), 192);
}

TEST(UrmaSockRingBufTest, ReleaseOutOfOrderThenHeadUnblocksAdvance)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a);
    buf.Allocate(64, b);
    buf.Allocate(64, c);

    buf.Release(c, 64);
    // head not released, no advance
    EXPECT_EQ(buf.Available(), kTestCap - 192);
    buf.Release(a, 64);
    // head released, but b still held -> advances past a only
    EXPECT_EQ(buf.Available(), kTestCap - 128);

    buf.Release(b, 64);
    // all released, full advance
    EXPECT_EQ(buf.Available(), kTestCap);
}

TEST(UrmaSockRingBufTest, ReleaseOutOfOrderMiddleBlocksAdvance)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t a = 0, b = 0, c = 0, d = 0;
    buf.Allocate(64, a);
    buf.Allocate(64, b);
    buf.Allocate(64, c);
    buf.Allocate(64, d);

    buf.Release(d, 64);
    buf.Release(c, 64);
    EXPECT_EQ(buf.Available(), kTestCap - 256);
    buf.Release(a, 64);
    EXPECT_EQ(buf.Available(), kTestCap - 192); // a released, b blocks c+d
    buf.Release(b, 64);
    EXPECT_EQ(buf.Available(), kTestCap);
}

// ---- Release with SizeRounding Tests ----

TEST(UrmaSockRingBufTest, ReleaseWithSizeRounding)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    buf.Allocate(100, off); // rounds to 128B = 2 chunks
    EXPECT_EQ(buf.Used(), 128);
    buf.Release(off, 100);
    // Release also rounds up, releasing 2 chunks
    EXPECT_EQ(buf.Available(), kTestCap);
}

// ---- Available Query Tests ----

TEST(UrmaSockRingBufTest, AvailableAfterPartialRelease)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t a = 0, b = 0;
    buf.Allocate(64, a);
    buf.Allocate(64, b);
    buf.Release(a, 64);
    EXPECT_EQ(buf.Available(), kTestCap - 64);
    EXPECT_EQ(buf.Used(), 64);
}

// ---- Wrap-around Tests ----

TEST(UrmaSockRingBufTest, AllocateWrapAroundAfterFullDrain)
{
    UrmaSockRingBuf buf(kTestCap);
    const uint64_t num_chunks = kTestCap / UrmaSockBuf::kChunkSize;

    // fill completely
    uint64_t first_off = 0;
    for (uint64_t i = 0; i < num_chunks; ++i) {
        uint64_t off = 0;
        buf.Allocate(UrmaSockBuf::kChunkSize, off);
        if (i == 0) {
            first_off = off;
        }
    }
    EXPECT_EQ(buf.Available(), 0);

    // release all
    buf.Release(first_off, kTestCap);
    EXPECT_EQ(buf.Available(), kTestCap);

    // allocate near-full again, write_pos logical is 2*kTestCap now
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(kTestCap - UrmaSockBuf::kChunkSize, off));
    // offset wraps to 0
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), kTestCap - UrmaSockBuf::kChunkSize);
}

TEST(UrmaSockRingBufTest, ReleaseWrapAround)
{
    UrmaSockRingBuf buf(kTestCap);
    const uint64_t num_chunks = kTestCap / UrmaSockBuf::kChunkSize;

    // fill and drain: fill, release all, so write_pos and read_pos both at kTestCap
    uint64_t first_off = 0;
    for (uint64_t i = 0; i < num_chunks; ++i) {
        uint64_t off = 0;
        buf.Allocate(UrmaSockBuf::kChunkSize, off);
        if (i == 0) {
            first_off = off;
        }
    }
    buf.Release(first_off, kTestCap);

    // allocate near-full, wraps to physical offset 0
    uint64_t off_a = 0;
    buf.Allocate((num_chunks - 2) * UrmaSockBuf::kChunkSize, off_a);
    EXPECT_EQ(off_a, 0);

    uint64_t off_b = 0;
    buf.Allocate(UrmaSockBuf::kChunkSize, off_b);

    uint64_t off_c = 0;
    buf.Allocate(UrmaSockBuf::kChunkSize, off_c);

    EXPECT_EQ(buf.Available(), 0);

    // release wrapped region
    buf.Release(off_b, UrmaSockBuf::kChunkSize);
    buf.Release(off_a, (num_chunks - 2) * UrmaSockBuf::kChunkSize);
    EXPECT_EQ(buf.Available(), kTestCap - UrmaSockBuf::kChunkSize);
    buf.Release(off_c, UrmaSockBuf::kChunkSize);
    EXPECT_EQ(buf.Available(), kTestCap);
}

// ---- Multi-Cycle Integration Tests ----

TEST(UrmaSockRingBufTest, MultipleFillAndDrain)
{
    UrmaSockRingBuf buf(kTestCap);
    const uint64_t num_chunks = kTestCap / UrmaSockBuf::kChunkSize;

    for (int cycle = 0; cycle < 5; ++cycle) {
        uint64_t offsets[70];
        for (uint64_t i = 0; i < num_chunks; ++i) {
            ASSERT_TRUE(buf.Allocate(UrmaSockBuf::kChunkSize, offsets[i]));
        }
        EXPECT_EQ(buf.Available(), 0);
        EXPECT_EQ(buf.Used(), kTestCap);

        for (uint64_t i = 0; i < num_chunks; ++i) {
            buf.Release(offsets[i], UrmaSockBuf::kChunkSize);
        }
        EXPECT_EQ(buf.Available(), kTestCap);
        EXPECT_EQ(buf.Used(), 0);
    }
}

TEST(UrmaSockRingBufTest, ReleaseZeroSizeChunkNoOp)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    buf.Allocate(64, off);
    buf.Release(off, 0); // aligned up: (0+63) & ~63 = 0, no chunks released
    EXPECT_EQ(buf.Used(), 64);
    buf.Release(off, 64);
    EXPECT_EQ(buf.Used(), 0);
}

// ==========================================================================
// UrmaSockRbTreeBuf Tests
// ==========================================================================

static constexpr uint64_t kRbTestCap = 4160; // 65 chunks, non-power-of-2

// ---- Constructor Tests ----

TEST(UrmaSockRbTreeBufTest, ConstructValid)
{
    UrmaSockRbTreeBuf buf1(64);
    UrmaSockRbTreeBuf buf2(1024);
    UrmaSockRbTreeBuf buf3(kTestCap);
    UrmaSockRbTreeBuf buf4(kRbTestCap); // non-power-of-2
    UrmaSockRbTreeBuf buf5(kLargeCap);
    UrmaSockRbTreeBuf buf6(UrmaSockBuf::kMaxCapacity);
    SUCCEED();
}

TEST(UrmaSockRbTreeBufTest, ConstructZeroCapacity)
{
    EXPECT_THROW(UrmaSockRbTreeBuf(0), std::invalid_argument);
}

TEST(UrmaSockRbTreeBufTest, ConstructTooLarge)
{
    EXPECT_THROW(UrmaSockRbTreeBuf(UrmaSockBuf::kMaxCapacity * 2), std::invalid_argument);
}

TEST(UrmaSockRbTreeBufTest, ConstructNot64BAligned)
{
    EXPECT_THROW(UrmaSockRbTreeBuf(32), std::invalid_argument);
}

TEST(UrmaSockRbTreeBufTest, ConstructNonPowerOfTwoOk)
{
    UrmaSockRbTreeBuf buf1(kRbTestCap);
    UrmaSockRbTreeBuf buf2(960); // 15 chunks
    SUCCEED();
}

// ---- Capacity / Available / Used Tests ----

TEST(UrmaSockRbTreeBufTest, CapacityMatchesConstructor)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    EXPECT_EQ(buf.Capacity(), kRbTestCap);
}

TEST(UrmaSockRbTreeBufTest, FreshBufferFullyAvailable)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    EXPECT_EQ(buf.Available(), kRbTestCap);
    EXPECT_EQ(buf.Used(), 0);
}

TEST(UrmaSockRbTreeBufTest, AvailableUsedAfterAlloc)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    buf.Allocate(128, off);
    EXPECT_EQ(buf.Available(), kRbTestCap - 128);
    EXPECT_EQ(buf.Used(), 128);
}

// ---- Allocate Tests ----

TEST(UrmaSockRbTreeBufTest, AllocateSingleChunk)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(64, off));
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 64);
}

TEST(UrmaSockRbTreeBufTest, AllocateMultipleChunksSequentialOffsets)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off1 = 0, off2 = 0, off3 = 0;
    EXPECT_TRUE(buf.Allocate(64, off1));
    EXPECT_EQ(off1, 0);
    EXPECT_TRUE(buf.Allocate(128, off2));
    EXPECT_EQ(off2, 64);
    EXPECT_TRUE(buf.Allocate(192, off3));
    EXPECT_EQ(off3, 64 + 128);
}

TEST(UrmaSockRbTreeBufTest, AllocateRoundingUp)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(1, off));
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 64);
}

TEST(UrmaSockRbTreeBufTest, AllocateRoundingBoundary)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    EXPECT_TRUE(buf.Allocate(63, off));
    EXPECT_EQ(buf.Used(), 64);
    EXPECT_TRUE(buf.Allocate(65, off));
    EXPECT_EQ(buf.Used(), 64 + 128);
}

TEST(UrmaSockRbTreeBufTest, AllocateFillExactly)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    const uint64_t num_chunks = kRbTestCap / UrmaSockBuf::kChunkSize;
    for (uint64_t i = 0; i < num_chunks; ++i) {
        uint64_t off = 0;
        EXPECT_TRUE(buf.Allocate(UrmaSockBuf::kChunkSize, off));
    }
    EXPECT_EQ(buf.Available(), 0);
    EXPECT_EQ(buf.Used(), kRbTestCap);
}

TEST(UrmaSockRbTreeBufTest, AllocateExceedsAvailable)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    const uint64_t num_chunks = kRbTestCap / UrmaSockBuf::kChunkSize;
    for (uint64_t i = 0; i < num_chunks; ++i) {
        uint64_t off = 0;
        buf.Allocate(UrmaSockBuf::kChunkSize, off);
    }
    uint64_t off = 0;
    EXPECT_FALSE(buf.Allocate(UrmaSockBuf::kChunkSize, off));
}

// ---- Release In-Order Tests ----

TEST(UrmaSockRbTreeBufTest, ReleaseInOrderSingleChunk)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    buf.Allocate(64, off);
    buf.Release(off, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap);
    EXPECT_EQ(buf.Used(), 0);
}

TEST(UrmaSockRbTreeBufTest, ReleaseInOrderMultipleChunks)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a);
    buf.Allocate(128, b);
    buf.Allocate(64, c);
    EXPECT_EQ(buf.Used(), 256);

    buf.Release(a, 64);
    EXPECT_EQ(buf.Used(), 192);
    buf.Release(b, 128);
    EXPECT_EQ(buf.Used(), 64);
    buf.Release(c, 64);
    EXPECT_EQ(buf.Used(), 0);
    EXPECT_EQ(buf.Available(), kRbTestCap);
}

// ---- Release Out-of-Order Tests (immediate reclaim) ----

TEST(UrmaSockRbTreeBufTest, ReleaseOutOfOrderTailImmediatelyReclaimed)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a);
    buf.Allocate(64, b);
    buf.Allocate(64, c);
    EXPECT_EQ(buf.Used(), 192);

    buf.Release(c, 64);
    // RbTree releases tail immediately; only c is freed (64B), a+b still held
    EXPECT_EQ(buf.Available(), kRbTestCap - 128);

    buf.Release(a, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap - 64);

    buf.Release(b, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap);
}

TEST(UrmaSockRbTreeBufTest, ReleaseMiddleHoleReclaimable)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0, c = 0, d = 0;
    buf.Allocate(64, a);
    buf.Allocate(64, b);
    buf.Allocate(64, c);
    buf.Allocate(64, d);

    buf.Release(b, 64);
    // middle hole freed, available += 64
    EXPECT_EQ(buf.Available(), kRbTestCap - 192);

    buf.Release(d, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap - 128);

    buf.Release(a, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap - 64);

    buf.Release(c, 64);
    EXPECT_EQ(buf.Available(), kRbTestCap);
}

// ---- Release Coalescing Tests ----

TEST(UrmaSockRbTreeBufTest, ReleaseCoalesceWithPredecessor)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0;
    buf.Allocate(64, a); // offset 0
    buf.Allocate(64, b); // offset 64
    buf.Release(a, 64);

    // now free block: [0, 64)
    uint64_t c = 0;
    buf.Allocate(64, c);
    EXPECT_EQ(c, 0); // best-fit should pick the 64B block at offset 0

    buf.Release(b, 64);
    buf.Release(c, 64);
    // after releasing both, the two adjacent blocks coalesce into [0, 128)
    uint64_t d = 0;
    EXPECT_TRUE(buf.Allocate(128, d));
    EXPECT_EQ(d, 0); // should get the full coalesced block at offset 0
}

TEST(UrmaSockRbTreeBufTest, ReleaseCoalesceWithSuccessor)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0;
    buf.Allocate(64, a); // offset 0
    buf.Allocate(64, b); // offset 64
    buf.Release(b, 64);

    // now free block: [64, 128)
    uint64_t c = 0;
    buf.Allocate(64, c);
    EXPECT_EQ(c, 64); // should pick the 64B block at offset 64

    buf.Release(a, 64); // releases [0, 64)
    buf.Release(c, 64); // releases [64, 128) — coalesces with predecessor
    uint64_t d = 0;
    EXPECT_TRUE(buf.Allocate(128, d));
    EXPECT_EQ(d, 0); // coalesced block at offset 0
}

TEST(UrmaSockRbTreeBufTest, ReleaseCoalesceBothSides)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t a = 0, b = 0, c = 0;
    buf.Allocate(64, a); // offset 0
    buf.Allocate(64, b); // offset 64
    buf.Allocate(64, c); // offset 128
    buf.Release(a, 64);  // free [0, 64)
    buf.Release(c, 64);  // free [128, 192)
    buf.Release(b, 64);  // free [64, 128) — merges with both sides → [0, 192)

    uint64_t d = 0;
    EXPECT_TRUE(buf.Allocate(192, d));
    EXPECT_EQ(d, 0); // coalesced into one big block
}

// ---- Best-Fit Tests ----

TEST(UrmaSockRbTreeBufTest, AllocateBestFitPicksSmallestBlock)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);

    // consume the entire buffer in differently-sized allocations
    uint64_t off_a = 0, off_b = 0, off_c = 0;
    EXPECT_TRUE(buf.Allocate(64, off_a));               // [0, 64)
    EXPECT_TRUE(buf.Allocate(128, off_b));              // [64, 192)
    EXPECT_TRUE(buf.Allocate(kRbTestCap - 192, off_c)); // [192, end)

    // release a and b, creating two free blocks: 64B at 0, 128B at 64
    buf.Release(off_a, 64);
    buf.Release(off_b, 128);

    // allocate 64B — should pick the 64B block at offset 0, not the 128B block
    uint64_t d = 0;
    EXPECT_TRUE(buf.Allocate(64, d));
    EXPECT_EQ(d, 0);

    // allocate another 64B — should now take from the 128B block at offset 64
    uint64_t e = 0;
    EXPECT_TRUE(buf.Allocate(64, e));
    EXPECT_EQ(e, 64);
}

// ---- Multi-Cycle Integration Tests ----

TEST(UrmaSockRbTreeBufTest, MultipleFillAndDrain)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    const uint64_t num_chunks = kRbTestCap / UrmaSockBuf::kChunkSize;

    for (int cycle = 0; cycle < 5; ++cycle) {
        uint64_t offsets[70];
        for (uint64_t i = 0; i < num_chunks; ++i) {
            ASSERT_TRUE(buf.Allocate(UrmaSockBuf::kChunkSize, offsets[i]));
        }
        EXPECT_EQ(buf.Available(), 0);
        EXPECT_EQ(buf.Used(), kRbTestCap);

        for (uint64_t i = 0; i < num_chunks; ++i) {
            buf.Release(offsets[i], UrmaSockBuf::kChunkSize);
        }
        EXPECT_EQ(buf.Available(), kRbTestCap);
        EXPECT_EQ(buf.Used(), 0);
    }
}

// ==========================================================================
// UrmaSockRingBuf — Range Allocate(minSize, maxSize, offset) Tests
// ==========================================================================

TEST(UrmaSockRingBufTest, RangeAllocateWhenCapacityExceedsMax)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 256, off);
    EXPECT_EQ(sz, 256);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 256);
}

TEST(UrmaSockRingBufTest, RangeAllocateReturnsAvailableWhenBelowMax)
{
    UrmaSockRingBuf buf(kTestCap);
    // fill nearly all: leave only 128B free
    for (uint64_t i = 0; i < (kTestCap / 64) - 2; ++i) {
        uint64_t off = 0;
        buf.Allocate(64, off);
    }
    EXPECT_EQ(buf.Available(), 128);

    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 4096, off);
    EXPECT_EQ(sz, 128);
    EXPECT_EQ(buf.Available(), 0);
}

TEST(UrmaSockRingBufTest, RangeAllocateReturnsZeroWhenBelowMin)
{
    UrmaSockRingBuf buf(kTestCap);
    // consume all but 64B
    for (uint64_t i = 0; i < (kTestCap / 64) - 1; ++i) {
        uint64_t off = 0;
        buf.Allocate(64, off);
    }
    EXPECT_EQ(buf.Available(), 64);

    uint64_t off = 0;
    uint64_t sz = buf.Allocate(128, 256, off);
    EXPECT_EQ(sz, 0);
    EXPECT_EQ(buf.Available(), 64);
}

TEST(UrmaSockRingBufTest, RangeAllocateMinEqualMax)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(128, 128, off);
    EXPECT_EQ(sz, 128);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 128);
}

TEST(UrmaSockRingBufTest, RangeAllocateRoundsUpSizes)
{
    UrmaSockRingBuf buf(kTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(65, 130, off);
    // 65 rounds to 128, 130 rounds to 192
    EXPECT_EQ(sz, 192);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 192);
}

// ==========================================================================
// UrmaSockRbTreeBuf — Range Allocate(minSize, maxSize, offset) Tests
// ==========================================================================

TEST(UrmaSockRbTreeBufTest, RangeAllocateReturnsMaxSizeWhenLargeBlockExists)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 1024, off);
    EXPECT_EQ(sz, 1024);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 1024);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateReturnsLargestBlockWhenNoBlockExceedsMax)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    // fill all but 384B
    uint64_t offHold = 0;
    buf.Allocate(kRbTestCap - 384, offHold);

    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 1024, off);
    // available is 384 (< 1024), return 384
    EXPECT_EQ(sz, 384);
    EXPECT_EQ(off, kRbTestCap - 384);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateReturnsZeroWhenNoBlockAboveMin)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    // fill all but 64B
    uint64_t offHold = 0;
    buf.Allocate(kRbTestCap - 64, offHold);

    uint64_t off = 0;
    uint64_t sz = buf.Allocate(128, 512, off);
    EXPECT_EQ(sz, 0);
    EXPECT_EQ(buf.Available(), 64);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocatePicksBlockSatisfyingMaxOverSmallBlock)
{
    // Create non-adjacent free blocks: 128B, 1024B, 4096B separated by
    // allocated gaps to prevent Release coalescing.
    // Layout: [128B free] [64B alloc] [1024B free] [64B alloc] [4096B free] [remainder]
    static constexpr uint64_t kCap = 12288;
    UrmaSockRbTreeBuf buf(kCap);

    uint64_t offA = 0, offGap1 = 0, offB = 0, offGap2 = 0, offC = 0, offD = 0;
    buf.Allocate(128, offA);         // [0, 128)
    buf.Allocate(64, offGap1);       // [128, 192) — keeps gap, never released
    buf.Allocate(1024, offB);        // [192, 1216)
    buf.Allocate(64, offGap2);       // [1216, 1280) — keeps gap, never released
    buf.Allocate(4096, offC);        // [1280, 5376)
    buf.Allocate(kCap - 5376, offD); // remainder

    buf.Release(offA, 128);
    buf.Release(offB, 1024);
    buf.Release(offC, 4096);

    // Free blocks: 128B at 0, 1024B at 192, 4096B at 1280
    // Allocate(64, 4096) → should return 4096 from the 4096B block
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 4096, off);
    EXPECT_EQ(sz, 4096);
    EXPECT_EQ(off, 1280);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocatePicksBlockSatisfyingMaxAndSplitsRemainder)
{
    // Non-adjacent free blocks: 128B, 8KB separated by a 64B allocated gap.
    // Layout: [128B free] [64B alloc] [8KB free] [remainder]
    static constexpr uint64_t kCap = 16384;
    UrmaSockRbTreeBuf buf(kCap);

    uint64_t offA = 0, offGap = 0, offB = 0, offC = 0;
    buf.Allocate(128, offA);         // [0, 128)
    buf.Allocate(64, offGap);        // [128, 192) — keeps gap
    buf.Allocate(8192, offB);        // [192, 8384)
    buf.Allocate(kCap - 8384, offC); // remainder

    buf.Release(offA, 128);
    buf.Release(offB, 8192);

    // Free: 128B at 0, 8KB at 192
    // Allocate(64, 2048) → should pick 8KB block, return 2048, split remainder
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 2048, off);
    EXPECT_EQ(sz, 2048);
    EXPECT_EQ(off, 192); // start of the 8KB block

    // remaining: 8192 - 2048 = 6144 at offset 192 + 2048 = 2240
    uint64_t off2 = 0;
    uint64_t sz2 = buf.Allocate(6144, 6144, off2);
    EXPECT_EQ(sz2, 6144);
    EXPECT_EQ(off2, 2240);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateReturnsLargestBlockWhenMultipleBelowMax)
{
    // Non-adjacent free blocks: 128B, 256B, 512B separated by 64B allocated gaps.
    // Layout: [128B free] [64B alloc] [256B free] [64B alloc] [512B free] [remainder]
    static constexpr uint64_t kCap = 3072;
    UrmaSockRbTreeBuf buf(kCap);

    uint64_t offA = 0, offGap1 = 0, offB = 0, offGap2 = 0, offC = 0, offD = 0;
    buf.Allocate(128, offA);         // [0, 128)
    buf.Allocate(64, offGap1);       // [128, 192) — gap
    buf.Allocate(256, offB);         // [192, 448)
    buf.Allocate(64, offGap2);       // [448, 512) — gap
    buf.Allocate(512, offC);         // [512, 1024)
    buf.Allocate(kCap - 1024, offD); // remainder

    buf.Release(offA, 128);
    buf.Release(offB, 256);
    buf.Release(offC, 512);

    // Free blocks: 128B at 0, 256B at 192, 512B at 512
    // None >= 4096, should pick the 512B (largest)
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(64, 4096, off);
    EXPECT_EQ(sz, 512);
    EXPECT_EQ(off, 512);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateMinEqualMaxReturnsExactSize)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(256, 256, off);
    EXPECT_EQ(sz, 256);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 256);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateRoundsUpSizes)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    uint64_t sz = buf.Allocate(65, 130, off);
    // 65 rounds to 128, 130 rounds to 192
    EXPECT_EQ(sz, 192);
    EXPECT_EQ(off, 0);
    EXPECT_EQ(buf.Used(), 192);
}

TEST(UrmaSockRbTreeBufTest, RangeAllocateUsedPlusAvailableEqualsCapacity)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    uint64_t off = 0;
    buf.Allocate(64, 256, off);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);
}

// ---- Used/Available Correlation Tests ----

TEST(UrmaSockRbTreeBufTest, UsedPlusAvailableEqualsCapacity)
{
    UrmaSockRbTreeBuf buf(kRbTestCap);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);

    uint64_t a = 0, b = 0;
    buf.Allocate(64, a);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);
    buf.Allocate(128, b);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);
    buf.Release(a, 64);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);
    buf.Release(b, 128);
    EXPECT_EQ(buf.Available() + buf.Used(), kRbTestCap);
    EXPECT_EQ(buf.Available(), kRbTestCap);
}
