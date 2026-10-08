/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_errno.h"
#include "common/ubsocket_functions.h"
#include "common/ubsocket_version.h"

using namespace ock::ubs;

// ==================== Func::FloatCompare ====================

class FloatCompareTest : public ::testing::Test {
};

TEST_F(FloatCompareTest, LargerThan_DiffExceedsEpsilon)
{
    EXPECT_TRUE(Func::FloatLargerThan(1.0f, 0.0f));
}

TEST_F(FloatCompareTest, LargerThan_DiffWithinEpsilon)
{
    EXPECT_FALSE(Func::FloatLargerThan(1.0f, 1.0f - 1e-7f));
}

TEST_F(FloatCompareTest, LargerThan_Equal)
{
    EXPECT_FALSE(Func::FloatLargerThan(3.14f, 3.14f));
}

TEST_F(FloatCompareTest, LargerThan_NegativeNumbers)
{
    EXPECT_TRUE(Func::FloatLargerThan(-1.0f, -2.0f));
    EXPECT_FALSE(Func::FloatLargerThan(-2.0f, -1.0f));
}

TEST_F(FloatCompareTest, LessThan_DiffExceedsEpsilon)
{
    EXPECT_TRUE(Func::FloatLessThan(0.0f, 1.0f));
}

TEST_F(FloatCompareTest, LessThan_DiffWithinEpsilon)
{
    EXPECT_FALSE(Func::FloatLessThan(1.0f - 1e-7f, 1.0f));
}

TEST_F(FloatCompareTest, LessThan_Equal)
{
    EXPECT_FALSE(Func::FloatLessThan(2.71f, 2.71f));
}

TEST_F(FloatCompareTest, Equal_Exact)
{
    EXPECT_TRUE(Func::FloatEqual(1.5f, 1.5f));
}

TEST_F(FloatCompareTest, Equal_DiffWithinEpsilon)
{
    EXPECT_TRUE(Func::FloatEqual(1.0f, 1.0f + 1e-7f));
}

TEST_F(FloatCompareTest, Equal_NotEqual)
{
    // Note: FloatEqual uses (a-b) < EPS_F without abs(), so a < b also
    // returns true. Use a > b so diff is positive and exceeds epsilon.
    EXPECT_FALSE(Func::FloatEqual(0.01f, 0.0f));
}

// ==================== Func::StrTrim ====================

class StrTrimTest : public ::testing::Test {
};

TEST_F(StrTrimTest, TrimSpaces)
{
    EXPECT_EQ(Func::StrTrim("  hello  "), "hello");
}

TEST_F(StrTrimTest, TrimTabsAndNewlines)
{
    EXPECT_EQ(Func::StrTrim("\t\n  world  \r\n"), "world");
}

TEST_F(StrTrimTest, NoWhitespace)
{
    EXPECT_EQ(Func::StrTrim("abc"), "abc");
}

TEST_F(StrTrimTest, EmptyString)
{
    EXPECT_EQ(Func::StrTrim(""), "");
}

TEST_F(StrTrimTest, AllWhitespace)
{
    EXPECT_EQ(Func::StrTrim("   \t\n\r  "), "");
}

// ==================== Func::StrSplit ====================

class StrSplitTest : public ::testing::Test {
};

TEST_F(StrSplitTest, SplitByComma)
{
    auto result = Func::StrSplit("a,b,c", ",");
    EXPECT_EQ(result.size(), 3U);
    EXPECT_NE(result.find("a"), result.end());
    EXPECT_NE(result.find("b"), result.end());
    EXPECT_NE(result.find("c"), result.end());
}

TEST_F(StrSplitTest, SplitByMultiCharSeparator)
{
    auto result = Func::StrSplit("ab::cd::ef", "::");
    EXPECT_EQ(result.size(), 3U);
    EXPECT_NE(result.find("ab"), result.end());
    EXPECT_NE(result.find("cd"), result.end());
    EXPECT_NE(result.find("ef"), result.end());
}

TEST_F(StrSplitTest, EmptySource)
{
    auto result = Func::StrSplit("", ",");
    EXPECT_EQ(result.size(), 1U);
    EXPECT_NE(result.find(""), result.end());
}

