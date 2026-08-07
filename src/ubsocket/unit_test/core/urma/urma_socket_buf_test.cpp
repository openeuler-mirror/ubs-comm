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
#include <cerrno>
#include <vector>
#include <list>

#include "core/urma/urma_socket_buf.h"

using namespace ock::ubs::urma;

namespace {

static constexpr uint64_t TEST_CAPACITY = 256;
static constexpr uint64_t TEST_CHUNK = UrmaSockBuf::kChunkSize;

class TestUrmaSockBuf : public UrmaSockBuf {
public:
    explicit TestUrmaSockBuf(uint64_t capacity) : capacity_(capacity), available_(capacity), used_(0) {}

    bool Allocate(uint64_t, uint64_t &offset) noexcept override
    {
        if (alloc_fail_ || alloc_count_ == fail_on_count_) {
            return false;
        }
        if (alloc_offsets_.size() <= alloc_count_) {
            offset = alloc_offset_;
        } else {
            offset = alloc_offsets_[alloc_count_];
        }
        alloc_count_++;
        return alloc_result_;
    }

    uint64_t Allocate(uint64_t, uint64_t maxSize, uint64_t &offset) noexcept override
    {
        if (minmax_alloc_size_ == 0) {
            return 0;
        }
        offset = minmax_offset_;
        return minmax_alloc_size_;
    }

    void Release(uint64_t offset, uint64_t size) noexcept override
    {
        release_calls_.push_back({offset, size});
    }

    uint64_t Available() const noexcept override
    {
        return available_;
    }
    uint64_t Used() const noexcept override
    {
        return used_;
    }
    uint64_t Capacity() const noexcept override
    {
        return capacity_;
    }

    bool alloc_fail_ = false;
    bool alloc_result_ = true;
    size_t fail_on_count_ = SIZE_MAX;
    uint64_t alloc_offset_ = 0;
    std::vector<uint64_t> alloc_offsets_;
    size_t alloc_count_ = 0;
    uint64_t minmax_alloc_size_ = 0;
    uint64_t minmax_offset_ = 0;
    struct ReleaseCall {
        uint64_t offset;
        uint64_t size;
    };
    std::vector<ReleaseCall> release_calls_;
    uint64_t available_ = 0;
    uint64_t used_ = 0;

private:
    uint64_t capacity_;
};

}  // namespace

class UrmaSockShiftBufTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
    }
    void TearDown() override
    {
        errno = 0;
    }
};

TEST_F(UrmaSockShiftBufTest, Constructor_NormalizesShiftModCapacity)
{
    UrmaSockShiftBuf buf(new UrmaSockRingBuf(TEST_CAPACITY), 300);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(TEST_CHUNK, offset));
    uint64_t shift = 300 % TEST_CAPACITY;
    EXPECT_EQ(shift, offset);
}

TEST_F(UrmaSockShiftBufTest, Allocate_Success_OffsetShifted)
{
    UrmaSockShiftBuf buf(new UrmaSockRingBuf(TEST_CAPACITY), 0);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(TEST_CHUNK, offset));
    EXPECT_EQ(0U, offset);
}

TEST_F(UrmaSockShiftBufTest, Allocate_WrapAroundShifted)
{
    uint64_t shift = 200;
    UrmaSockShiftBuf buf(new UrmaSockRingBuf(TEST_CAPACITY), shift);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(TEST_CHUNK, offset));
    EXPECT_EQ(shift, offset);
    ASSERT_TRUE(buf.Allocate(TEST_CHUNK * 2, offset));
    uint64_t expected = (TEST_CHUNK + shift) % TEST_CAPACITY;
    EXPECT_EQ(expected, offset);
}

TEST_F(UrmaSockShiftBufTest, Allocate_Failure_ReturnsFalse)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_fail_ = true;
    UrmaSockShiftBuf buf(base, 0);
    uint64_t offset = 0;
    EXPECT_FALSE(buf.Allocate(TEST_CHUNK, offset));
}

TEST_F(UrmaSockShiftBufTest, AllocateMinMax_Success_OffsetShiftedAndSizeCorrect)
{
    UrmaSockShiftBuf buf(new UrmaSockRingBuf(TEST_CAPACITY), 50);
    uint64_t offset = 0;
    auto sz = buf.Allocate(TEST_CHUNK, TEST_CHUNK * 4, offset);
    EXPECT_EQ(TEST_CHUNK * 4, sz);
    EXPECT_EQ(50U, offset);
}

