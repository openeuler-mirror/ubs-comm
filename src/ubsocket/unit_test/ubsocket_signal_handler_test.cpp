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
    void SetUp() override {}
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigusr2_DumpsObjectStats)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGUSR2);
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigint_ReturnsEarly)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGINT);
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Sigterm_ReturnsEarly)
{
    ubsocket_handle_signal(TEST_SIGNAL_SIGTERM);
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_Zero_ReturnsEarly)
{
    ubsocket_handle_signal(TEST_SIGNAL_ZERO);
}

TEST_F(UbsocketSignalHandlerTest, HandleSignal_InvalidSignal_ReturnsEarly)
{
    ubsocket_handle_signal(TEST_SIGNAL_INVALID);
}