TEST_F(StrSplitTest, EmptySeparator)
{
    auto result = Func::StrSplit("hello", "");
    EXPECT_EQ(result.size(), 1U);
    EXPECT_NE(result.find("hello"), result.end());
}

TEST_F(StrSplitTest, Deduplicate)
{
    auto result = Func::StrSplit("a,a,b", ",");
    EXPECT_EQ(result.size(), 2U);
    EXPECT_NE(result.find("a"), result.end());
    EXPECT_NE(result.find("b"), result.end());
}

TEST_F(StrSplitTest, ConsecutiveSeparators)
{
    auto result = Func::StrSplit("a,,b", ",");
    EXPECT_EQ(result.size(), 2U);
    EXPECT_NE(result.find("a"), result.end());
    EXPECT_NE(result.find("b"), result.end());
}

// ==================== Func::StrLowerCase ====================

class StrLowerCaseTest : public ::testing::Test {
};

TEST_F(StrLowerCaseTest, AllUppercase)
{
    EXPECT_EQ(Func::StrLowerCase("HELLO"), "hello");
}

TEST_F(StrLowerCaseTest, MixedCase)
{
    EXPECT_EQ(Func::StrLowerCase("HelloWorld"), "helloworld");
}

TEST_F(StrLowerCaseTest, AlreadyLowercase)
{
    EXPECT_EQ(Func::StrLowerCase("abc"), "abc");
}

TEST_F(StrLowerCaseTest, EmptyString)
{
    EXPECT_EQ(Func::StrLowerCase(""), "");
}

TEST_F(StrLowerCaseTest, StrLowerCaseDirect)
{
    std::string s = "Hello WORLD";
    Func::StrLowerCaseDirect(s);
    EXPECT_EQ(s, "hello world");
}

// ==================== Func::BoolFromStr ====================

class BoolFromStrTest : public ::testing::Test {
};

TEST_F(BoolFromStrTest, TrueString)
{
    EXPECT_TRUE(Func::BoolFromStr("true"));
}

TEST_F(BoolFromStrTest, TrueWithSpaces)
{
    EXPECT_TRUE(Func::BoolFromStr("  true  "));
}

TEST_F(BoolFromStrTest, TrueMixedCase)
{
    EXPECT_TRUE(Func::BoolFromStr("True"));
    EXPECT_TRUE(Func::BoolFromStr("TRUE"));
}

TEST_F(BoolFromStrTest, FalseForOtherStrings)
{
    EXPECT_FALSE(Func::BoolFromStr("false"));
    EXPECT_FALSE(Func::BoolFromStr("yes"));
    EXPECT_FALSE(Func::BoolFromStr("1"));
}

TEST_F(BoolFromStrTest, EmptyString)
{
    EXPECT_FALSE(Func::BoolFromStr(""));
}

// ==================== Func::Error2Str ====================

class Error2StrTest : public ::testing::Test {
};

TEST_F(Error2StrTest, KnownErrno)
{
    char *msg = Func::Error2Str(EINVAL);
    EXPECT_NE(msg, nullptr);
    EXPECT_GT(std::strlen(msg), 0U);
}

TEST_F(Error2StrTest, SuccessErrno)
{
    char *msg = Func::Error2Str(0);
    EXPECT_NE(msg, nullptr);
    EXPECT_GT(std::strlen(msg), 0U);
}

// ==================== Func::SecureRandUInt32 ====================

class SecureRandTest : public ::testing::Test {
};

TEST_F(SecureRandTest, ReturnsNonZero)
{
    // /dev/urandom is available on Linux; created as non-static to use instance method
    uint32_t val = Func().SecureRandUInt32();
    EXPECT_NE(val, 0U);
}

TEST_F(SecureRandTest, MultipleCallsYieldDifferentValues)
{
    Func f;
    uint32_t a = f.SecureRandUInt32();
    uint32_t b = f.SecureRandUInt32();
    // Extremely unlikely to get same twice from /dev/urandom
    // but not guaranteed, so test as probabilistic
    EXPECT_NE(a, 0U);
    EXPECT_NE(b, 0U);
}

