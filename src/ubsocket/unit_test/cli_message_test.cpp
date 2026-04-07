/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for cli_message module
 */

#include "cli_message.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstring>

using namespace Statistics;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const uint32_t CLI_BUF_100 = 100U;
static const uint32_t CLI_BUF_50 = 50U;
static const uint32_t CLI_BUF_200 = 200U;
static const uint32_t CLI_BUF_256 = 256U;
static const uint32_t CLI_SOCKET_NUM_10 = 10U;
static const uint32_t CLI_CONN_NUM_100 = 100U;
static const uint32_t CLI_ACTIVE_50 = 50U;
static const uint32_t CLI_SOCKET_ID_1 = 1U;
static const uint32_t CLI_RECV_PKT_100 = 100U;
static const uint32_t CLI_SEND_PKT_200 = 200U;
static const uint32_t CLI_RECV_BYTES_1000 = 1000U;
static const uint32_t CLI_SEND_BYTES_2000 = 2000U;
static const uint32_t CLI_ERROR_PKT_5 = 5U;
static const uint32_t CLI_LOST_PKT_3 = 3U;
static const uint16_t CLI_SWITCH_0XFFFF = 0xFFFF;
static const uint8_t CLI_SWITCH_0X7 = 0x7U;
static const double CLI_PI = 3.14;
static const double CLI_PI_FULL = 3.14159;
} // namespace

// Test fixture for CLIMessage tests
class CLIMessageTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void CLIMessageTest::SetUp()
{
}

void CLIMessageTest::TearDown()
{
    GlobalMockObject::verify();
}


TEST_F(CLIMessageTest, AllocateIfNeed_ZeroSize)
{
    CLIMessage msg;
    bool result = msg.AllocateIfNeed(0);
    EXPECT_FALSE(result);
}

TEST_F(CLIMessageTest, AllocateIfNeed_NewSizeLessThanBufLen)
{
    CLIMessage msg;
    // First allocate a buffer
    bool result = msg.AllocateIfNeed(CLI_BUF_100);
    ASSERT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_100);

    result = msg.AllocateIfNeed(CLI_BUF_50);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_100);  // BufLen should not change
}

TEST_F(CLIMessageTest, AllocateIfNeed_NewSizeGreaterThanBufLen)
{
    CLIMessage msg;
    // First allocate a buffer
    bool result = msg.AllocateIfNeed(CLI_BUF_100);
    ASSERT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_100);

    // Request larger size - should reallocate
    result = msg.AllocateIfNeed(CLI_BUF_200);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_200);
}

TEST_F(CLIMessageTest, AllocateIfNeed_FirstAllocation)
{
    CLIMessage msg;
    // No buffer initially
    EXPECT_EQ(msg.GetBufLen(), 0);
    EXPECT_EQ(msg.Data(), nullptr);

    // First allocation
    bool result = msg.AllocateIfNeed(CLI_BUF_256);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_256);
    EXPECT_NE(msg.Data(), nullptr);
}

TEST_F(CLIMessageTest, AllocateIfNeed_DataLenSetter)
{
    CLIMessage msg;
    bool result = msg.AllocateIfNeed(CLI_BUF_100);
    ASSERT_TRUE(result);

    // Set data len within buf len
    result = msg.SetDataLen(CLI_BUF_50);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.DataLen(), CLI_BUF_50);

    // Set data len exceeding buf len
    result = msg.SetDataLen(CLI_BUF_200);
    EXPECT_FALSE(result);
}

TEST_F(CLIMessageTest, AllocateIfNeed_SetDataLenZero)
{
    CLIMessage msg;
    bool result = msg.AllocateIfNeed(CLI_BUF_100);
    ASSERT_TRUE(result);

    // Set data len to 0 should be valid
    result = msg.SetDataLen(0);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.DataLen(), 0);
}

// ============= CLIControlHeader Tests =============

class CLIControlHeaderTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void CLIControlHeaderTest::SetUp()
{
}

void CLIControlHeaderTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(CLIControlHeaderTest, SetSwitch_Enable)
{
    CLIControlHeader header;
    header.Reset();
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
}

