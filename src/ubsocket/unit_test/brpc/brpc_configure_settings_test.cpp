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
constexpr int K_LINK_PRIORITY_VAL_5 = 5;
constexpr uint64_t K_MIN_RESERVED_CREDIT_VAL_200 = 200;
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

TEST_F(BrpcConfigureSettingsTest, Init_LinkPriorityValid)
{
    setenv("UBSOCKET_LINK_PRIORITY", "5", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetLinkPriority(), K_LINK_PRIORITY_VAL_5);
}

TEST_F(BrpcConfigureSettingsTest, Init_LinkPriorityOutOfRange)
{
    setenv("UBSOCKET_LINK_PRIORITY", "20", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    // Should fall back to default
    EXPECT_EQ(settings.GetLinkPriority(), DEFAULT_LINK_PRIORITY);
}

TEST_F(BrpcConfigureSettingsTest, Init_MinReservedCredit)
{
    setenv("UBSOCKET_MIN_RESERVED_CREDIT", "200", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetMinReservedCredit(), K_MIN_RESERVED_CREDIT_VAL_200);
}

TEST_F(BrpcConfigureSettingsTest, Init_MinReservedCreditZero)
{
    setenv("UBSOCKET_MIN_RESERVED_CREDIT", "0", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetMinReservedCredit(), DEFAULT_MIN_RESERVED_CREDIT);
}

TEST_F(BrpcConfigureSettingsTest, Init_OnlyAllocSymSet)
{
    setenv("UBSOCKET_BRPC_ALLOC_SYM", "alloc_sym", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    EXPECT_EQ(settings.Init(), K_CONFIG_INIT_FAIL);
}

TEST_F(BrpcConfigureSettingsTest, Init_OnlyDeallocSymSet)
{
    setenv("UBSOCKET_BRPC_DEALLOC_SYM", "dealloc_sym", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    EXPECT_EQ(settings.Init(), K_CONFIG_INIT_FAIL);
}

TEST_F(BrpcConfigureSettingsTest, UseUB_AFSMC)
{
    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.UseUB(AF_SMC, SOCK_STREAM), true);
}

TEST_F(BrpcConfigureSettingsTest, UseUB_AFInet6)
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.UseUB(AF_INET6, SOCK_STREAM), true);
}

TEST_F(BrpcConfigureSettingsTest, UseUB_SockDgram)
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    // DGRAM should not use UB
    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_DGRAM), false);
}

TEST_F(BrpcConfigureSettingsTest, Init_LinkPriorityNegative)
{
    setenv("UBSOCKET_LINK_PRIORITY", "-1", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    // Should use default for invalid values
    EXPECT_EQ(settings.GetLinkPriority(), DEFAULT_LINK_PRIORITY);
}

TEST_F(BrpcConfigureSettingsTest, Init_LinkPriorityInvalidString)
{
    setenv("UBSOCKET_LINK_PRIORITY", "invalid", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    // Should use default for invalid values
    EXPECT_EQ(settings.GetLinkPriority(), DEFAULT_LINK_PRIORITY);
}

TEST_F(BrpcConfigureSettingsTest, Init_EnableShareJfrTrue)
{
    setenv("UBSOCKET_ENABLE_SHARE_JFR", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.EnableShareJfr(), true);
}

TEST_F(BrpcConfigureSettingsTest, Init_EnableShareJfrFalse)
{
    setenv("UBSOCKET_ENABLE_SHARE_JFR", "false", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.EnableShareJfr(), false);
}

TEST_F(BrpcConfigureSettingsTest, Init_AutoFallbackTcpTrue)
{
    setenv("UBSOCKET_AUTO_FALLBACK_TCP", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.AutoFallbackTCP(), true);
}

TEST_F(BrpcConfigureSettingsTest, Init_UsePollingTrue)
{
    setenv("UBSOCKET_USE_POLLING", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetUsePolling(), true);
}

TEST_F(BrpcConfigureSettingsTest, Init_ReadvUnlimitedFalse)
{
    setenv("UBSOCKET_READV_UNLIMITED", "false", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetReadvUnlimited(), false);
}

TEST_F(BrpcConfigureSettingsTest, UseUB_WithoutForceFlag)
{
    // Ensure clean environment - unset any previous UBSOCKET_USE_UB_FORCE
    unsetenv("UBSOCKET_USE_UB_FORCE");

    // Without UBSOCKET_USE_UB_FORCE, AF_INET should not use UB
    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_STREAM), false);
    EXPECT_EQ(settings.UseUB(AF_INET6, SOCK_STREAM), false);
}

TEST_F(BrpcConfigureSettingsTest, UseUB_DifferentSocketTypes)
{
    setenv("UBSOCKET_USE_UB_FORCE", "true", K_ENV_SET_OVERWRITE);

    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    // Only SOCK_STREAM should use UB
    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_STREAM), true);
    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_DGRAM), false);
    EXPECT_EQ(settings.UseUB(AF_INET, SOCK_RAW), false);
}

TEST_F(BrpcConfigureSettingsTest, GetBrpcAllocSymStr_NullWhenNotSet)
{
    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetBrpcAllocSymStr(), nullptr);
}

TEST_F(BrpcConfigureSettingsTest, GetBrpcDeallocSymStr_NullWhenNotSet)
{
    Brpc::ConfigSettings settings;
    ASSERT_EQ(settings.Init(), K_CONFIG_INIT_OK);

    EXPECT_EQ(settings.GetBrpcDeallocSymStr(), nullptr);
}