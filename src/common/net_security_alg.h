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

#ifndef OCK_HCOM_SECURITY_ALG_H
#define OCK_HCOM_SECURITY_ALG_H

#include "net_common.h"
#include "net_security_rand.h"
#include "openssl_api_wrapper.h"

namespace ock {
namespace hcom {
class AesGcm128 {
public:
    /*
     * @brief Encrypt @b rawData to @b cipher, default use AES-GCM-128
     *
     * @param secret   : the secret for encrypt
     * @param rawData : the raw data for encrypt
     * @param rawLen  : the length of raw data
     * @param cipher  : the cipher data
     * @param cipherLen : the length of cipher data
     *
     * @return true for success
     * @note : user can call EstimatedEncryptLen() for @b cipher memory length allocating,
     * and should make sure @b cipher is enough for write
     */
    bool Encrypt(NetSecrets &secrets, const void *rawData, uint32_t rawLen, void *cipher, uint32_t &cipherLen)
    {
        if (NN_UNLIKELY(secrets.GetKeySecret() == nullptr) || NN_UNLIKELY(rawData == nullptr) ||
            NN_UNLIKELY(rawLen == 0) || NN_UNLIKELY(cipher == nullptr)) {
            NN_LOG_ERROR("Failed to encrypt as invalid params.");
            return false;
        }

        int32_t ret;
        uint32_t estimateLen = EstimatedEncryptLen(rawLen);
        cipherLen = estimateLen;

        // openssl api the type of len is int
        if (NN_UNLIKELY(rawLen > INT_MAX)) {
            NN_LOG_ERROR("invalid rawLen " << rawLen);
            return false;
        }
        ret = EncryptInner(static_cast<const unsigned char *>(secrets.GetKeySecret()),
            static_cast<const unsigned char *>(secrets.GetAADSecret()), static_cast<const unsigned char *>(rawData),
            rawLen, static_cast<unsigned char *>(cipher), cipherLen);
        if (NN_UNLIKELY(ret != 0) || NN_UNLIKELY(cipherLen != estimateLen)) {
            NN_LOG_ERROR("Failed to encrypt as ret:" << ret << " cipher length:" << cipherLen <<
                " estimateLen length:" << estimateLen);
            return false;
        }
        return true;
    }

    NResult EncryptInner(const unsigned char *key, const unsigned char *aad, const unsigned char *rawData,
        uint32_t rawLen, unsigned char *cipher, uint32_t &cipherLen);

    NResult SetEncryptInfo(EVP_CIPHER_CTX *ctx, const unsigned char *key, unsigned char *cipher);
    /*
     * @brief Decrypt @b cipher to @b raw, default use AES-GCM-128
     *
     * @param key     : the private-key for decrypt, length is @b mKeyLen
     * @param cipher  : the cipher data
     * @param cipherLen : the length of cipher data
     * @param rawData : the raw data for encrypt
     * @param rawLen  : the length of raw data
     *
     * @return true for success
     * @note : user should make sure @b rawData is enough for write
     */
    bool Decrypt(NetSecrets &secrets, const void *cipher, uint32_t cipherLen, void *rawData, uint32_t &rawLen)
    {
        if (NN_UNLIKELY(secrets.GetKeySecret() == nullptr) || NN_UNLIKELY(cipher == nullptr) ||
            NN_UNLIKELY(cipherLen == 0) || NN_UNLIKELY(rawData == nullptr)) {
            NN_LOG_ERROR("Invalid params");
            return false;
        }

        int32_t ret;
        uint32_t estimateLen = GetRawLen(cipherLen);
        rawLen = estimateLen;

        // openssl api the type of len is int
        if (NN_UNLIKELY(cipherLen <= mCipherOffset) || NN_UNLIKELY(cipherLen - mCipherOffset > INT_MAX)) {
            NN_LOG_ERROR("invalid cipherLen " << cipherLen);
            return false;
        }
        ret = DecryptInner(static_cast<const unsigned char *>(secrets.GetKeySecret()),
            static_cast<const unsigned char *>(cipher), cipherLen, static_cast<unsigned char *>(rawData), rawLen);
        if (NN_UNLIKELY(ret != 0) || NN_UNLIKELY(rawLen != estimateLen)) {
            NN_LOG_ERROR("Failed to decrypt as ret:" << ret << " raw length:" << rawLen <<
                " estimateLen length:" << estimateLen);
            return false;
        }
        return true;
    }

    NResult DecryptInner(const unsigned char *key, const unsigned char *cipher, uint32_t cipherLen,
        unsigned char *rawData, uint32_t &rawLen);

    NResult SetDecryptInfo(EVP_CIPHER_CTX *ctx, const unsigned char *key, const unsigned char *cipher);

    /*
     * @brief Estimated the cipher length, it will be greater than or equal to real cipher length
     */
    inline uint32_t EstimatedEncryptLen(uint32_t rawLen) const
    {
        auto cipherLen = static_cast<uint32_t>(mCipherOffset);
        if (NN_UNLIKELY(rawLen == 0 || rawLen > UINT32_MAX - cipherLen)) {
            NN_LOG_ERROR("Failed to estimate ep encrypt raw length invalid");
            return 0;
        }
        cipherLen += rawLen;
        return cipherLen;
    }

    inline uint32_t GetRawLen(uint32_t cipherLen) const
    {
        if (cipherLen <= static_cast<uint32_t>(mCipherOffset)) {
            return 0;
        }
        return cipherLen - static_cast<uint32_t>(mCipherOffset);
    }

    inline void SetEncryptOptions(UBSHcomNetCipherSuite cipherSuite)
    {
        mCipherSuite = cipherSuite;
        if (cipherSuite == AES_GCM_128 || cipherSuite == AES_CCM_128) {
            mKeyLen = NN_NO16;
        } else if (cipherSuite == AES_GCM_256 || cipherSuite == CHACHA20_POLY1305) {
            mKeyLen = NN_NO32;
        } else {
            NN_LOG_WARN("Invalid to set encrypt options, because unknown cipher suite, use default one.");
        }
    }

private:
    UBSHcomNetCipherSuite mCipherSuite = AES_GCM_128;
    /*
     * cipher data format :|IV||AAD (opt)|TAG(opt)|CIPHER DATA|
     * ------------ Bytes :|12 | 16      | 16     |      ?    |
     * ------------------- |-> mIVOffset          |-> mCipherOffset
     * ----------------------- |-> mAADOffset
     * --------------------------------- |-> mTagOffset
     *
     * default dont use AAD and TAG, so it is same
     */

    /* Key use 32Bytes cipher len 256, 16Bytes cipher len 128 */
    int mKeyLen = NN_NO16;
    /* IV use 12Bytes(96bits) */
    const int mIVLen = NN_NO12;
    /* AAD use 16Bytes(128bits) */
    const int mAADLen = NN_NO16;
    /* Tag use 16Bytes(128bits) */
    const int mTagLen = NN_NO16;

    off_t mIVOffset = 0;
    off_t mAADOffset = mIVOffset + mIVLen;
    off_t mTagOffset = mAADOffset + mAADLen;
    off_t mCipherOffset = mTagOffset + mTagLen;
};
}
}

#endif