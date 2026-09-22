/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "cli_message.h"

#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace Statistics;

namespace {

constexpr uint32_t TEST_MSG_SIZE_64 = 64;
constexpr uint32_t TEST_MSG_SIZE_32 = 32;
constexpr uint32_t TEST_MSG_SIZE_256 = 256;
constexpr uint32_t TEST_MSG_SIZE_128 = 128;

} // namespace

class CliMessageTest : public ::testing::Test {
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

// ==================== CLIMessage ====================

TEST_F(CliMessageTest, Message_DefaultConstruct_EmptyState)
{
    CLIMessage msg;
    EXPECT_EQ(msg.DataLen(), 0u);
    EXPECT_EQ(msg.GetBufLen(), 0u);
    EXPECT_EQ(msg.Data(), nullptr);
}

TEST_F(CliMessageTest, AllocateIfNeed_ZeroSize_ReturnsFalse)
{
    CLIMessage msg;
    EXPECT_FALSE(msg.AllocateIfNeed(0));
    EXPECT_EQ(msg.Data(), nullptr);
    EXPECT_EQ(msg.GetBufLen(), 0u);
}

TEST_F(CliMessageTest, AllocateIfNeed_FirstAlloc_Allocates)
{
    CLIMessage msg;
    EXPECT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    EXPECT_NE(msg.Data(), nullptr);
    EXPECT_EQ(msg.GetBufLen(), TEST_MSG_SIZE_64);
    EXPECT_EQ(msg.DataLen(), 0u);
}

TEST_F(CliMessageTest, AllocateIfNeed_WithinCapacity_ReusesBuffer)
{
    CLIMessage msg;
    ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    void *first = msg.Data();
    EXPECT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_32));
    EXPECT_EQ(msg.Data(), first);
    EXPECT_EQ(msg.GetBufLen(), TEST_MSG_SIZE_64);
}

TEST_F(CliMessageTest, AllocateIfNeed_Grow_Reallocates)
{
    CLIMessage msg;
    ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    EXPECT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_256));
    EXPECT_NE(msg.Data(), nullptr);
    EXPECT_EQ(msg.GetBufLen(), TEST_MSG_SIZE_256);
}

TEST_F(CliMessageTest, SetDataLen_ExceedsCapacity_ReturnsFalse)
{
    CLIMessage msg;
    ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    EXPECT_FALSE(msg.SetDataLen(TEST_MSG_SIZE_128));
    EXPECT_EQ(msg.DataLen(), 0u);
}

TEST_F(CliMessageTest, SetDataLen_WithinCapacity_ReturnsTrue)
{
    CLIMessage msg;
    ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    EXPECT_TRUE(msg.SetDataLen(TEST_MSG_SIZE_32));
    EXPECT_EQ(msg.DataLen(), TEST_MSG_SIZE_32);
}

TEST_F(CliMessageTest, ResetBuf_AllocatedBuffer_ZeroesContents)
{
    CLIMessage msg;
    ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
    auto *buf = static_cast<uint8_t *>(msg.Data());
    for (uint32_t i = 0; i < TEST_MSG_SIZE_64; ++i) {
        buf[i] = 0xAB;
    }
    msg.ResetBuf();
    for (uint32_t i = 0; i < TEST_MSG_SIZE_64; ++i) {
        EXPECT_EQ(buf[i], 0u);
    }
}

TEST_F(CliMessageTest, ResetBuf_NullBuffer_Noop)
{
    CLIMessage msg;
    EXPECT_NO_FATAL_FAILURE(msg.ResetBuf());
}

TEST_F(CliMessageTest, AllocateIfNeed_AllocTwice_DestroysOnce)
{
    {
        CLIMessage msg;
        ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_64));
        ASSERT_TRUE(msg.AllocateIfNeed(TEST_MSG_SIZE_256));
    }
    EXPECT_TRUE(true);
}

TEST_F(CliMessageTest, DelayHeader_DefaultConstruct_ZeroFields)
{
    CLIDelayHeader header;
    EXPECT_EQ(header.retCode, 0);
    EXPECT_EQ(header.tracePointDataSize, 0u);
}

// ==================== CLIControlHeader ====================

TEST_F(CliMessageTest, SetSwitch_ValidPositionEnable_BitSet)
{
    CLIControlHeader header{};
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
}

TEST_F(CliMessageTest, SetSwitch_ValidPositionDisable_BitCleared)
{
    CLIControlHeader header{};
    header.SetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE, true);
    header.SetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE, false);
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE));
}

TEST_F(CliMessageTest, SetSwitch_MultiplePositions_BitsIndependent)
{
    CLIControlHeader header{};
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    header.SetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE, true);
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE));
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE));
}

TEST_F(CliMessageTest, SetSwitch_InvalidPosition_Noop)
{
    CLIControlHeader header{};
    header.SetSwitch(CLISwitchPosition::INVALID, true);
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
    EXPECT_EQ(header.mSwitch, 0u);
}

TEST_F(CliMessageTest, GetSwitch_InvalidPosition_ReturnsFalse)
{
    CLIControlHeader header{};
    header.mSwitch = 0xFFFF;
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::INVALID));
}

TEST_F(CliMessageTest, Reset_ClearsFields)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::STAT;
    header.mErrorCode = CLIErrorCode::INTERNAL_ERROR;
    header.mDataSize = 128;
    header.mType = CLITypeParam::PROF_OP_QUERY;
    header.mSwitch = 0xFFFF;
    header.mValue = 3.14;
    header.Reset();
    EXPECT_EQ(header.mCmdId, CLICommand::INVALID);
    EXPECT_EQ(header.mErrorCode, CLIErrorCode::OK);
    EXPECT_EQ(header.mDataSize, 0u);
    EXPECT_EQ(header.mType, CLITypeParam::INVALID);
    EXPECT_EQ(header.mSwitch, 0u);
    EXPECT_EQ(header.mValue, 0.0);
}

// ==================== Split ====================

TEST_F(CliMessageTest, Split_EmptyString_ReturnsEmpty)
{
    std::vector<std::string> tokens = Split("", ',');
    EXPECT_TRUE(tokens.empty());
}

TEST_F(CliMessageTest, Split_NoDelimiter_SingleToken)
{
    std::vector<std::string> tokens = Split("abc", ',');
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "abc");
}

TEST_F(CliMessageTest, Split_MultipleTokens_ReturnsTokens)
{
    std::vector<std::string> tokens = Split("a,b,c", ',');
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "b");
    EXPECT_EQ(tokens[2], "c");
}

TEST_F(CliMessageTest, Split_ConsecutiveDelimiters_EmptyTokens)
{
    std::vector<std::string> tokens = Split("a,,b", ',');
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "");
    EXPECT_EQ(tokens[2], "b");
}

TEST_F(CliMessageTest, Split_LeadingTrailingDelimiters_EdgeTokens)
{
    std::vector<std::string> tokens = Split(",a,", ',');
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "");
    EXPECT_EQ(tokens[1], "a");
}
