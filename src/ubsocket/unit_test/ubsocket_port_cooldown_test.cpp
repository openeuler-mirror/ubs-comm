/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_port_cooldown.h"

using namespace ock::ubs;

// ==================== PortCooldownManager Tests ====================

class PortCooldownTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        LockRegistry::RegisterDefaultOps();
    }

    void SetUp() override
    {
        orig_cooldown_ = GlobalSetting::UBS_PORT_COOLDOWN_SEC;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_PORT_COOLDOWN_SEC = orig_cooldown_;
    }

    static umq_port_id_t MakePort(uint32_t chip, uint32_t die, uint32_t idx)
    {
        umq_port_id_t p;
        p.value = 0;
        p.bs.chip_id = chip;
        p.bs.die_id = die;
        p.bs.port_idx = idx;
        return p;
    }

    uint32_t orig_cooldown_ = 0;
};

// --- Singleton ---

TEST_F(PortCooldownTest, Singleton_ReturnsSameInstance)
{
    PortCooldownManager &a = PortCooldownManager::Instance();
    PortCooldownManager &b = PortCooldownManager::Instance();
    EXPECT_EQ(&a, &b);
}

// --- GetCooldownSec ---

TEST_F(PortCooldownTest, GetCooldownSec_ReturnsConfiguredValue)
{
    GlobalSetting::UBS_PORT_COOLDOWN_SEC = 120;
    EXPECT_EQ(PortCooldownManager::GetCooldownSec(), 120u);
}

// --- Mark and Is ---

TEST_F(PortCooldownTest, IsPortInCooldown_NotMarkedReturnsFalse)
{
    umq_port_id_t port = MakePort(0, 0, 1);
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(port));
}

TEST_F(PortCooldownTest, MarkPortInCooldown_ThenIsPortInCooldownReturnsTrue)
{
    umq_port_id_t port = MakePort(1, 0, 5);
    PortCooldownManager::MarkPortInCooldown(port);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(port));
}

TEST_F(PortCooldownTest, DifferentPorts_IndependentCooldown)
{
    umq_port_id_t portA = MakePort(0, 0, 10);
    umq_port_id_t portB = MakePort(0, 0, 20);

    PortCooldownManager::MarkPortInCooldown(portA);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(portA));
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(portB));
}

TEST_F(PortCooldownTest, DifferentChips_DifferentPorts)
{
    umq_port_id_t portA = MakePort(0, 0, 3);
    umq_port_id_t portB = MakePort(1, 0, 3);

    PortCooldownManager::MarkPortInCooldown(portA);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(portA));
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(portB));
}

TEST_F(PortCooldownTest, DifferentDies_DifferentPorts)
{
    umq_port_id_t portA = MakePort(0, 0, 7);
    umq_port_id_t portB = MakePort(0, 1, 7);

    PortCooldownManager::MarkPortInCooldown(portA);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(portA));
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(portB));
}

// --- Cooldown expiration ---

TEST_F(PortCooldownTest, CooldownExpires_AfterDuration)
{
    GlobalSetting::UBS_PORT_COOLDOWN_SEC = 1;
    umq_port_id_t port = MakePort(2, 0, 99);

    PortCooldownManager::MarkPortInCooldown(port);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(port));

    // Wait for cooldown to expire (1 sec + small margin for timing)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // IsPortInCooldown does lazy cleanup of expired entries
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(port));
}

TEST_F(PortCooldownTest, CooldownNotExpired_BeforeDuration)
{
    GlobalSetting::UBS_PORT_COOLDOWN_SEC = 60;
    umq_port_id_t port = MakePort(3, 0, 42);

    PortCooldownManager::MarkPortInCooldown(port);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(port));
}

// --- GetPortHash ---

TEST_F(PortCooldownTest, GetPortHash_SameChipDiePort_ProducesSameHash)
{
    umq_port_id_t p1 = MakePort(0xAA, 0xBB, 0xCC);
    umq_port_id_t p2 = MakePort(0xAA, 0xBB, 0xCC);
    // reserved field differs but should not affect hash
    p1.bs.reserved = 0x11;
    p2.bs.reserved = 0x22;

    // Both should map to same cooldown state
    PortCooldownManager::MarkPortInCooldown(p1);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(p2));
}

// --- Mark same port multiple times ---

TEST_F(PortCooldownTest, MarkSamePortTwice_StillInCooldown)
{
    umq_port_id_t port = MakePort(4, 0, 50);
    PortCooldownManager::MarkPortInCooldown(port);
    PortCooldownManager::MarkPortInCooldown(port);
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(port));
}

// --- Lazy cleanup (non-expired entries survive) ---

TEST_F(PortCooldownTest, LazyCleanup_OnlyRemovesExpiredEntries)
{
    GlobalSetting::UBS_PORT_COOLDOWN_SEC = 1;
    umq_port_id_t expiredPort = MakePort(5, 0, 0);
    umq_port_id_t activePort = MakePort(5, 0, 1);

    // Mark first port, wait for it to expire
    PortCooldownManager::MarkPortInCooldown(expiredPort);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Mark second port (still active)
    PortCooldownManager::MarkPortInCooldown(activePort);

    // Check active port — this triggers lazy cleanup of expired
    EXPECT_FALSE(PortCooldownManager::IsPortInCooldown(expiredPort));
    EXPECT_TRUE(PortCooldownManager::IsPortInCooldown(activePort));
}
