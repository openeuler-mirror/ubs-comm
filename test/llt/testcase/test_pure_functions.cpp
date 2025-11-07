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
#include "test_pure_functions.h"


TestPureFunctions::TestPureFunctions() {}

void TestPureFunctions::SetUp() {}

void TestPureFunctions::TearDown() {}

#if 0
#include "hcom_securec.h"
using namespace ock::hcom;
TEST_F(TestPureFunctions, Memcpy_s)
{
    auto src = (int *)malloc(sizeof(int) * 10);
    for (int i = 0; i < 10; ++i) {
        src[i] = i;
    }
    auto dst = (int *)malloc(sizeof(int) * 8);
    bzero(dst, sizeof(int) * 8);
    for (int i = 0; i < 8; ++i) {
        dst[i] = 11;
    }

    auto ret = memcpy_s(nullptr, 0, nullptr, 0);
    EXPECT_EQ(ret, SEC_ERANGE);

    ret = memcpy_s(nullptr, 0x7fffffffUL + 1, nullptr, 0);
    EXPECT_EQ(ret, SEC_ERANGE);

    ret = memcpy_s(dst, 0x7fffffffUL + 1, src, 0);
    EXPECT_EQ(ret, SEC_ERANGE);
    EXPECT_EQ(dst[0], 11);

    ret = memcpy_s(nullptr, 1, src, 0);
    EXPECT_EQ(ret, SEC_EINVAL);

    ret = memcpy_s(dst, sizeof(int), nullptr, 0);
    EXPECT_EQ(ret, SEC_EINVAL_AND_RESET);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 11);

    dst[0] = 11;
    ret = memcpy_s(dst, sizeof(int), src, sizeof(int) * 2);
    EXPECT_EQ(ret, SEC_ERANGE_AND_RESET);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 11);

    dst[0] = 11;
    ret = memcpy_s(dst, sizeof(int) * 2, dst + 1, sizeof(int) * 2);
    EXPECT_EQ(ret, SEC_EOVERLAP_AND_RESET);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[1], 0);

    dst[0] = 11;
    dst[1] = 11;
    ret = memcpy_s(dst, sizeof(int) * 8, src, sizeof(int) * 4);
    EXPECT_EQ(ret, EOK);
    EXPECT_EQ(dst[0], 0);
    EXPECT_EQ(dst[3], 3);
    EXPECT_EQ(dst[4], 11);
}

TEST_F(TestPureFunctions, Strcpy_s)
{
    char src[8] = "abcdefg";
    char dst[10] = "zzzzzzzzz";
    auto result = strcpy_s(nullptr, 0, nullptr);
    EXPECT_EQ(result, SEC_ERANGE);
    result = strcpy_s(dst, 0, src);
    EXPECT_EQ(result, SEC_ERANGE);
    result = strcpy_s(nullptr, 1, nullptr);
    EXPECT_EQ(result, SEC_EINVAL);
    result = strcpy_s(nullptr, 1, src);
    EXPECT_EQ(result, SEC_EINVAL);
    result = strcpy_s(dst, 8, nullptr);
    EXPECT_EQ(result, SEC_EINVAL_AND_RESET);
    EXPECT_EQ(dst[0], '\0');
    EXPECT_EQ(dst[1], 'z');
    dst[0] = 'z';
    result = strcpy_s(dst, 7, src);
    EXPECT_EQ(result, SEC_ERANGE_AND_RESET);
    EXPECT_EQ(dst[0], '\0');
    EXPECT_EQ(dst[1], 'z');
    dst[0] = 'z';
    result = strcpy_s(dst, 6, &(dst[4]));
    EXPECT_EQ(result, SEC_EOVERLAP_AND_RESET);
    EXPECT_EQ(dst[0], '\0');
    EXPECT_EQ(dst[1], 'z');
    dst[0] = 'z';
    result = strcpy_s(dst, 8, src);
    EXPECT_EQ(result, EOK);
    EXPECT_EQ(dst[0], 'a');
    EXPECT_EQ(dst[6], 'g');
    EXPECT_EQ(dst[7], '\0');
}
#endif