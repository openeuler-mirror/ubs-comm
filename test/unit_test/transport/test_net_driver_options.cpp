// Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
// Author: zhiwei

#include <gtest/gtest.h>

#include "hcom.h"

namespace ock {
namespace hcom {

class TestNetDriverOptions : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(TestNetDriverOptions, SetNetDeviceEidFailed)
{
    UBSHcomNetDriverOptions opts;
    EXPECT_FALSE(opts.SetNetDeviceEid("length < 32"));
    EXPECT_FALSE(opts.SetNetDeviceEid("0000:0000:0000:0000:0000:ffff:0102:xxyy"));
}

TEST_F(TestNetDriverOptions, SetNetDeviceEidOk)
{
    UBSHcomNetDriverOptions opts;
    EXPECT_TRUE(opts.SetNetDeviceEid("0000:0000:0000:0000:0000:ffff:0102:0304"));
    EXPECT_EQ(opts.netDeviceEid[0], 0x00);
    EXPECT_EQ(opts.netDeviceEid[9], 0x00);
    EXPECT_EQ(opts.netDeviceEid[10], 0xff);
    EXPECT_EQ(opts.netDeviceEid[11], 0xff);
    EXPECT_EQ(opts.netDeviceEid[12], 0x01);
    EXPECT_EQ(opts.netDeviceEid[13], 0x02);
    EXPECT_EQ(opts.netDeviceEid[14], 0x03);
    EXPECT_EQ(opts.netDeviceEid[15], 0x04);
}

}  // namespace hcom
}  // namespace ock
