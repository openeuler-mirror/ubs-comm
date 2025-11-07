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

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "net_security_alg.h"
#include "net_security_rand.h"

namespace ock {
namespace hcom {
class TestNetSecurity : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    NetSecrets mSecrets;
    AesGcm128 mAes;

    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;

    unsigned char *mRawData;
    uint32_t mRawLen = NN_NO16;

    unsigned char *mCipher;
    uint32_t mCipherLen = NN_NO60;

    char dummyCtx[1];
    EVP_CIPHER_CTX *ctx;
};

void TestNetSecurity::SetUp()
{
    mAes.SetEncryptOptions(mCipherSuite);
    mCipher = static_cast<unsigned char *>(malloc(mCipherLen));
    mRawData = static_cast<unsigned char *>(malloc(mRawLen));
    ctx = reinterpret_cast<EVP_CIPHER_CTX *>(dummyCtx);
}

void TestNetSecurity::TearDown()
{
    free(mCipher);
    free(mRawData);
    GlobalMockObject::verify();
}

TEST_F(TestNetSecurity, EncryptOpenSSLSuccess)
{
    MOCKER_CPP(&AesGcm128::EncryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Encrypt(mSecrets, mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, EncryptOpenSSLSuccess_AES_CCM_128)
{
    mAes.SetEncryptOptions(AES_CCM_128);

    MOCKER_CPP(&AesGcm128::EncryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Encrypt(mSecrets, mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, EncryptOpenSSLSuccess_CHACHA20_POLY1305)
{
    mAes.SetEncryptOptions(CHACHA20_POLY1305);

    MOCKER_CPP(&AesGcm128::EncryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Encrypt(mSecrets, mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, EncryptWithInvalidParamFail)
{
    const void *keySecrets = nullptr;
    MOCKER_CPP(&NetSecrets::GetKeySecret).stubs().will(returnValue(keySecrets));
    mAes.SetEncryptOptions(mCipherSuite);
    bool ret = mAes.Encrypt(mSecrets, mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetSecurity, EncryptInnerCtxNewFail)
{
    EVP_CIPHER_CTX *ctx = nullptr;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestNetSecurity, EncryptInnerSetEncryptInfoFail)
{
    NResult result = NN_ENCRYPT_FAILED;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetEncryptInfo).stubs().will(returnValue(result));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestNetSecurity, EncryptInnerUpdateFail)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetEncryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpEncryptUpdate).stubs().will(returnValue(-1));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestNetSecurity, EncryptInnerFinalExFail)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetEncryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpEncryptUpdate).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpEncryptFinalEx).stubs().will(returnValue(-1));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestNetSecurity, EncryptInnerCipherCtxCtrlFail)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetEncryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpEncryptUpdate).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpEncryptFinalEx).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxCtrl).stubs().will(returnValue(-1));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_ENCRYPT_FAILED);
}

TEST_F(TestNetSecurity, EncryptInnerSuccess)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetEncryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpEncryptUpdate).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpEncryptFinalEx).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxCtrl).stubs().will(returnValue(1));
    NResult ret = mAes.EncryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()),
        static_cast<const unsigned char *>(mSecrets.GetAADSecret()), mRawData, mRawLen, mCipher, mCipherLen);
    EXPECT_EQ(ret, NN_OK);
}

TEST_F(TestNetSecurity, DecryptOpenSSLSuccess)
{
    MOCKER_CPP(&AesGcm128::DecryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Decrypt(mSecrets, mCipher, mCipherLen, mRawData, mRawLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, DecryptOpenSSLSuccess_AES_CCM_128)
{
    mAes.SetEncryptOptions(AES_CCM_128);

    MOCKER_CPP(&AesGcm128::DecryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Decrypt(mSecrets, mCipher, mCipherLen, mRawData, mRawLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, DecryptOpenSSLSuccess_CHACHA20_POLY1305)
{
    mAes.SetEncryptOptions(CHACHA20_POLY1305);

    MOCKER_CPP(&AesGcm128::DecryptInner).stubs().will(returnValue(0));
    bool ret = mAes.Decrypt(mSecrets, mCipher, mCipherLen, mRawData, mRawLen);
    EXPECT_EQ(ret, true);
}

TEST_F(TestNetSecurity, DecryptWithInvalidParamFail)
{
    const void *keySecrets = nullptr;
    MOCKER_CPP(&NetSecrets::GetKeySecret).stubs().will(returnValue(keySecrets));
    bool ret = mAes.Decrypt(mSecrets, mCipher, mCipherLen, mRawData, mRawLen);
    EXPECT_EQ(ret, false);
}

TEST_F(TestNetSecurity, DecryptInnerCtxNewFail)
{
    EVP_CIPHER_CTX *ctx = nullptr;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    NResult ret = mAes.DecryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()), mCipher, mCipherLen,
        mRawData, mRawLen);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetSecurity, DecryptInnerSetDecryptInfoFail)
{
    NResult result = NN_DECRYPT_FAILED;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetDecryptInfo).stubs().will(returnValue(result));
    NResult ret = mAes.DecryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()), mCipher, mCipherLen,
        mRawData, mRawLen);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetSecurity, DecryptInnerUpdateFail)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetDecryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpDecryptUpdate).stubs().will(returnValue(-1));
    NResult ret = mAes.DecryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()), mCipher, mCipherLen,
        mRawData, mRawLen);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetSecurity, DecryptInnerCipherCtxCtrlFail)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetDecryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpDecryptUpdate).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxCtrl).stubs().will(returnValue(-1));
    NResult ret = mAes.DecryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()), mCipher, mCipherLen,
        mRawData, mRawLen);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetSecurity, DecryptInnerSuccess)
{
    NResult result = NN_OK;
    MOCKER_CPP(&HcomSsl::EvpCipherCtxNew).stubs().will(returnValue(ctx));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxFree).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::SetDecryptInfo).stubs().will(returnValue(result));
    MOCKER_CPP(&HcomSsl::EvpDecryptUpdate).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpDecryptFinalEx).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomSsl::EvpCipherCtxCtrl).stubs().will(returnValue(1));
    NResult ret = mAes.DecryptInner(static_cast<const unsigned char *>(mSecrets.GetKeySecret()), mCipher, mCipherLen,
        mRawData, mRawLen);
    EXPECT_EQ(ret, NN_OK);
}
}
}