TEST_F(UrmaSockShiftBufTest, AllocateMinMax_Failure_ReturnsZero)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 0;
    UrmaSockShiftBuf buf(base, 0);
    uint64_t offset = 0;
    EXPECT_EQ(0U, buf.Allocate(TEST_CHUNK, TEST_CHUNK * 4, offset));
}

TEST_F(UrmaSockShiftBufTest, Release_NoWrap_ReverseShifted)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    UrmaSockShiftBuf buf(base, 50);
    buf.Release(150, 64);
    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(100U, base->release_calls_[0].offset);
    EXPECT_EQ(64U, base->release_calls_[0].size);
}

TEST_F(UrmaSockShiftBufTest, Release_WrapAround_ReverseShifted)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    UrmaSockShiftBuf buf(base, 200);
    buf.Release(30, 64);
    ASSERT_EQ(1U, base->release_calls_.size());
    uint64_t expected_base = (30 + TEST_CAPACITY - 200) % TEST_CAPACITY;
    EXPECT_EQ(expected_base, base->release_calls_[0].offset);
    EXPECT_EQ(64U, base->release_calls_[0].size);
}

TEST_F(UrmaSockShiftBufTest, Available_DelegatesToBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->available_ = 123;
    UrmaSockShiftBuf buf(base, 0);
    EXPECT_EQ(123U, buf.Available());
}

TEST_F(UrmaSockShiftBufTest, Used_DelegatesToBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->used_ = 456;
    UrmaSockShiftBuf buf(base, 0);
    EXPECT_EQ(456U, buf.Used());
}

TEST_F(UrmaSockShiftBufTest, Capacity_MatchesBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    UrmaSockShiftBuf buf(base, 0);
    EXPECT_EQ(TEST_CAPACITY, buf.Capacity());
}

class UrmaSockContinuousBufTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
    }
    void TearDown() override
    {
        errno = 0;
    }
};

TEST_F(UrmaSockContinuousBufTest, Allocate_NoWrap_OffsetUnchanged)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_offset_ = 64;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(128, offset));
    EXPECT_EQ(64U, offset);
}

TEST_F(UrmaSockContinuousBufTest, Allocate_Failure_ReturnsFalse)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_fail_ = true;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_FALSE(buf.Allocate(TEST_CHUNK, offset));
}

TEST_F(UrmaSockContinuousBufTest, Allocate_Wrap_ProducesContiguousAtOffsetZero)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_offsets_ = {200, 44};
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(100, offset));

    EXPECT_EQ(0U, offset);
    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(200U, base->release_calls_[0].offset);
    EXPECT_EQ(56U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, Allocate_Wrap_SecondAllocFails_Rollback)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_offsets_ = {200};
    base->fail_on_count_ = 1;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_FALSE(buf.Allocate(100, offset));

    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(200U, base->release_calls_[0].offset);
    EXPECT_EQ(100U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, Allocate_Wrap_WrongAppendOffset_Rollback)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_offsets_ = {200, 128};
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_FALSE(buf.Allocate(100, offset));

    ASSERT_EQ(2U, base->release_calls_.size());
    EXPECT_EQ(200U, base->release_calls_[0].offset);
    EXPECT_EQ(100U, base->release_calls_[0].size);
    EXPECT_EQ(128U, base->release_calls_[1].offset);
    EXPECT_EQ(56U, base->release_calls_[1].size);
}

