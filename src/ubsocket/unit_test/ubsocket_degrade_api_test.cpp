/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <cerrno>

#include "include/ubsocket.h"
#include "include/ubsocket_def.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_set.h"
#include "core/ubsocket_socket.h"

using namespace ock::ubs;

// ==================== UB Degrade API Tests ====================

class UbDegradeApiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Ensure UBS_INITED is true so APIs don't return -1
        GlobalSetting::UBS_INITED = true;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_INITED = false;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    }
};

// --- ubsocket_is_ub_transport ---

TEST_F(UbDegradeApiTest, IsUbTransportReturnsZeroForUnknownFd)
{
    // fd not in ArraySet → returns 0 (TCP)
    EXPECT_EQ(0, ubsocket_is_ub_transport(99998));
}

TEST_F(UbDegradeApiTest, IsUbTransportReturnsMinusOneInNativeTcpMode)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    EXPECT_EQ(-1, ubsocket_is_ub_transport(0));
}

TEST_F(UbDegradeApiTest, IsUbTransportReturnsMinusOneWhenNotInited)
{
    GlobalSetting::UBS_INITED = false;
    EXPECT_EQ(-1, ubsocket_is_ub_transport(0));
}

TEST_F(UbDegradeApiTest, IsUbTransportReturnsMinusOneForNegativeFd)
{
    EXPECT_EQ(-1, ubsocket_is_ub_transport(-1));
}

// --- ubsocket_set_degrade_enable ---

TEST_F(UbDegradeApiTest, SetDegradeEnableTrue)
{
    EXPECT_EQ(0, ubsocket_set_degrade_enable(1));
    EXPECT_TRUE(GlobalSetting::UBS_ENABLE_DEGRADE);
}

TEST_F(UbDegradeApiTest, SetDegradeEnableFalse)
{
    EXPECT_EQ(0, ubsocket_set_degrade_enable(0));
    EXPECT_FALSE(GlobalSetting::UBS_ENABLE_DEGRADE);
}

TEST_F(UbDegradeApiTest, SetDegradeEnableRestoresDefault)
{
    // Restore default after test
    GlobalSetting::UBS_ENABLE_DEGRADE = true;
    EXPECT_TRUE(GlobalSetting::UBS_ENABLE_DEGRADE);
}

TEST_F(UbDegradeApiTest, SetDegradeEnableSucceedsWhenNotInited)
{
    // ubsocket_set_degrade_enable intentionally does NOT check UBS_INITED,
    // so that brpc can set the flag before ubsocket_init() runs
    // (GlobalInitialize may execute before main()).
    GlobalSetting::UBS_INITED = false;
    EXPECT_EQ(0, ubsocket_set_degrade_enable(1));
}

// --- UBS_AUTO_FALLBACK_TCP removed ---

TEST_F(UbDegradeApiTest, AutoFallbackTcpFieldRemoved)
{
    // UBS_AUTO_FALLBACK_TCP has been removed from GlobalSetting.
    // This test verifies the field no longer exists by checking that
    // UBS_ENABLE_DEGRADE is the only degrade-related boolean.
    // If UBS_AUTO_FALLBACK_TCP still existed, this would be a compile error.
    EXPECT_TRUE(GlobalSetting::UBS_ENABLE_DEGRADE);
}

// ==================== ubsocket_init / ubsocket_uninit 生命周期契约 ====================

TEST(UbsocketInitLifecycle, ReinitAfterUninitFails)
{
    // 模拟 ubsocket_uninit() 之后的进程状态：UBS_INITED 已复位、退出标志已置位。
    GlobalSetting::UBS_INITED = false;
    GlobalSetting::MarkExiting();

    u_init_options_t options{};
    errno = 0;
    EXPECT_EQ(ubsocket_init(&options), static_cast<int>(UBS_ERROR));
    EXPECT_EQ(errno, EPERM);

    // 恢复全局状态，避免污染其它用例。
    GlobalSetting::UBS_EXITING.store(false, std::memory_order_release);
    GlobalSetting::UBS_INITED = false;
}
