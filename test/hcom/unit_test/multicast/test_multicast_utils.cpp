/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

#include "utils/multicast_utils.h"

namespace ock {
namespace hcom {
namespace {
constexpr char LOOPBACK_IP[] = "127.0.0.1";
constexpr char LOOPBACK_MASK[] = "127.0.0.0/8";
// a private subnet that never exists on the host, so the mask matches nothing
constexpr char UNMATCHED_MASK[] = "10.99.99.0/24";
constexpr char INVALID_MASK[] = "not-an-ip";
constexpr char TCP_URL[] = "tcp://127.0.0.1:9981";
constexpr char NO_SCHEMA_URL[] = "127.0.0.1:9981";
constexpr char WRONG_PROTO_URL[] = "rdma://127.0.0.1:9981";
constexpr char BAD_ADDR_URL[] = "tcp://invalid-address";
constexpr uint16_t TEST_URL_PORT = 9981;
} // namespace

class TestMulticastUtils : public testing::Test {
public:
    TestMulticastUtils() = default;
    ~TestMulticastUtils() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestMulticastUtils::SetUp() {}

void TestMulticastUtils::TearDown() {}

TEST_F(TestMulticastUtils, TestParseUrlValidTcpUrl)
{
    std::string ip;
    uint16_t port = 0;
    EXPECT_TRUE(MulticastUtils::ParseUrl(TCP_URL, ip, port));
    EXPECT_EQ(ip, LOOPBACK_IP);
    EXPECT_EQ(port, TEST_URL_PORT);
}

TEST_F(TestMulticastUtils, TestParseUrlWithoutSchema)
{
    std::string ip;
    uint16_t port = 0;
    EXPECT_FALSE(MulticastUtils::ParseUrl(NO_SCHEMA_URL, ip, port));
}

TEST_F(TestMulticastUtils, TestParseUrlWithUnsupportedProtocol)
{
    std::string ip;
    uint16_t port = 0;
    EXPECT_FALSE(MulticastUtils::ParseUrl(WRONG_PROTO_URL, ip, port));
}

TEST_F(TestMulticastUtils, TestParseUrlWithInvalidAddress)
{
    std::string ip;
    uint16_t port = 0;
    EXPECT_FALSE(MulticastUtils::ParseUrl(BAD_ADDR_URL, ip, port));
}

TEST_F(TestMulticastUtils, TestGetFilteredDeviceIpWithLoopbackMask)
{
    // loopback 127.0.0.1 exists on every Linux host, so the mask must match
    EXPECT_EQ(MulticastUtils::GetFilteredDeviceIP(LOOPBACK_MASK), LOOPBACK_IP);
}

TEST_F(TestMulticastUtils, TestGetFilteredDeviceIpWithoutMatch)
{
    // TEST-NET-3 segment never exists on the host
    EXPECT_TRUE(MulticastUtils::GetFilteredDeviceIP(UNMATCHED_MASK).empty());
}

TEST_F(TestMulticastUtils, TestGetFilteredDeviceIpWithInvalidMask)
{
    EXPECT_TRUE(MulticastUtils::GetFilteredDeviceIP(INVALID_MASK).empty());
}

TEST_F(TestMulticastUtils, TestGetFilteredDeviceIpWithEmptyMask)
{
    EXPECT_TRUE(MulticastUtils::GetFilteredDeviceIP("").empty());
}
} // namespace hcom
} // namespace ock