TEST_F(UrmaSockContinuousBufTest, Allocate_Wrap_AtNonZeroBaseOffset_ProducesContiguous)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->alloc_offsets_ = {192, 64};
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    ASSERT_TRUE(buf.Allocate(128, offset));

    EXPECT_EQ(0U, offset);
    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(192U, base->release_calls_[0].offset);
    EXPECT_EQ(64U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_NoWrap_ReturnsUnchanged)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 32;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_EQ(128U, buf.Allocate(TEST_CHUNK, 256, offset));
    EXPECT_EQ(32U, offset);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_Failure_ReturnsZero)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 32;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_EQ(0U, buf.Allocate(TEST_CHUNK, 256, offset));
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_WrapLessThanMax_WrapSizeSufficient_ReturnsWrapSize)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 200;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    uint64_t wrap_size = 128 - (TEST_CAPACITY - 200);
    EXPECT_EQ(wrap_size, buf.Allocate(TEST_CHUNK, 256, offset));
    EXPECT_EQ(0U, offset);

    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(200U, base->release_calls_[0].offset);
    EXPECT_EQ(TEST_CAPACITY - 200, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_WrapLessThanMax_WrapSizeInsufficient_ReturnsZero)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 240;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_EQ(0U, buf.Allocate(128, 256, offset));

    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(240U, base->release_calls_[0].offset);
    EXPECT_EQ(128U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_WrapEqualsMax_SecondAllocSucceeds_ReturnsFull)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 200;
    base->alloc_offset_ = 72;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    EXPECT_EQ(128U, buf.Allocate(TEST_CHUNK, 128, offset));
    EXPECT_EQ(0U, offset);

    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(200U, base->release_calls_[0].offset);
    EXPECT_EQ(56U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_WrapEqualsMax_SecondAllocFails_ReturnsWrapSize)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 200;
    base->alloc_fail_ = true;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    uint64_t wrap_size = 128 - (TEST_CAPACITY - 200);
    EXPECT_EQ(wrap_size, buf.Allocate(TEST_CHUNK, 128, offset));
    EXPECT_EQ(0U, offset);
}

TEST_F(UrmaSockContinuousBufTest, AllocateMinMax_WrapEqualsMax_WrongAppendOffset_ReturnsWrapSize)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->minmax_alloc_size_ = 128;
    base->minmax_offset_ = 200;
    base->alloc_offset_ = 64;
    UrmaSockContinuousBuf buf(base);
    uint64_t offset = 0;
    uint64_t wrap_size = 128 - (TEST_CAPACITY - 200);
    EXPECT_EQ(wrap_size, buf.Allocate(TEST_CHUNK, 128, offset));
    EXPECT_EQ(0U, offset);

    ASSERT_GE(base->release_calls_.size(), 1U);
    EXPECT_EQ(64U, base->release_calls_.back().offset);
}

TEST_F(UrmaSockContinuousBufTest, Release_PassThrough)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    UrmaSockContinuousBuf buf(base);
    buf.Release(42, 123);
    ASSERT_EQ(1U, base->release_calls_.size());
    EXPECT_EQ(42U, base->release_calls_[0].offset);
    EXPECT_EQ(123U, base->release_calls_[0].size);
}

TEST_F(UrmaSockContinuousBufTest, Available_DelegatesToBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->available_ = 789;
    UrmaSockContinuousBuf buf(base);
    EXPECT_EQ(789U, buf.Available());
}

TEST_F(UrmaSockContinuousBufTest, Used_DelegatesToBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    base->used_ = 321;
    UrmaSockContinuousBuf buf(base);
    EXPECT_EQ(321U, buf.Used());
}

TEST_F(UrmaSockContinuousBufTest, Capacity_MatchesBase)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(TEST_CAPACITY);
    UrmaSockContinuousBuf buf(base);
    EXPECT_EQ(TEST_CAPACITY, buf.Capacity());
}

TEST_F(UrmaSockContinuousBufTest, Keep_LongAllocate)
{
    TestUrmaSockBuf *base = new TestUrmaSockBuf(128 * 1024);
    ASSERT_TRUE(base != nullptr);
    UrmaSockContinuousBuf buf(base);

    auto unit = 128U;
    std::list<std::pair<uint64_t, uint64_t>> chunks;
    for (auto d = 0; d < 100; d++) {
        auto n = (abs(rand()) % 8) + 4;
        uint64_t size = n * unit;
        uint64_t offset = 0;
        auto success = buf.Allocate(n * unit, offset);
        ASSERT_TRUE(success) << "times: base, depth=" << d << ", size=" << size;

        chunks.push_back(std::pair(size, offset));
    }

    for (auto i = 0U; i < 100000U; i++) {
        auto n = (abs(rand()) % 8) + 1;uint64_t size = n * unit;
        uint64_t offset = 0;
        auto success = buf.Allocate(n * unit, offset);
        ASSERT_TRUE(success) << "times: " << i << ", size=" << size;
        chunks.push_back(std::pair(size, offset));

        buf.Release(chunks.front().second, chunks.front().first);
        chunks.pop_front();
    }
}