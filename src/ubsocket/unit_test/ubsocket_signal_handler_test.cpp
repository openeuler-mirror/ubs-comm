/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "ubsocket_signal_handler.h"
#include "ubsocket_logger.h"
#include "ubsocket_obj_statistics.h"

#include <gtest/gtest.h>
#include <csignal>
#include <mockcpp/mockcpp.hpp>

using namespace ock::ubs;

namespace {
static const int TEST_SIGNAL_SIGUSR2 = SIGUSR2;
static const int TEST_SIGNAL_SIGINT = SIGINT;
static const int TEST_SIGNAL_SIGTERM = SIGTERM;
static const int TEST_SIGNAL_ZERO = 0;
static const int TEST_SIGNAL_INVALID = 999;
} // namespace

class UbsocketSignalHandlerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        /* 清除可能残留的标志位，保证每个用例独立 */
        (void)ConsumeDumpRequest();
    }
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

/* SIGUSR2 仅设置标志位，ConsumeDumpRequest 返回 true 并清除标志 */
TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigusr2_SetsDumpFlag)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGUSR2);
    EXPECT_TRUE(ConsumeDumpRequest());
    /* 二次消费应返回 false（标志已清除） */
    EXPECT_FALSE(ConsumeDumpRequest());
}

/* 非 SIGUSR2 信号不设置标志位 */
TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigint_DoesNotSetFlag)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGINT);
    EXPECT_FALSE(ConsumeDumpRequest());
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigterm_DoesNotSetFlag)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGTERM);
    EXPECT_FALSE(ConsumeDumpRequest());
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Zero_DoesNotSetFlag)
{
    ubsocket_handle_signal(TEST_SIGNAL_ZERO);
    EXPECT_FALSE(ConsumeDumpRequest());
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_InvalidSignal_DoesNotSetFlag)
{
    ubsocket_handle_signal(TEST_SIGNAL_INVALID);
    EXPECT_FALSE(ConsumeDumpRequest());
}

/* 无信号时 ConsumeDumpRequest 返回 false */
TEST_F(UbsocketSignalHandlerTest, ConsumeDumpRequest_NoSignal_ReturnsFalse)
{
    EXPECT_FALSE(ConsumeDumpRequest());
}