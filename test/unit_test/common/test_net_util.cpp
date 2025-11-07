// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
// Author: zhiwei

#include <gtest/gtest.h>

#include "net_util.h"

namespace ock {
namespace hcom {

class TestNetUtil : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(TestNetUtil, ScopeExitSimple)
{
    bool flag = false;
    auto guard0 = MakeScopeExit([&flag]() { EXPECT_TRUE(flag); });
    auto guard1 = MakeScopeExit([&flag]() { flag = true; });
}

TEST_F(TestNetUtil, ScopeExitActive)
{
    auto guard = MakeScopeExit([]() { EXPECT_TRUE(true); });
    EXPECT_TRUE(guard.Active());
}

TEST_F(TestNetUtil, ScopeExitDeactivate)
{
    bool flag = true;
    auto guard0 = MakeScopeExit([&flag]() { EXPECT_TRUE(flag); });
    auto guard1 = MakeScopeExit([&flag]() { flag = false; });

    guard1.Deactivate();
    EXPECT_FALSE(guard1.Active());
}

TEST_F(TestNetUtil, HexStringToBuffFailed)
{
    uint8_t buf[4];

    EXPECT_FALSE(HexStringToBuff("112233", sizeof(buf), nullptr));
    EXPECT_FALSE(HexStringToBuff("112233", sizeof(buf), buf));
    EXPECT_FALSE(HexStringToBuff("112233xyz", sizeof(buf), buf));
}

TEST_F(TestNetUtil, HexStringToBuffOk)
{
    uint8_t buf[4];

    EXPECT_TRUE(HexStringToBuff("61626364", sizeof(buf), buf));
    EXPECT_EQ(buf[0], 0x61);
    EXPECT_EQ(buf[1], 0x62);
    EXPECT_EQ(buf[2], 0x63);
    EXPECT_EQ(buf[3], 0x64);
}

TEST_F(TestNetUtil, BuffToHexStringFailed)
{
    std::string out;
    EXPECT_FALSE(BuffToHexString(nullptr, 8, out));
}

TEST_F(TestNetUtil, BuffToHexStringOk)
{
    char buf[] = "12345678";
    std::string out = "01234567";
    EXPECT_TRUE(BuffToHexString(buf, sizeof(buf), out));
}
}  // namespace hcom
}  // namespace ock
