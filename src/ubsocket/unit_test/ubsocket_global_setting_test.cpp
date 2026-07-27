/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <cstdlib>

#include "common/ubsocket_global_setting.h"

using namespace ock::ubs;

// ==================== GlobalSetting Tests ====================

class GlobalSettingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        GlobalSetting::AddRules();
    }
};

// --- Default values ---

TEST_F(GlobalSettingTest, Defaults_TxDepth)
{
    EXPECT_EQ(GlobalSetting::GetTxDepth(), 0);
}

TEST_F(GlobalSettingTest, Defaults_PortCooldownSec)
{
    EXPECT_GT(GlobalSetting::UBS_PORT_COOLDOWN_SEC, 0u);
}

TEST_F(GlobalSettingTest, Defaults_ThreadPoolSize)
{
    EXPECT_GT(GlobalSetting::UBS_THREAD_POOL_SIZE, 0u);
}

// --- Async enabled checks ---

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenDisabled)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
    EXPECT_FALSE(GlobalSetting::AsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenThreadCountPositive)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 1;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
    EXPECT_TRUE(GlobalSetting::AsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncAcceptorEnabled_WhenFlagEnabled)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    EXPECT_TRUE(GlobalSetting::AsyncAcceptorEnabled());
}

TEST_F(GlobalSettingTest, AsyncConnectorEnabled_WhenZero)
{
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 0;
    EXPECT_FALSE(GlobalSetting::AsyncConnectorEnabled());
}

TEST_F(GlobalSettingTest, AsyncConnectorEnabled_WhenPositive)
{
    GlobalSetting::UBS_CONNECTOR_ASYNC_THREAD_COUNT = 5;
    EXPECT_TRUE(GlobalSetting::AsyncConnectorEnabled());
}

TEST_F(GlobalSettingTest, AsyncEpollEnabled_WhenZero)
{
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 0;
    EXPECT_FALSE(GlobalSetting::AsyncEpollEnabled());
}

TEST_F(GlobalSettingTest, AsyncEpollEnabled_WhenPositive)
{
    GlobalSetting::UBS_EPOLL_ASYNC_THREAD_COUNT = 3;
    EXPECT_TRUE(GlobalSetting::AsyncEpollEnabled());
}

// --- GetEnv: integer ---

TEST_F(GlobalSettingTest, GetEnv_Int_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_INT_VAR");
    int64_t out = 0;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_INT_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_Int_ValidValue)
{
    setenv("TEST_UBS_INT_VAR", "123", 1);
    int64_t out = 0;
    EXPECT_TRUE(GlobalSetting::GetEnv("TEST_UBS_INT_VAR", out));
    EXPECT_EQ(out, 123);
    unsetenv("TEST_UBS_INT_VAR");
}

TEST_F(GlobalSettingTest, GetEnv_Int_NegativeValue)
{
    setenv("TEST_UBS_INT_NEG", "-456", 1);
    int64_t out = 0;
    EXPECT_TRUE(GlobalSetting::GetEnv("TEST_UBS_INT_NEG", out));
    EXPECT_EQ(out, -456);
    unsetenv("TEST_UBS_INT_NEG");
}

TEST_F(GlobalSettingTest, GetEnv_Int_InvalidStringReturnsFalse)
{
    setenv("TEST_UBS_INT_INVALID", "not_a_number", 1);
    int64_t out = 0;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_INT_INVALID", out));
    unsetenv("TEST_UBS_INT_INVALID");
}

TEST_F(GlobalSettingTest, GetEnv_Int_EmptyStringReturnsFalse)
{
    setenv("TEST_UBS_INT_EMPTY", "", 1);
    int64_t out = 0;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_INT_EMPTY", out));
    unsetenv("TEST_UBS_INT_EMPTY");
}

// --- GetEnv: float ---

TEST_F(GlobalSettingTest, GetEnv_Float_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_FLOAT_VAR");
    float out = 0;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_FLOAT_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_Float_IntegerValue)
{
    setenv("TEST_UBS_FLOAT_INT", "42", 1);
    float out = 0;
    EXPECT_TRUE(GlobalSetting::GetEnv("TEST_UBS_FLOAT_INT", out));
    unsetenv("TEST_UBS_FLOAT_INT");
}

TEST_F(GlobalSettingTest, GetEnv_Float_InvalidReturnsFalse)
{
    setenv("TEST_UBS_FLOAT_INVALID", "abc", 1);
    float out = 0;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_FLOAT_INVALID", out));
    unsetenv("TEST_UBS_FLOAT_INVALID");
}

// --- GetEnv: string ---

TEST_F(GlobalSettingTest, GetEnv_String_NotSetReturnsFalse)
{
    unsetenv("TEST_UBS_STR_VAR");
    std::string out;
    EXPECT_FALSE(GlobalSetting::GetEnv("TEST_UBS_STR_VAR", out));
}

TEST_F(GlobalSettingTest, GetEnv_String_ValidValue)
{
    setenv("TEST_UBS_STR_VAR", "hello", 1);
    std::string out;
    EXPECT_TRUE(GlobalSetting::GetEnv("TEST_UBS_STR_VAR", out));
    EXPECT_EQ(out, "hello");
    unsetenv("TEST_UBS_STR_VAR");
}

TEST_F(GlobalSettingTest, GetEnv_String_EmptyValue)
{
    setenv("TEST_UBS_STR_EMPTY", "", 1);
    std::string out;
    EXPECT_TRUE(GlobalSetting::GetEnv("TEST_UBS_STR_EMPTY", out));
    EXPECT_TRUE(out.empty());
    unsetenv("TEST_UBS_STR_EMPTY");
}
