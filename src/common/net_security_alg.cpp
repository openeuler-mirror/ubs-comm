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

#include "net_security_alg.h"
#include "net_security_rand.h"
#include "openssl_api_wrapper.h"

namespace ock {
namespace hcom {
const EVP_CIPHER *GetEvpCipherSuite(UBSHcomNetCipherSuite mCipherSuite)
{
    const EVP_CIPHER *cipherSuite;
    switch (mCipherSuite) {
        case AES_GCM_128:
            cipherSuite = HcomSsl::EvpAes128Gcm();
            break;
        case AES_GCM_256:
            cipherSuite = HcomSsl::EvpAes256Gcm();
            break;
        case AES_CCM_128:
            cipherSuite = HcomSsl::EvpAes128Ccm();
            break;
        case CHACHA20_POLY1305:
            cipherSuite = HcomSsl::EvpChacha20Poly1305();
            break;
        default:
            cipherSuite = HcomSsl::EvpAes128Gcm();
    }
    return cipherSuite;
}

NResult AesGcm128::SetEncryptInfo(EVP_CIPHER_CTX *ctx, const unsigned char *key, unsigned char *cipher)
{
    if (HcomSsl::EvpEncryptInitEx(ctx, GetEvpCipherSuite(mCipherSuite), nullptr, nullptr, nullptr) <= 0) {
        NN_LOG_ERROR("EvpEncryptInitEx() failed");
        return NN_ENCRYPT_FAILED;
    }

    /* Put IV */
    if (!SecurityRandGenerator::SslRand(cipher + mIVOffset, mIVLen)) {
        NN_LOG_ERROR("Generate IV failed");
        return NN_ENCRYPT_FAILED;
    }

    if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_SET_IVLEN, mIVLen, nullptr) <= 0) {
        NN_LOG_ERROR("EvpCipherCtxCtrl() failed");
        return NN_ENCRYPT_FAILED;
    }

    if (mCipherSuite == AES_CCM_128) {
        /* if CipherSuite is AES_CCM_128, need set tag */
        if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_SET_TAG, mTagLen, nullptr) <= 0) {
            NN_LOG_ERROR("Set TAG failed");
            return NN_ENCRYPT_FAILED;
        }
    }

    if (HcomSsl::EvpEncryptInitEx(ctx, nullptr, nullptr, key, cipher + mIVOffset) <= 0) {
        NN_LOG_ERROR("EvpEncryptInitEx() failed");
        return NN_ENCRYPT_FAILED;
    }
    return NN_OK;
}

NResult AesGcm128::EncryptInner(const unsigned char *key, const unsigned char *aad, const unsigned char *rawData,
    uint32_t rawLen, unsigned char *cipher, uint32_t &cipherLen)
{
    EVP_CIPHER_CTX *ctx = HcomSsl::EvpCipherCtxNew();
    if (ctx == nullptr) {
        NN_LOG_ERROR("EvpCipherCtxNew() alloc memory failed!");
        return NN_ENCRYPT_FAILED;
    }

    if (SetEncryptInfo(ctx, key, cipher) != NN_OK) {
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_ENCRYPT_FAILED;
    }

    int outLen = 0;
    if (mCipherSuite == AES_CCM_128) {
        /* if CipherSuite is AES_CCM, set plaintext length: only needed if AAD is used */
        /* AES_GCM and CHACHA20_POLY1305 automatically handle the plaintext length through their internal mechanisms */
        if (HcomSsl::EvpEncryptUpdate(ctx, nullptr, &outLen, nullptr, rawLen) <= 0) {
            NN_LOG_ERROR("EVP_EncryptUpdate() set plaintext length failed");
            goto ERROR_FREE;
        }
    }

    if (memcpy_s(cipher + mAADOffset, mAADLen, aad, mAADLen) != 0) {
        NN_LOG_ERROR("Failed to copy request to mrBufAddress");
        goto ERROR_FREE;
    }

    if (HcomSsl::EvpEncryptUpdate(ctx, nullptr, &outLen, cipher + mAADOffset, mAADLen) <= 0) {
        NN_LOG_ERROR("EvpEncryptUpdate() AAD failed");
        goto ERROR_FREE;
    }

    if (HcomSsl::EvpEncryptUpdate(ctx, cipher + mCipherOffset, &outLen, rawData, rawLen) <= 0) {
        NN_LOG_ERROR("EvpEncryptUpdate() raw data failed");
        goto ERROR_FREE;
    }
    cipherLen = static_cast<uint32_t>(outLen);

    if (HcomSsl::EvpEncryptFinalEx(ctx, cipher + mCipherOffset + outLen, &outLen) <= 0) {
        NN_LOG_ERROR("EvpEncryptFinalEx() raw data failed");
        goto ERROR_FREE;
    }

    /* Final should make outLen to zero */
    if (outLen != 0) {
        NN_LOG_ERROR("EvpEncryptFinalEx() raw data failed as out len should be zero");
        goto ERROR_FREE;
    }

    /* Add the prefix data in cipher format because cipher is same with plain */
    if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_GET_TAG, mTagLen, cipher + mTagOffset) <= 0) {
        NN_LOG_ERROR("Generate TAG failed");
        goto ERROR_FREE;
    }

    cipherLen += static_cast<uint32_t>(mCipherOffset);
    HcomSsl::EvpCipherCtxFree(ctx);
    NN_LOG_TRACE_INFO("Encrypt data rawLen :" << rawLen << " cipherLen: " << cipherLen);
    return NN_OK;