TEST_F(CLIControlHeaderTest, SetSwitch_Disable)
{
    CLIControlHeader header;
    header.Reset();
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, false);
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
}

TEST_F(CLIControlHeaderTest, SetSwitch_InvalidPosition)
{
    CLIControlHeader header;
    header.Reset();
    // Should not crash with invalid position
    header.SetSwitch(CLISwitchPosition::INVALID, true);
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::INVALID));
}

TEST_F(CLIControlHeaderTest, GetSwitch_InvalidPosition)
{
    CLIControlHeader header;
    header.Reset();
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::INVALID));
}

TEST_F(CLIControlHeaderTest, Reset)
{
    CLIControlHeader header;
    header.mCmdId = CLICommand::TOPO;
    header.mErrorCode = CLIErrorCode::INTERNAL_ERROR;
    header.mDataSize = CLI_BUF_100;
    header.mType = CLITypeParam::TRACE_OP_QUERY;
    header.mSwitch = CLI_SWITCH_0XFFFF;
    header.mValue = CLI_PI;

    header.Reset();

    EXPECT_EQ(header.mCmdId, CLICommand::INVALID);
    EXPECT_EQ(header.mErrorCode, CLIErrorCode::OK);
    EXPECT_EQ(header.mDataSize, 0);
    EXPECT_EQ(header.mType, CLITypeParam::INVALID);
    EXPECT_EQ(header.mSwitch, 0);
    EXPECT_EQ(header.mValue, 0.0);
}

TEST_F(CLIControlHeaderTest, MultipleSwitches)
{
    CLIControlHeader header;
    header.Reset();

    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    header.SetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE, true);
    header.SetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE, false);

    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE));
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE));
}

TEST_F(CLIControlHeaderTest, SwitchBitPattern)
{
    CLIControlHeader header;
    header.Reset();

    // Set all three switches
    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);          // bit 0
    header.SetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE, true); // bit 1
    header.SetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE, true);       // bit 2

    // Check the switch value directly
    EXPECT_EQ(header.mSwitch, CLI_SWITCH_0X7);  // 0b111 = 7
}

// ============= CLIDelayHeader Tests =============

class CLIDelayHeaderTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void CLIDelayHeaderTest::SetUp()
{
}

void CLIDelayHeaderTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(CLIDelayHeaderTest, Constructor)
{
    CLIDelayHeader header;
    EXPECT_EQ(header.retCode, 0);
    EXPECT_EQ(header.tracePointNum, 0);
}

TEST_F(CLIMessageTest, AllocateIfNeed_ReallocOnExistingBuffer)
{
    CLIMessage msg;
    // First allocation
    bool result = msg.AllocateIfNeed(CLI_BUF_100);
    ASSERT_TRUE(result);

    void* oldBuf = msg.Data();
    ASSERT_NE(oldBuf, nullptr);

    // Request larger size - should reallocate
    result = msg.AllocateIfNeed(CLI_BUF_200);
    EXPECT_TRUE(result);
    EXPECT_EQ(msg.GetBufLen(), CLI_BUF_200);
}

TEST_F(CLIMessageTest, Destructor_FreesBuffer)
{
    CLIMessage* msg = new CLIMessage();
    msg->AllocateIfNeed(CLI_BUF_100);
    // Destructor should free the buffer
    delete msg;
    // No crash = success
}

TEST_F(CLIMessageTest, Data_ReturnsNullInitially)
{
    CLIMessage msg;
    EXPECT_EQ(msg.Data(), nullptr);
}

TEST_F(CLIMessageTest, DataLen_InitiallyZero)
{
    CLIMessage msg;
    EXPECT_EQ(msg.DataLen(), 0u);
}

TEST_F(CLIMessageTest, SetDataLen_ValidValue)
{
    CLIMessage msg;
    msg.AllocateIfNeed(CLI_BUF_100);
    EXPECT_TRUE(msg.SetDataLen(CLI_BUF_50));
    EXPECT_EQ(msg.DataLen(), CLI_BUF_50);
}

