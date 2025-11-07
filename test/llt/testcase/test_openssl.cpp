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
#include <dlfcn.h>
#include <unistd.h>
#include "hcom.h"
#include "openssl_api_wrapper.h"
#include "test_openssl.h"

using namespace ock::hcom;
TestOpenSsl::TestOpenSsl() {}

void TestOpenSsl::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestOpenSsl::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestOpenSsl, OpenSslLoadError)
{
    int result = 0;
    void *ptr = nullptr;
    MOCKER(dlsym).stubs().will(returnValue(ptr));
    result = HcomSsl::Load();
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError1)
{
    int result = 0;
    void *ptr = nullptr;
    MOCKER(dlopen).stubs().will(returnValue(ptr));
    result = HcomSsl::Load();
    EXPECT_EQ(-1, result);
}

int openSize = 64;
int times = 1;
TEST_F(TestOpenSsl, OpenSslLoadError2)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(returnValue(ptr1)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError3)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError4)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError5)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError6)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError7)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError8)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError9)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError10)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError11)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError12)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError13)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError14)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError15)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError16)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError17)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError18)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError19)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError20)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError21)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError22)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError23)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError24)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError25)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError26)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError27)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError28)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError29)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError30)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError31)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError32)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError33)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError34)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError35)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError36)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError37)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError38)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError39)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError40)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError41)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError42)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError43)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError44)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError45)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError46)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError47)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError48)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError49)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError50)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError51)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError52)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError53)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError54)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError55)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}

TEST_F(TestOpenSsl, OpenSslLoadError56)
{
    int result = 0;
    void *ptr = nullptr;
    void *ptr1 = malloc(openSize);
    MOCKER(dlsym).stubs().will(repeat(ptr1, ++times)).then(returnValue(ptr));
    result = HcomSsl::Load();
    free(ptr1);
    EXPECT_EQ(-1, result);
}