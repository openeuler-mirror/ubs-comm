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

#include "umq_backend.h"

#include <gtest/gtest.h>
#include <cstring>
#include <mockcpp/mockcpp.hpp>

#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const uint32_t TEST_ALLOC_SIZE = 4096;
static const uint32_t TEST_BUF_DATA_SIZE = 8192;

static umq_alloc_option_t g_capturedOption{};
static int g_bufAllocCallCount = 0;
static int g_bufFreeCallCount = 0;
static int g_dataToHeadCallCount = 0;
static umq_buf_t *g_returnBuf1 = nullptr;
static umq_buf_t *g_returnBuf2 = nullptr;

static umq_buf_t *MockBufAllocCapture(uint32_t req, uint32_t num, uint64_t handle, umq_alloc_option_t *opt)
{
    g_bufAllocCallCount++;
    if (opt != nullptr) {
        g_capturedOption = *opt;
    } else {
        g_capturedOption = {};
    }
    return g_returnBuf1;
}

static void MockBufFreeCount(umq_buf_t *qbuf)
{
    g_bufFreeCallCount++;
}

static umq_buf_t *MockDataToHeadNormal(void *data)
{
    g_dataToHeadCallCount++;
    return g_returnBuf1;
}

static umq_buf_t *MockDataToHeadIndirection(void *data)
{
    g_dataToHeadCallCount++;
    if (g_dataToHeadCallCount == 1) {
        return g_returnBuf1;
    }
    return g_returnBuf2;
}
} // namespace

class UmqZcopyAllocatorMultiLevelTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_capturedOption = {};
        g_bufAllocCallCount = 0;
        g_bufFreeCallCount = 0;
        g_dataToHeadCallCount = 0;
        g_returnBuf1 = nullptr;
        g_returnBuf2 = nullptr;
        memset(&m_qbuf, 0, sizeof(m_qbuf));
        memset(&m_qbuf2, 0, sizeof(m_qbuf2));
        memset(m_blockStorage, 0, sizeof(m_blockStorage));
        memset(m_bufDataStorage, 0, sizeof(m_bufDataStorage));
        memset(m_hugeDataStorage, 0, sizeof(m_hugeDataStorage));
        memset(m_dataBuf, 0, sizeof(m_dataBuf));
        memset(m_otherDataBuf, 0, sizeof(m_otherDataBuf));
        m_blk = nullptr;
    }

    void TearDown() override
    {
        if (m_blk != nullptr) {
            m_blk->~Block();
            m_blk = nullptr;
        }
        errno = 0;
        GlobalMockObject::verify();
    }

    Block *ConstructBlock(char *data, uint32_t cap)
    {
        m_blk = new (m_blockStorage) Block(data, cap);
        return m_blk;
    }

    UmqZeroCopyAllocator m_allocator;
    umq_buf_t m_qbuf;
    umq_buf_t m_qbuf2;
    alignas(alignof(Block)) char m_blockStorage[sizeof(Block)];
    Block *m_blk{nullptr};
    char m_bufDataStorage[TEST_BUF_DATA_SIZE];
    alignas(alignof(umq_buf_t *)) char m_hugeDataStorage[TEST_BUF_DATA_SIZE];
    char m_dataBuf[1024];
    char m_otherDataBuf[1024];
};

// ==================== allocate ====================

