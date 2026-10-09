/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <sys/uio.h>

#include "common/ubsocket_ref.h"
#include "core/ubsocket_buf_converter.h"

using namespace ock::ubs;

// ==================== Test stubs (MemCopy not needed for these tests) ====================

class TestIovConverter : public IovConverter {
public:
    using IovConverter::IovConverter;
    bool MemCopy(uint32_t /*len*/, uintptr_t /*buf*/) override
    {
        return true;
    }
};

class TestBufferConverter : public BufferConverter {
public:
    using BufferConverter::BufferConverter;
    bool MemCopy(uint32_t /*len*/, uintptr_t /*buf*/) override
    {
        return true;
    }
};

// ==================== IovConverter Tests ====================

class IovConverterTest : public ::testing::Test {
};

TEST_F(IovConverterTest, IndexMove_SingleIov_IncrementsOffset)
{
    struct iovec iov[1] = {{.iov_base = nullptr, .iov_len = 100}};
    TestIovConverter cvt(iov, 1);

    uint32_t moved = cvt.IndexMove(30);
    EXPECT_EQ(moved, 30U);
}

TEST_F(IovConverterTest, IndexMove_SingleIov_ExactBoundary)
{
    struct iovec iov[1] = {{.iov_base = nullptr, .iov_len = 50}};
    TestIovConverter cvt(iov, 1);

    uint32_t moved = cvt.IndexMove(50);
    // offset reaches iov_len, should advance to next iov
    EXPECT_GT(moved, 0U);
}

TEST_F(IovConverterTest, IndexMove_MultiIov_AcrossSegments)
{
    char buf1[100], buf2[100];
    struct iovec iov[2] = {
        {.iov_base = buf1, .iov_len = 30},
        {.iov_base = buf2, .iov_len = 70},
    };
    TestIovConverter cvt(iov, 2);

    // first move: 30 bytes → should consume first iov and advance
    uint32_t m1 = cvt.IndexMove(30);
    EXPECT_EQ(m1, 30U);

    // second move should be on second iov
    uint32_t m2 = cvt.IndexMove(20);
    EXPECT_EQ(m2, 20U);
}

TEST_F(IovConverterTest, IndexMove_SkipsZeroLengthIovs)
{
    char buf[50];
    struct iovec iov[4] = {
        {.iov_base = nullptr, .iov_len = 0},
        {.iov_base = nullptr, .iov_len = 0},
        {.iov_base = buf, .iov_len = 50},
        {.iov_base = nullptr, .iov_len = 0},
    };
    TestIovConverter cvt(iov, 4);

    // First move: len=10 triggers advance across zero-length iovs →
    // consumes entire 50-byte iov in one go, then skips trailing zero iov
    uint32_t moved = cvt.IndexMove(10);
    EXPECT_EQ(moved, 50U);

    // No more iovs remain
    uint32_t moved2 = cvt.IndexMove(10);
    EXPECT_EQ(moved2, 0U);
}

TEST_F(IovConverterTest, IndexMove_MoveExceedingLastIov)
{
    char buf[20];
    struct iovec iov[1] = {{.iov_base = buf, .iov_len = 20}};
    TestIovConverter cvt(iov, 1);

    // first consume entire iov
    uint32_t m1 = cvt.IndexMove(30);
    EXPECT_EQ(m1, 20U);

    // subsequent move: no more iovs
    uint32_t m2 = cvt.IndexMove(10);
    EXPECT_EQ(m2, 0U);
}

TEST_F(IovConverterTest, IndexMove_AllZeroLengthIovs)
{
    struct iovec iov[3] = {
        {.iov_base = nullptr, .iov_len = 0},
        {.iov_base = nullptr, .iov_len = 0},
        {.iov_base = nullptr, .iov_len = 0},
    };
    TestIovConverter cvt(iov, 3);

    uint32_t moved = cvt.IndexMove(10);
    EXPECT_EQ(moved, 0U);
}

TEST_F(IovConverterTest, IndexMove_EmptyIovArray)
{
    TestIovConverter cvt(nullptr, 0);

    uint32_t moved = cvt.IndexMove(10);
    EXPECT_EQ(moved, 0U);
}

TEST_F(IovConverterTest, Reset_AfterMove_ResetsPosition)
{
    char buf[100];
    struct iovec iov[1] = {{.iov_base = buf, .iov_len = 100}};
    TestIovConverter cvt(iov, 1);

    cvt.IndexMove(40);
    cvt.Reset();

    // after reset, should behave like fresh
    uint32_t moved = cvt.IndexMove(10);
    EXPECT_EQ(moved, 10U);
}

TEST_F(IovConverterTest, Reset_ThenMoveAcross)
{
    char buf1[30], buf2[70];
    struct iovec iov[2] = {
        {.iov_base = buf1, .iov_len = 30},
        {.iov_base = buf2, .iov_len = 70},
    };
    TestIovConverter cvt(iov, 2);

    // consume first
    cvt.IndexMove(30);
    cvt.Reset();

    // fresh start: should begin at first iov again
    uint32_t m1 = cvt.IndexMove(30);
    EXPECT_EQ(m1, 30U);
    uint32_t m2 = cvt.IndexMove(10);
    EXPECT_EQ(m2, 10U);
}

// ==================== BufferConverter Tests ====================

class BufferConverterTest : public ::testing::Test {
};

TEST_F(BufferConverterTest, IndexMove_WithinBounds)
{
    char buf[100] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    uint32_t moved = cvt.IndexMove(30);
    EXPECT_EQ(moved, 30U);
}

TEST_F(BufferConverterTest, IndexMove_ExactEnd)
{
    char buf[50] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    // Note: when m_offset + len >= m_size, code sets m_offset=m_size first
    // then returns m_size - m_offset (= 0). The return value is always 0
    // when the end condition is hit, not the remaining bytes.
    uint32_t moved = cvt.IndexMove(50);
    EXPECT_EQ(moved, 0U);
}

TEST_F(BufferConverterTest, IndexMove_PastEnd_Clamped)
{
    char buf[50] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    uint32_t moved = cvt.IndexMove(100);
    EXPECT_EQ(moved, 0U);
}

TEST_F(BufferConverterTest, IndexMove_AfterClamp_ReturnsZero)
{
    char buf[50] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    cvt.IndexMove(100); // clamped to end
    uint32_t moved = cvt.IndexMove(10);
    EXPECT_EQ(moved, 0U);
}

TEST_F(BufferConverterTest, IndexMove_StepwiseWithinBounds)
{
    char buf[100] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    // all moves stay within bounds, so each returns the full len
    EXPECT_EQ(cvt.IndexMove(30), 30U);
    EXPECT_EQ(cvt.IndexMove(40), 40U);
    // at offset 70, remaining is 30; len=20 fits
    EXPECT_EQ(cvt.IndexMove(20), 20U);
}

TEST_F(BufferConverterTest, Reset_AfterMove)
{
    char buf[100] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    cvt.IndexMove(60);
    cvt.Reset();

    uint32_t moved = cvt.IndexMove(20);
    EXPECT_EQ(moved, 20U);
}

TEST_F(BufferConverterTest, Reset_MultipleCycles)
{
    char buf[100] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(cvt.IndexMove(40), 40U);
        cvt.Reset();
    }
}

TEST_F(BufferConverterTest, IndexMove_ZeroLen)
{
    char buf[100] = {};
    TestBufferConverter cvt(buf, sizeof(buf));

    uint32_t moved = cvt.IndexMove(0);
    EXPECT_EQ(moved, 0U);
}
