/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for buffer_util.h
 */

#include "buffer_util.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cstring>

namespace {
static const int IOV_CNT_1 = 1;
static const int IOV_CNT_2 = 2;
static const int IOV_CNT_3 = 3;
static const int IOV_CNT_4 = 4;
static const uint32_t CUT_LEN_5 = 5U;
static const uint32_t CUT_LEN_10 = 10U;
static const uint32_t CUT_LEN_15 = 15U;
static const uint32_t CUT_LEN_20 = 20U;
static const uint32_t CUT_LEN_25 = 25U;
static const uint32_t CUT_LEN_30 = 30U;
static const uint32_t IOV_LEN_10 = 10U;
static const uint32_t IOV_LEN_20 = 20U;
static const uint32_t IOV_LEN_30 = 30U;
static const uint32_t IOV_LEN_0 = 0U;
static const uint32_t EXPECTED_MOVED_5 = 5U;
static const uint32_t EXPECTED_MOVED_10 = 10U;
static const uint32_t EXPECTED_MOVED_15 = 15U;
static const uint32_t EXPECTED_MOVED_20 = 20U;
static const uint32_t EXPECTED_MOVED_0 = 0U;
static const size_t BUF_DATA_SIZE_100 = 100;

// 新增常量替换魔鬼数字索引
static const size_t INDEX_THIRD = 2;
}

class BufferUtilTest : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

// Test Cut with single iov element
TEST_F(BufferUtilTest, Cut_SingleIovElement_PartialCut)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_1);

    uint32_t moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);
}

TEST_F(BufferUtilTest, Cut_SingleIovElement_FullCut)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_10;

    IovConverter converter(iov, IOV_CNT_1);

    uint32_t moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);

    // After full cut, next cut should return 0
    moved = converter.Cut(CUT_LEN_5);
    EXPECT_EQ(moved, EXPECTED_MOVED_0);
}

TEST_F(BufferUtilTest, Cut_SingleIovElement_OverCut)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_10;

    IovConverter converter(iov, IOV_CNT_1);

    // Cut more than available length
    uint32_t moved = converter.Cut(CUT_LEN_20);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);
}

// Test Cut with multiple iov elements
TEST_F(BufferUtilTest, Cut_MultipleIovElements_CrossBoundary)
{
    struct iovec iov[IOV_CNT_2];
    char buffer1[BUF_DATA_SIZE_100] = {0};
    char buffer2[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer1;
    iov[0].iov_len = IOV_LEN_10;
    iov[1].iov_base = buffer2;
    iov[1].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_2);

    // First cut partial in first element
    uint32_t moved = converter.Cut(CUT_LEN_5);
    EXPECT_EQ(moved, EXPECTED_MOVED_5);

    // Second cut crosses to second element
    moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_5);

    // Continue cutting in second element
    moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);
}

// Test Cut with zero-length iov elements
TEST_F(BufferUtilTest, Cut_ZeroLengthIov_Skip)
{
    struct iovec iov[IOV_CNT_3];
    char buffer1[BUF_DATA_SIZE_100] = {0};
    char buffer2[BUF_DATA_SIZE_100] = {0};
    char buffer3[BUF_DATA_SIZE_100] = {0};

    iov[0].iov_base = buffer1;
    iov[0].iov_len = IOV_LEN_10;
    iov[1].iov_base = buffer2;
    iov[1].iov_len = IOV_LEN_0;  // Zero length
    iov[INDEX_THIRD].iov_base = buffer3;
    iov[INDEX_THIRD].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_3);

    // First cut
    uint32_t moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);

    // Cut that crosses zero-length element
    moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);
}

// Test Cut when all iov exhausted
TEST_F(BufferUtilTest, Cut_AllIovExhausted_ReturnZero)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_10;

    IovConverter converter(iov, IOV_CNT_1);

    // Exhaust all
    uint32_t moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);

    // Further cuts return 0
    moved = converter.Cut(CUT_LEN_5);
    EXPECT_EQ(moved, EXPECTED_MOVED_0);
}