TEST_F(UmqZcopyAllocatorMultiLevelTest, Allocate_NoOption_ReturnsBufData)
{
    m_qbuf.buf_data = m_bufDataStorage;
    m_qbuf.data_size = TEST_ALLOC_SIZE;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockBufAllocCapture));

    void *result = m_allocator.allocate(TEST_ALLOC_SIZE, nullptr);
    EXPECT_EQ(result, static_cast<void *>(m_qbuf.buf_data));
    EXPECT_EQ(g_bufAllocCallCount, 1);
    EXPECT_EQ(g_capturedOption.headroom_size, 0u);
    EXPECT_NE(g_capturedOption.flag & UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, 0u);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Allocate_PoolTypeNormal_ReturnsBufData)
{
    ubs_iobuf_alloc_option_t option{};
    option.flag = UBS_IOBUF_ALLOC_FLAG_POOL_TYPE;
    option.pool_type = UBS_IOBUF_POOL_NORMAL;

    m_qbuf.buf_data = m_bufDataStorage;
    m_qbuf.data_size = TEST_ALLOC_SIZE;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockBufAllocCapture));

    void *result = m_allocator.allocate(TEST_ALLOC_SIZE, &option);
    EXPECT_EQ(result, static_cast<void *>(m_qbuf.buf_data));
    EXPECT_EQ(g_capturedOption.pool_type, UMQ_ALLOC_POOL_NORMAL);
    EXPECT_NE(g_capturedOption.flag & UMQ_ALLOC_FLAG_POOL_TYPE, 0u);
    EXPECT_NE(g_capturedOption.flag & UMQ_ALLOC_FLAG_HEAD_ROOM_SIZE, 0u);
    EXPECT_EQ(g_capturedOption.headroom_size, 0u);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Allocate_PoolTypeTiny_ReturnsBufData)
{
    ubs_iobuf_alloc_option_t option{};
    option.flag = UBS_IOBUF_ALLOC_FLAG_POOL_TYPE;
    option.pool_type = UBS_IOBUF_POOL_TINY;

    m_qbuf.buf_data = m_bufDataStorage;
    m_qbuf.data_size = TEST_ALLOC_SIZE;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockBufAllocCapture));

    void *result = m_allocator.allocate(TEST_ALLOC_SIZE, &option);
    EXPECT_EQ(result, static_cast<void *>(m_qbuf.buf_data));
    EXPECT_EQ(g_capturedOption.pool_type, UMQ_ALLOC_POOL_TINY);
    EXPECT_EQ(g_capturedOption.headroom_size, 0u);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Allocate_PoolTypeEscape_ReturnsBufData)
{
    ubs_iobuf_alloc_option_t option{};
    option.flag = UBS_IOBUF_ALLOC_FLAG_POOL_TYPE;
    option.pool_type = UBS_IOBUF_POOL_ESCAPE;

    m_qbuf.buf_data = m_bufDataStorage;
    m_qbuf.data_size = TEST_ALLOC_SIZE;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockBufAllocCapture));

    void *result = m_allocator.allocate(TEST_ALLOC_SIZE, &option);
    EXPECT_EQ(result, static_cast<void *>(m_qbuf.buf_data));
    EXPECT_EQ(g_capturedOption.pool_type, UMQ_ALLOC_POOL_ESCAPE);
    EXPECT_EQ(g_capturedOption.headroom_size, 0u);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Allocate_AllocFails_ReturnsNull)
{
    g_returnBuf1 = nullptr;

    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockBufAllocCapture));

    void *result = m_allocator.allocate(TEST_ALLOC_SIZE, nullptr);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(g_bufAllocCallCount, 1);

    GlobalMockObject::verify();
}

// ==================== deallocate ====================

TEST_F(UmqZcopyAllocatorMultiLevelTest, Deallocate_NullPtr_NoOp)
{
    m_allocator.deallocate(nullptr);
    EXPECT_EQ(g_bufFreeCallCount, 0);
    EXPECT_EQ(g_dataToHeadCallCount, 0);
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Deallocate_ValidBlock_NormalPath_CallsBufFree)
{
    Block *blk = ConstructBlock(m_dataBuf, sizeof(m_dataBuf));

    m_qbuf.buf_data = m_dataBuf;
    m_qbuf.qbuf_next = nullptr;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(MockDataToHeadNormal));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFreeCount));

    m_allocator.deallocate(static_cast<void *>(blk));
    EXPECT_EQ(g_dataToHeadCallCount, 1);
    EXPECT_EQ(g_bufFreeCallCount, 1);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Deallocate_BlockDataNull_EscapePath_CallsBufFree)
{
    Block *blk = ConstructBlock(nullptr, 0);

    m_qbuf.buf_data = reinterpret_cast<char *>(blk);
    m_qbuf.qbuf_next = nullptr;
    g_returnBuf1 = &m_qbuf;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(MockDataToHeadNormal));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFreeCount));

    m_allocator.deallocate(static_cast<void *>(blk));
    EXPECT_EQ(g_dataToHeadCallCount, 1);
    EXPECT_EQ(g_bufFreeCallCount, 1);

    GlobalMockObject::verify();
}

TEST_F(UmqZcopyAllocatorMultiLevelTest, Deallocate_BufDataNotEqualDataPtr_Indirection_CallsDataToHeadTwice)
{
    Block *blk = ConstructBlock(m_dataBuf, sizeof(m_dataBuf));

    m_qbuf.buf_data = m_otherDataBuf;
    m_qbuf.qbuf_next = nullptr;
    m_qbuf2.buf_data = m_dataBuf;
    m_qbuf2.qbuf_next = nullptr;
    g_returnBuf1 = &m_qbuf;
    g_returnBuf2 = &m_qbuf2;

    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(MockDataToHeadIndirection));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockBufFreeCount));

    m_allocator.deallocate(static_cast<void *>(blk));
    EXPECT_EQ(g_dataToHeadCallCount, 2);
    EXPECT_EQ(g_bufFreeCallCount, 1);

    GlobalMockObject::verify();
}