TEST_F(CLIMessageTest, SetDataLen_ExceedsBufLen)
{
    CLIMessage msg;
    msg.AllocateIfNeed(CLI_BUF_100);
    EXPECT_FALSE(msg.SetDataLen(CLI_BUF_200));
}

// Note: Malloc failure tests removed because mocking malloc causes
// issues with test framework infrastructure (gtest uses malloc internally)

// ============= CLIControlHeader Additional Tests =============

TEST_F(CLIControlHeaderTest, SetSwitch_EnableDisable)
{
    CLIControlHeader header;
    header.Reset();

    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, true);
    EXPECT_TRUE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));

    header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, false);
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
}

TEST_F(CLIControlHeaderTest, GetSwitch_AllPositions)
{
    CLIControlHeader header;
    header.Reset();

    // All should be false initially
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_ENABLE));
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE));
    EXPECT_FALSE(header.GetSwitch(CLISwitchPosition::IS_TRACE_LOG_ENABLE));
}

TEST_F(CLIControlHeaderTest, MemberValues)
{
    CLIControlHeader header;
    header.Reset();

    header.mCmdId = CLICommand::TOPO;
    header.mErrorCode = CLIErrorCode::OK;
    header.mDataSize = CLI_BUF_100;
    header.mType = CLITypeParam::TRACE_OP_QUERY;
    header.mValue = CLI_PI_FULL;

    EXPECT_EQ(header.mCmdId, CLICommand::TOPO);
    EXPECT_EQ(header.mErrorCode, CLIErrorCode::OK);
    EXPECT_EQ(header.mDataSize, CLI_BUF_100);
    EXPECT_EQ(header.mType, CLITypeParam::TRACE_OP_QUERY);
    EXPECT_DOUBLE_EQ(header.mValue, CLI_PI_FULL);
}

// ============= CLIDataHeader Tests =============

class CLIDataHeaderTest : public testing::Test {
public:
    virtual void SetUp() {}
    virtual void TearDown() { GlobalMockObject::verify(); }
};

TEST_F(CLIDataHeaderTest, MemberAccess)
{
    CLIDataHeader dataHeader;
    dataHeader.socketNum = CLI_SOCKET_NUM_10;
    dataHeader.connNum = CLI_CONN_NUM_100;
    dataHeader.activeConn = CLI_ACTIVE_50;

    EXPECT_EQ(dataHeader.socketNum, CLI_SOCKET_NUM_10);
    EXPECT_EQ(dataHeader.connNum, CLI_CONN_NUM_100);
    EXPECT_EQ(dataHeader.activeConn, CLI_ACTIVE_50);
}

// ============= CLISocketData Tests =============

class CLISocketDataTest : public testing::Test {
public:
    virtual void SetUp() {}
    virtual void TearDown() { GlobalMockObject::verify(); }
};

TEST_F(CLISocketDataTest, MemberAccess)
{
    CLISocketData socketData;
    socketData.socketId = CLI_SOCKET_ID_1;
    socketData.recvPackets = CLI_RECV_PKT_100;
    socketData.sendPackets = CLI_SEND_PKT_200;
    socketData.recvBytes = CLI_RECV_BYTES_1000;
    socketData.sendBytes = CLI_SEND_BYTES_2000;
    socketData.errorPackets = CLI_ERROR_PKT_5;
    socketData.lostPackets = CLI_LOST_PKT_3;

    EXPECT_EQ(socketData.socketId, CLI_SOCKET_ID_1);
    EXPECT_EQ(socketData.recvPackets, CLI_RECV_PKT_100);
    EXPECT_EQ(socketData.sendPackets, CLI_SEND_PKT_200);
    EXPECT_EQ(socketData.recvBytes, CLI_RECV_BYTES_1000);
    EXPECT_EQ(socketData.sendBytes, CLI_SEND_BYTES_2000);
    EXPECT_EQ(socketData.errorPackets, CLI_ERROR_PKT_5);
    EXPECT_EQ(socketData.lostPackets, CLI_LOST_PKT_3);
}