/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include "core/ubsocket_core_types.h"

using namespace ock::ubs;

class CoreTypesSocketStateTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CoreTypesSocketStateTest, SocketStateValid_AllDefinedStates)
{
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_INIT));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_RAW_ESTABLISHED));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_ESTABLISHED));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_SHUTDOWN));
    EXPECT_TRUE(SocketStateValid(SOCK_STAT_CLOSE));
}

TEST_F(CoreTypesSocketStateTest, SocketStateValid_OutOfRange)
{
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(SOCK_STATE_COUNT)));
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(SOCK_STATE_COUNT + 1)));
    EXPECT_FALSE(SocketStateValid(static_cast<SocketState>(255)));
}

TEST_F(CoreTypesSocketStateTest, SocketStateToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_INIT), "init");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_RAW_ESTABLISHED), "raw_socket_established");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_ESTABLISHED), "established");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_SHUTDOWN), "shutdown");
    EXPECT_EQ(SocketStateToStr(SOCK_STAT_CLOSE), "closed");
}

TEST_F(CoreTypesSocketStateTest, SocketStateToStr_OutOfRangeReturnsUnknown)
{
    EXPECT_EQ(SocketStateToStr(static_cast<SocketState>(SOCK_STATE_COUNT)), "unknown");
    EXPECT_EQ(SocketStateToStr(static_cast<SocketState>(SOCK_STATE_COUNT + 1)), "unknown");
}

// ==================== SocketType ====================

class CoreTypesSocketTypeTest : public ::testing::Test {
};

TEST_F(CoreTypesSocketTypeTest, SocketTypeValid_AllDefinedTypes)
{
    EXPECT_TRUE(SocketTypeValid(SocketType::SOCK_TYPE_TCP));
    EXPECT_TRUE(SocketTypeValid(SocketType::SOCK_TYPE_UMQ));
}

TEST_F(CoreTypesSocketTypeTest, SocketTypeValid_OutOfRange)
{
    EXPECT_FALSE(SocketTypeValid(SocketType::SOCK_TYPE_COUNT));
}

TEST_F(CoreTypesSocketTypeTest, SocketTypeToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketTypeToStr(SocketType::SOCK_TYPE_TCP), "TCP");
    EXPECT_EQ(SocketTypeToStr(SocketType::SOCK_TYPE_UMQ), "UMQ");
}

// ==================== SocketCreateType ====================

class CoreTypesCreateTypeTest : public ::testing::Test {
};

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeValid_AllDefined)
{
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_UNKNOWN));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_LISTEN));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_CONNECT));
    EXPECT_TRUE(SocketCreateTypeValid(SOCK_CREATE_TYPE_ACCEPT));
}

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeValid_OutOfRange)
{
    EXPECT_FALSE(SocketCreateTypeValid(static_cast<SocketCreateType>(SOCK_CREATE_TYPE_COUNT)));
}

TEST_F(CoreTypesCreateTypeTest, SocketCreateTypeToStr_ReturnsExpectedStrings)
{
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_UNKNOWN), "unknown");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_LISTEN), "listen");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_CONNECT), "client");
    EXPECT_EQ(SocketCreateTypeToStr(SOCK_CREATE_TYPE_ACCEPT), "accept");
}