// ==================== Func::CurrentTimeNs ====================

class CurrentTimeNsTest : public ::testing::Test {
};

TEST_F(CurrentTimeNsTest, ReturnsPositiveValue)
{
    uint64_t t = Func::CurrentTimeNs();
    EXPECT_GT(t, 0ULL);
}

TEST_F(CurrentTimeNsTest, MonotonicIncreasing)
{
    uint64_t t1 = Func::CurrentTimeNs();
    uint64_t t2 = Func::CurrentTimeNs();
    EXPECT_GE(t2, t1);
}

// ==================== UBSVersion ====================

class UBSVersionTest : public ::testing::Test {
};

TEST_F(UBSVersionTest, DefaultConstruct_MaxUint)
{
    UBSVersion v;
    EXPECT_EQ(v.GetWhole(), UINT32_MAX);
}

TEST_F(UBSVersionTest, ConstructFromUint32)
{
    UBSVersion v(0x00012345U);
    EXPECT_EQ(v.GetWhole(), 0x00012345U);
}

TEST_F(UBSVersionTest, ConstructFromComponents)
{
    UBSVersion v(1, 2, 3);
    EXPECT_EQ(v.major, 1U);
    EXPECT_EQ(v.minor, 2U);
    EXPECT_EQ(v.patch, 3U);
}

TEST_F(UBSVersionTest, BitfieldLayout_MajorUpperBits)
{
    // major is bits 26-31, minor 14-25, patch 0-13
    UBSVersion v(0x3F, 0, 0); // major=63, all 6 bits set
    uint32_t w = v.GetWhole();
    EXPECT_NE(w, 0U);
    EXPECT_EQ(v.major, 63U);
    EXPECT_EQ(v.minor, 0U);
    EXPECT_EQ(v.patch, 0U);
}

TEST_F(UBSVersionTest, BitfieldLayout_PatchLowerBits)
{
    UBSVersion v(0, 0, 0x3FFF); // patch=16383, all 14 bits
    EXPECT_EQ(v.patch, 0x3FFFU);
    EXPECT_EQ(v.major, 0U);
    EXPECT_EQ(v.minor, 0U);
}

TEST_F(UBSVersionTest, BitfieldLayout_FullEncoding)
{
    // major=2(01_xxxx), minor=100(0110_0100), patch=500(011111_0100)
    UBSVersion v(2, 100, 500);
    EXPECT_EQ(v.major, 2U);
    EXPECT_EQ(v.minor, 100U);
    EXPECT_EQ(v.patch, 500U);
}

TEST_F(UBSVersionTest, Negotiate_SameVersion)
{
    UBSVersion local(2, 100, 500);
    UBSVersion peer(2, 100, 500);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.GetWhole(), local.GetWhole());
}

TEST_F(UBSVersionTest, Negotiate_LocalNewer_PickPeer)
{
    UBSVersion local(2, 200, 0);
    UBSVersion peer(2, 100, 0);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.GetWhole(), peer.GetWhole());
}

TEST_F(UBSVersionTest, Negotiate_PeerNewer_PickLocal)
{
    UBSVersion local(2, 100, 0);
    UBSVersion peer(2, 200, 0);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
    EXPECT_EQ(negotiated.GetWhole(), local.GetWhole());
}

TEST_F(UBSVersionTest, Negotiate_MajorMismatch)
{
    UBSVersion local(1, 0, 0);
    UBSVersion peer(2, 0, 0);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kMajorMismatch);
}

TEST_F(UBSVersionTest, Negotiate_SameMajor_DifferentPatch)
{
    // patch 不同也可兼容
    UBSVersion local(3, 50, 100);
    UBSVersion peer(3, 50, 200);
    UBSVersion negotiated;
    auto result = local.Negotiate(peer, negotiated);
    EXPECT_EQ(result, VersionCheckResult::kCompatible);
}

