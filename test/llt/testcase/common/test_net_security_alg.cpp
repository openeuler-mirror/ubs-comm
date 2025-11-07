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

#include "hcom.h"
#include "common/net_security_alg.h"
#include "common/net_util.h"
#include "test_net_security_alg.hpp"

using namespace ock::hcom;

AesGcm128 mAes;
NetSecrets secrets;

TestNetSecurityAlg::TestNetSecurityAlg() {}

void TestNetSecurityAlg::SetUp()
{
    EXPECT_EQ(HcomSsl::Load(), 0);
    secrets.Init(AES_CCM_128);
}

void TestNetSecurityAlg::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetSecurityAlg, EncryptSuccess)
{
    void *dest = malloc(1024);
    std::string value = "hello";

    uint32_t destLen;
    bool result =
        mAes.Encrypt(secrets, value.c_str(), value.length(), dest, destLen);

    EXPECT_EQ(true, result);
    EXPECT_EQ(mAes.EstimatedEncryptLen(value.length()), destLen);
}

TEST_F(TestNetSecurityAlg, EncryptKeySecretNullFailed)
{
    void *dest = malloc(1024);
    std::string value = "hello";
    const void *keySecrets = nullptr;
    MOCKER_CPP(&NetSecrets::GetKeySecret).stubs().will(returnValue(keySecrets));
    uint32_t destLen;
    bool result = mAes.Encrypt(secrets, value.c_str(), value.length(), dest, destLen);

    EXPECT_EQ(false, result);
}

TEST_F(TestNetSecurityAlg, EncryptAadSecretNullFailed)
{
    void *dest = malloc(1024);
    std::string value = "hello";
    const void *aadSecrets = nullptr;
    MOCKER_CPP(&NetSecrets::GetAADSecret).stubs().will(returnValue(aadSecrets));
    uint32_t destLen;
    bool result = mAes.Encrypt(secrets, value.c_str(), value.length(), dest, destLen);

    EXPECT_EQ(false, result);
}

TEST_F(TestNetSecurityAlg, DecryptCipherLenTooShortFailed)
{
    void *decrypt = malloc(1024);
    std::string cipher = "hello";

    uint32_t decryptRawLen = mAes.GetRawLen(cipher.length());
    bool result = mAes.Decrypt(secrets, cipher.c_str(), cipher.length(), decrypt, decryptRawLen);

    EXPECT_EQ(false, result);
}

TEST_F(TestNetSecurityAlg, DecryptWrongCipherSuccess)
{
    void *decrypt = malloc(1024);
    std::string cipher = "aad iv aes fake cipher of hello world cipher cipher cipher";

    uint32_t decryptRawLen = mAes.GetRawLen(cipher.length());
    bool result = mAes.Decrypt(secrets, cipher.c_str(), cipher.length(), decrypt, decryptRawLen);

    EXPECT_EQ(true, result);
}

TEST_F(TestNetSecurityAlg, DecryptSuccess)
{
    void *dest = malloc(1024);
    std::string value = "hello";

    uint32_t destLen = mAes.EstimatedEncryptLen(value.length());
    bool result =
        mAes.Encrypt(secrets, value.c_str(), value.length(), dest, destLen);

    EXPECT_EQ(true, result);
    EXPECT_EQ(mAes.EstimatedEncryptLen(value.length()), destLen);

    uint32_t decryptRawLen = mAes.GetRawLen(destLen);
    void *decrypt = malloc(decryptRawLen);
    result = mAes.Decrypt(secrets, dest, destLen, decrypt, decryptRawLen);

    EXPECT_EQ(true, result);
    EXPECT_EQ(value.length(), decryptRawLen);
    EXPECT_EQ(0, strncmp(value.c_str(), (char *)decrypt, value.length()));
}