// Test Reset
TEST_F(BufferUtilTest, Reset_AfterCut_RestartFromBeginning)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_1);

    // Cut some
    uint32_t moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);

    // Reset
    converter.Reset();

    // Can cut again from beginning
    moved = converter.Cut(CUT_LEN_10);
    EXPECT_EQ(moved, EXPECTED_MOVED_10);
}

// Test CutLast with single iov
TEST_F(BufferUtilTest, CutLast_SingleIov_PartialCut)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_30;

    IovConverter converter(iov, IOV_CNT_1);
    umq_buf_t buf = {};

    bool isLast = converter.CutLast(CUT_LEN_10, &buf);
    EXPECT_FALSE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_10);
    EXPECT_NE(buf.buf_data, nullptr);
}

TEST_F(BufferUtilTest, CutLast_SingleIov_LastElement)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_10;

    IovConverter converter(iov, IOV_CNT_1);
    umq_buf_t buf = {};

    bool isLast = converter.CutLast(CUT_LEN_10, &buf);
    EXPECT_TRUE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_10);
}

// Test CutLast with multiple iov
TEST_F(BufferUtilTest, CutLast_MultipleIov_CrossBoundary)
{
    struct iovec iov[IOV_CNT_2];
    char buffer1[BUF_DATA_SIZE_100] = {0};
    char buffer2[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer1;
    iov[0].iov_len = IOV_LEN_10;
    iov[1].iov_base = buffer2;
    iov[1].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_2);
    umq_buf_t buf = {};

    // First cut stays in first element
    bool isLast = converter.CutLast(CUT_LEN_5, &buf);
    EXPECT_FALSE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_5);

    // Cut crosses to second element
    isLast = converter.CutLast(CUT_LEN_10, &buf);
    EXPECT_FALSE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_5);

    // Final cut in second element
    isLast = converter.CutLast(CUT_LEN_20, &buf);
    EXPECT_TRUE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_20);
}

// Test CutLast with zero-length iov
TEST_F(BufferUtilTest, CutLast_ZeroLengthIov_Skip)
{
    struct iovec iov[IOV_CNT_3];
    char buffer1[BUF_DATA_SIZE_100] = {0};
    char buffer2[BUF_DATA_SIZE_100] = {0};
    char buffer3[BUF_DATA_SIZE_100] = {0};

    iov[0].iov_base = buffer1;
    iov[0].iov_len = IOV_LEN_10;
    iov[1].iov_base = buffer2;
    iov[1].iov_len = IOV_LEN_0;
    iov[INDEX_THIRD].iov_base = buffer3;
    iov[INDEX_THIRD].iov_len = IOV_LEN_20;

    IovConverter converter(iov, IOV_CNT_3);
    umq_buf_t buf = {};

    bool isLast = converter.CutLast(CUT_LEN_10, &buf);
    EXPECT_FALSE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_10);

    // Next cut skips zero-length and goes to third element
    isLast = converter.CutLast(CUT_LEN_20, &buf);
    EXPECT_TRUE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_20);
}

// Test CutLast when exhausted - behavior matches actual implementation
TEST_F(BufferUtilTest, CutLast_Exhausted_ReturnsTrue)
{
    struct iovec iov[IOV_CNT_1];
    char buffer[BUF_DATA_SIZE_100] = {0};
    iov[0].iov_base = buffer;
    iov[0].iov_len = IOV_LEN_10;

    IovConverter converter(iov, IOV_CNT_1);
    umq_buf_t buf = {};

    // Exhaust all - returns true when iov_idx >= iovcnt
    bool isLast = converter.CutLast(CUT_LEN_10, &buf);
    EXPECT_TRUE(isLast);
    EXPECT_EQ(buf.data_size, EXPECTED_MOVED_10);

    // Further cuts - since iov_idx >= iovcnt, outer if fails
    // but return condition still returns true
    umq_buf_t buf2 = {};
    isLast = converter.CutLast(CUT_LEN_5, &buf2);
    // Behavior: when exhausted, returns true (iov_idx >= iovcnt)
    // buf2 is not modified since outer if condition fails
    EXPECT_TRUE(isLast);
}