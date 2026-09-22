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

#include "umq_tx_helper.h"

#include <gtest/gtest.h>
#include <cstring>
#include <mockcpp/mockcpp.hpp>

#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const uint32_t SIZE_64K_PLUS_1 = 65537;
static const uintptr_t DUMMY_INPUT_ADDR = 0x2000;
} // namespace

class UmqTxHelperMultiLevelTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        memset(&m_qbuf, 0, sizeof(m_qbuf));
        memset(m_blockStorage, 0, sizeof(m_blockStorage));
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

    // Construct a Block in m_blockStorage via placement new with given data and cap.
    // Caller may further override fields (e.g., m_blk->cap) after construction.
    Block *ConstructBlock(char *data, uint32_t cap)
    {
        m_blk = new (m_blockStorage) Block(data, cap);
        return m_blk;
    }

    umq_buf_t m_qbuf;
    alignas(alignof(Block)) char m_blockStorage[sizeof(Block)];
    Block *m_blk{nullptr};
};

// ==================== DataToBlock ====================

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_NormalValidBlock_ReturnsBlock)
{
    char dummyData[16] = {0};
    Block *blk = ConstructBlock(&dummyData[0], SIZE_4K);
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->cap, SIZE_4K);
    EXPECT_EQ(result->data, &dummyData[0]);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_QbufNull_ReturnsNull)
{
    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(static_cast<umq_buf_t *>(nullptr)));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_EQ(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_BufDataNull_ReturnsNull)
{
    m_qbuf.buf_data = nullptr;

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_EQ(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_BlockDataNull_ReturnsBlock)
{
    Block *blk = ConstructBlock(nullptr, SIZE_4K);
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_BlockCapZero_ReturnsBlock)
{
    char dummyData[16] = {0};
    Block *blk = ConstructBlock(&dummyData[0], 0);
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_BlockCapTooLarge_ReturnsBlock)
{
    char dummyData[16] = {0};
    Block *blk = ConstructBlock(&dummyData[0], SIZE_4K);
    blk->cap = SIZE_64K_PLUS_1;
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_BlockCapBoundary65536_ReturnsBlock)
{
    char dummyData[16] = {0};
    Block *blk = ConstructBlock(&dummyData[0], SIZE_64K);
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->cap, SIZE_64K);
    GlobalMockObject::verify();
}

TEST_F(UmqTxHelperMultiLevelTest, DataToBlock_LargeScBlockValid_ReturnsBlock)
{
    char dummyData[16] = {0};
    Block *blk = ConstructBlock(&dummyData[0], SIZE_64K);
    blk->flags |= IOBUF_BLOCK_FLAGS_UB_HUGE_POOL;
    m_qbuf.buf_data = reinterpret_cast<char *>(blk);

    MOCKER_CPP(::umq_data_to_head).stubs().will(returnValue(&m_qbuf));

    Block *result = UmqTxHelper::DataToBlock(reinterpret_cast<void *>(DUMMY_INPUT_ADDR));
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->cap, SIZE_64K);
    EXPECT_NE(result->flags & IOBUF_BLOCK_FLAGS_UB_HUGE_POOL, 0);
    GlobalMockObject::verify();
}
