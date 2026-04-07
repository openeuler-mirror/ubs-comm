/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli args parser, etc
 * Author:
 * Create: 2026-03-30
 * Note:
 * History: 2026-03-30
*/

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "brpc_configure_settings.h"

namespace {
constexpr int K_ENV_SET_OVERWRITE = 1;
constexpr int K_CONFIG_INIT_OK = 0;
constexpr int K_CONFIG_INIT_FAIL = -1;
constexpr uint64_t K_SHARE_JFR_RX_QUEUE_DEPTH = 4096;
}  // namespace

class BrpcConfigureSettingsTest : public testing::Test {
public:
    void SetUp() override
    {
        ClearEnv();
    }

    void TearDown() override
    {
        ClearEnv();
    }

protected:
    void ClearEnv()
    {
        unsetenv("UBSOCKET_BRPC_ALLOC_SYM");
        unsetenv("UBSOCKET_BRPC_DEALLOC_SYM");
        unsetenv("UBSOCKET_READV_UNLIMITED");
        unsetenv("UBSOCKET_USE_POLLING");
        unsetenv("UBSOCKET_ENABLE_SHARE_JFR");
        unsetenv("UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH");
        unsetenv("UBSOCKET_AUTO_FALLBACK_TCP");
    }
};

TEST_F(BrpcConfigureSettingsTest, Init_UsesDefaultValues)
{
    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetBrpcAllocSymStr(), nullptr);
    EXPECT_EQ(settings.GetBrpcDeallocSymStr(), nullptr);
    EXPECT_EQ(settings.GetReadvUnlimited(), true);
    EXPECT_EQ(settings.GetUsePolling(), false);
    EXPECT_EQ(settings.EnableShareJfr(), true);
    EXPECT_EQ(settings.GetShareJfrRxQueueDepth(), static_cast<uint64_t>(DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH));
    EXPECT_EQ(settings.AutoFallbackTCP(), true);

    EXPECT_EQ(settings.UseUB(AF_SMC, SOCK_STREAM), true);
    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_STREAM), false);
}

TEST_F(BrpcConfigureSettingsTest, Init_FailsWhenAllocAndDeallocNotPaired)
{
    setenv("UBSOCKET_BRPC_ALLOC_SYM", "alloc_sym", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    EXPECT_EQ(settings.Init(), K_CONFIG_INIT_FAIL);
}

TEST_F(BrpcConfigureSettingsTest, Init_SucceedsWhenAllocAndDeallocAreBothSet)
{
    setenv("UBSOCKET_BRPC_ALLOC_SYM", "alloc_sym", K_ENV_SET_OVERWRITE);
    setenv("UBSOCKET_BRPC_DEALLOC_SYM", "dealloc_sym", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    ASSERT_NE(settings.GetBrpcAllocSymStr(), nullptr);
    ASSERT_NE(settings.GetBrpcDeallocSymStr(), nullptr);
    EXPECT_STREQ(settings.GetBrpcAllocSymStr(), "alloc_sym");
    EXPECT_STREQ(settings.GetBrpcDeallocSymStr(), "dealloc_sym");
}

TEST_F(BrpcConfigureSettingsTest, Init_ParsesBooleanAndDepthEnvValues)
{
    setenv("UBSOCKET_READV_UNLIMITED", "false", K_ENV_SET_OVERWRITE);
    setenv("UBSOCKET_USE_POLLING", "true", K_ENV_SET_OVERWRITE);
    setenv("UBSOCKET_ENABLE_SHARE_JFR", "false", K_ENV_SET_OVERWRITE);
    setenv("UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH", "4096", K_ENV_SET_OVERWRITE);
    setenv("UBSOCKET_AUTO_FALLBACK_TCP", "false", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetReadvUnlimited(), false);
    EXPECT_EQ(settings.GetUsePolling(), true);
    EXPECT_EQ(settings.EnableShareJfr(), false);
    EXPECT_EQ(settings.GetShareJfrRxQueueDepth(), K_SHARE_JFR_RX_QUEUE_DEPTH);
    EXPECT_EQ(settings.AutoFallbackTCP(), false);
}

TEST_F(BrpcConfigureSettingsTest, Init_ZeroShareJfrDepthFallsBackToDefault)
{
    setenv("UBSOCKET_SHARE_JFR_RX_QUEUE_DEPTH", "0", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetShareJfrRxQueueDepth(), static_cast<uint64_t>(DEFAULT_SHARE_JFR_RX_QUEUE_DEPTH));
}