ERROR_FREE:
    HcomSsl::EvpCipherCtxFree(ctx);
    return NN_ENCRYPT_FAILED;
}

NResult AesGcm128::SetDecryptInfo(EVP_CIPHER_CTX *ctx, const unsigned char *key, const unsigned char *cipher)
{
    if (HcomSsl::EvpDecryptInitEx(ctx, GetEvpCipherSuite(mCipherSuite), nullptr, nullptr, nullptr) <= 0) {
        NN_LOG_ERROR("EvpDecryptInitEx() failed");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_ENCRYPT_FAILED;
    }

    if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_SET_IVLEN, mIVLen, nullptr) <= 0) {
        NN_LOG_ERROR("Set IV length failed");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_ENCRYPT_FAILED;
    }

    if (mCipherSuite == AES_CCM_128) {
        /* if CipherSuite is AES_CCM_128, need set tag */
        if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_SET_TAG, mTagLen,
            const_cast<unsigned char *>(cipher + mTagOffset)) <= 0) {
            NN_LOG_ERROR("Set TAG failed");
            HcomSsl::EvpCipherCtxFree(ctx);
            return NN_ENCRYPT_FAILED;
        }
    }

    if (HcomSsl::EvpDecryptInitEx(ctx, nullptr, nullptr, key, cipher + mIVOffset) <= 0) {
        NN_LOG_ERROR("EvpDecryptInitEx() failed");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_ENCRYPT_FAILED;
    }
    return NN_OK;
}

NResult AesGcm128::DecryptInner(const unsigned char *key, const unsigned char *cipher, uint32_t cipherLen,
    unsigned char *rawData, uint32_t &rawLen)
{
    EVP_CIPHER_CTX *ctx = HcomSsl::EvpCipherCtxNew();
    if (ctx == nullptr) {
        NN_LOG_ERROR("EvpCipherCtxNew() alloc memory failed!");
        return NN_DECRYPT_FAILED;
    }

    if (SetDecryptInfo(ctx, key, cipher) != NN_OK) {
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_DECRYPT_FAILED;
    }

    int outLen = 0;
    if (mCipherSuite == AES_CCM_128) {
        /* if CipherSuite is AES_CCM_128, set cipher length: only needed if AAD is used */
        if (HcomSsl::EvpDecryptUpdate(ctx, nullptr, &outLen, nullptr, cipherLen - mCipherOffset) <= 0) {
            NN_LOG_ERROR("EvpDecryptUpdate() set cipher length failed");
            HcomSsl::EvpCipherCtxFree(ctx);
            return NN_DECRYPT_FAILED;
        }
    }

    if (HcomSsl::EvpDecryptUpdate(ctx, nullptr, &outLen, cipher + mAADOffset, mAADLen) <= 0) {
        NN_LOG_ERROR("EvpDecryptUpdate() AAD failed");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_DECRYPT_FAILED;
    }

    if (HcomSsl::EvpDecryptUpdate(ctx, rawData, &outLen, cipher + mCipherOffset, cipherLen - mCipherOffset) <= 0) {
        NN_LOG_ERROR("EvpDecryptUpdate() cipher data failed");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_DECRYPT_FAILED;
    }
    rawLen = static_cast<uint32_t>(outLen);

    if (mCipherSuite != AES_CCM_128) {
        if (HcomSsl::EvpCipherCtxCtrl(ctx, HcomSsl::EVP_CTRL_AEAD_SET_TAG, mTagLen,
            const_cast<unsigned char *>(cipher + mTagOffset)) <= 0) {
            NN_LOG_ERROR("Set TAG failed");
            HcomSsl::EvpCipherCtxFree(ctx);
            return NN_DECRYPT_FAILED;
        }
    }

    /* If don't check TAG, the EvpDecryptFinalEx() will always return 0, ignore that error */
    if (HcomSsl::EvpDecryptFinalEx(ctx, rawData, &outLen) <= 0) {
        NN_LOG_WARN("EvpDecryptFinalEx() cipher data unfinished");
    }

    /* DecryptFinal should make it to zero */
    if (outLen != 0) {
        NN_LOG_WARN("EvpDecryptFinalEx() cipher data failed as outLen is zero");
        HcomSsl::EvpCipherCtxFree(ctx);
        return NN_DECRYPT_FAILED;
    }

    HcomSsl::EvpCipherCtxFree(ctx);
    NN_LOG_TRACE_INFO("Decrypt data rawLen :" << rawLen << " cipherLen: " << cipherLen);
    return NN_OK;
}
}
}