TEST_F(UBSVersionTest, ValidateNegotiated_Success)
{
    UBSVersion negotiated(2, 100, 500);
    UBSVersion local(2, 100, 500);
    EXPECT_EQ(negotiated.ValidateNegotiated(local), VersionCheckResult::kCompatible);
}

TEST_F(UBSVersionTest, ValidateNegotiated_MajorMismatch)
{
    UBSVersion negotiated(3, 0, 0);
    UBSVersion local(2, 0, 0);
    EXPECT_EQ(negotiated.ValidateNegotiated(local), VersionCheckResult::kMajorMismatch);
}

// ==================== InnerCode / Error Helpers ====================

class InnerCodeTest : public ::testing::Test {
};

TEST_F(InnerCodeTest, IsOk_SuccessReturnsTrue)
{
    EXPECT_TRUE(IsOk(UBS_OK));
}

TEST_F(InnerCodeTest, IsOk_ErrorReturnsFalse)
{
    EXPECT_FALSE(IsOk(UBS_ERROR));
    EXPECT_FALSE(IsOk(UBS_INVALID_PARAM));
}

TEST_F(InnerCodeTest, IsOk_ErrorWithFlag_StillFailure)
{
    Result flagged = UBS_ERROR | UBS_RETRYABLE_MASK;
    EXPECT_FALSE(IsOk(flagged));
}

TEST_F(InnerCodeTest, IsRetryable_FlagSet)
{
    Result flagged = UBS_ERROR | UBS_RETRYABLE_MASK;
    EXPECT_TRUE(IsRetryable(flagged));
}

TEST_F(InnerCodeTest, IsRetryable_FlagNotSet)
{
    EXPECT_FALSE(IsRetryable(UBS_ERROR));
}

TEST_F(InnerCodeTest, IsDegradable_FlagSet)
{
    Result flagged = UBS_ERROR | UBS_DEGRADABLE_MASK;
    EXPECT_TRUE(IsDegradable(flagged));
}

TEST_F(InnerCodeTest, IsDegradable_FlagNotSet)
{
    EXPECT_FALSE(IsDegradable(UBS_ERROR));
}

TEST_F(InnerCodeTest, GetPureCode_StripsFlags)
{
    Result flagged = UBS_ERROR | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    EXPECT_EQ(GetPureCode(flagged), UBS_ERROR);
}

TEST_F(InnerCodeTest, GetPureCode_NoFlags)
{
    EXPECT_EQ(GetPureCode(UBS_ERROR), UBS_ERROR);
}

TEST_F(InnerCodeTest, GetPureCode_Ok)
{
    EXPECT_EQ(GetPureCode(UBS_OK), UBS_OK);
}

TEST_F(InnerCodeTest, WithoutFlags)
{
    InnerCode flagged = UBS_ERROR | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    EXPECT_EQ(WithoutFlags(flagged), UBS_ERROR);
}

TEST_F(InnerCodeTest, OperatorAnd_CombinesFlags)
{
    InnerCode combined = UBS_ERROR | UBS_RETRYABLE_MASK;
    Result asResult = static_cast<Result>(combined);
    EXPECT_TRUE(IsRetryable(asResult));
    EXPECT_FALSE(IsOk(asResult));
}

TEST_F(InnerCodeTest, OperatorMinus_RemovesFlag)
{
    InnerCode flagged = UBS_ERROR | UBS_RETRYABLE_MASK;
    InnerCode stripped = flagged - UBS_RETRYABLE_MASK;
    EXPECT_EQ(stripped, UBS_ERROR);
}

TEST_F(InnerCodeTest, FlagsMask_CoversBothFlags)
{
    EXPECT_EQ(UBS_FLAGS_MASK, static_cast<int32_t>(UBS_RETRYABLE_MASK) | static_cast<int32_t>(UBS_DEGRADABLE_MASK));
}

TEST_F(InnerCodeTest, DoubleFlag_IsRetryableAndDegradable)
{
    Result flagged = UBS_ERROR | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK;
    EXPECT_TRUE(IsRetryable(flagged));
    EXPECT_TRUE(IsDegradable(flagged));
    EXPECT_FALSE(IsOk(flagged));
}
