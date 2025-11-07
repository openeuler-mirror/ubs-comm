/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "test_net_crc32.h"
#include "hcom_def.h"
#include "net_crc32.h"

using namespace ock::hcom;
TestCaseNetCrc32::TestCaseNetCrc32() {}

void TestCaseNetCrc32::SetUp() {}

void TestCaseNetCrc32::TearDown() {}

TEST_F(TestCaseNetCrc32, TestSameString)
{
    std::string buff = "abcdefghijklnmopqrstuvwxyz";
    auto crc1 = NetCrc32::CalcCrc32(buff.data(), buff.size());
    auto crc2 = NetCrc32::CalcCrc32(buff.data(), buff.size());

    EXPECT_EQ(crc1, crc2);
}

TEST_F(TestCaseNetCrc32, TestDifferentString)
{
    std::string buff1 = "abcdefghijklnmopqrstuvwxyz0123456789";
    std::string buff2 = "abcdefghijklnmopqrstuvwxyz";
    auto crc1 = NetCrc32::CalcCrc32(buff1.data(), buff1.size());
    auto crc2 = NetCrc32::CalcCrc32(buff2.data(), buff2.size());

    EXPECT_NE(crc1, crc2);
}
