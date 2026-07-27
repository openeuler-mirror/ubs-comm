/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <sstream>

#include "common/ubsocket_version.h"

using namespace ock::ubs;

// ==================== UBSVersion Tests ====================

class UBSVersionTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// --- Construction ---

TEST_F(UBSVersionTest, DefaultConstructor_SetsWholeToUintMax)
{
    UBSVersion v;
    EXPECT_EQ(v.whole, UINT32_MAX);
}

TEST_F(UBSVersionTest, ConstructorFromUint32)
{
    UBSVersion v(0x12345678u);
    EXPECT_EQ(v.whole, 0x12345678u);
}

TEST_F(UBSVersionTest, ConstructorFromFields)
{
    // major=1, minor=2, patch=3
    UBSVersion v(1, 2, 3);
    EXPECT_EQ(v.major, 1u);
    EXPECT_EQ(v.minor, 2u);
    EXPECT_EQ(v.patch, 3u);
}

TEST_F(UBSVersionTest, GetWhole_ReturnsWireValue)
{
    UBSVersion v(5, 10, 20);
    uint32_t w = v.GetWhole();
    EXPECT_EQ(w, v.whole);
    // Verify encoding: patch in bits 0-13, minor 14-25, major 26-31
    EXPECT_EQ(w & 0x3FFFu, 20u);        // patch
    EXPECT_EQ((w >> 14) & 0xFFFu, 10u); // minor
    EXPECT_EQ((w >> 26) & 0x3Fu, 5u);   // major
}

// --- Constants ---

TEST_F(UBSVersionTest, Constants_MajorMax)
{
    EXPECT_EQ(kProtocolMajorMax, 64u);
}

TEST_F(UBSVersionTest, Constants_MinorMax)
{
    EXPECT_EQ(kProtocolMinorMax, 4096u);
}

TEST_F(UBSVersionTest, Constants_PatchMax)
{
    EXPECT_EQ(kProtocolPatchMax, 16384u);
}

// --- Negotiate ---

TEST_F(UBSVersionTest, Negotiate_SameVersion_ReturnsCompatible)
{
    UBSVersion local(1, 5, 10);
    UBSVersion peer(1, 5, 10);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.major, 1u);
    EXPECT_EQ(negotiated.minor, 5u);
    EXPECT_EQ(negotiated.patch, 10u);
}

TEST_F(UBSVersionTest, Negotiate_LocalNewer_ChoosesPeer)
{
    UBSVersion local(1, 10, 100);
    UBSVersion peer(1, 5, 50);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.major, 1u);
    EXPECT_EQ(negotiated.minor, 5u);
    EXPECT_EQ(negotiated.patch, 50u);
}

TEST_F(UBSVersionTest, Negotiate_PeerNewer_ChoosesLocal)
{
    UBSVersion local(1, 5, 50);
    UBSVersion peer(1, 10, 100);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.major, 1u);
    EXPECT_EQ(negotiated.minor, 5u);
    EXPECT_EQ(negotiated.patch, 50u);
}

TEST_F(UBSVersionTest, Negotiate_MajorMismatch_ReturnsMismatch)
{
    UBSVersion local(1, 5, 10);
    UBSVersion peer(2, 5, 10);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kMajorMismatch);
}

TEST_F(UBSVersionTest, Negotiate_SameMajor_DifferentMinor_ChoosesLower)
{
    UBSVersion local(3, 200, 500);
    UBSVersion peer(3, 100, 1000);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.minor, 100u);
    EXPECT_EQ(negotiated.patch, 1000u);
}

TEST_F(UBSVersionTest, Negotiate_SameMajor_SameMinor_DifferentPatch_ChoosesLower)
{
    UBSVersion local(2, 4, 99);
    UBSVersion peer(2, 4, 50);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.patch, 50u);
}

// --- ValidateNegotiated ---

TEST_F(UBSVersionTest, ValidateNegotiated_SameMajor_Compatible)
{
    UBSVersion local(1, 0, 0);
    UBSVersion negotiated(1, 5, 100);
    EXPECT_EQ(negotiated.ValidateNegotiated(local), VersionCheckResult::kCompatible);
}

TEST_F(UBSVersionTest, ValidateNegotiated_DifferentMajor_Mismatch)
{
    UBSVersion local(1, 0, 0);
    UBSVersion negotiated(2, 0, 0);
    EXPECT_EQ(negotiated.ValidateNegotiated(local), VersionCheckResult::kMajorMismatch);
}

// --- operator<< ---

TEST_F(UBSVersionTest, OutputOperator_FormatsCorrectly)
{
    UBSVersion v(1, 23, 456);
    std::ostringstream oss;
    oss << v;
    EXPECT_EQ(oss.str(), "1.23.456");
}

// --- Edge cases ---

TEST_F(UBSVersionTest, Negotiate_ZeroVersion)
{
    UBSVersion local(0, 0, 0);
    UBSVersion peer(0, 0, 0);
    UBSVersion negotiated(99, 99, 99);
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.whole, 0u);
}

TEST_F(UBSVersionTest, Negotiate_MaxValues)
{
    UBSVersion local(63, 4095, 16383);
    UBSVersion peer(63, 4095, 16383);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.major, 63u);
    EXPECT_EQ(negotiated.minor, 4095u);
    EXPECT_EQ(negotiated.patch, 16383u);
}
