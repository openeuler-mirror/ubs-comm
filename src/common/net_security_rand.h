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

#ifndef OCK_HCOM_SECURITY_RAND_H
#define OCK_HCOM_SECURITY_RAND_H

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>

#include "securec.h"
#include "openssl_api_wrapper.h"

namespace ock {
namespace hcom {
/*
 * Because OpenSSL will reseed by itself, so there
 * we don't reseed it by manual
 */
class SecurityRandGenerator {
public:
    /*
     * @brief generate @b length random data to @b out
     */
    static bool SslRand(void *out, size_t length)
    {
        int ret;
        auto outBuf = static_cast<unsigned char *>(out);
        const uint16_t maxTryCount = NN_NO1000;
        uint16_t tryCount = 0;

        if (out == nullptr || length == 0) {
            return false;
        }

        while (tryCount <= maxTryCount) {
            Poll();
            tryCount++;

            ret = HcomSsl::RandPrivBytes(outBuf, length);
            if (ret <= 0) {
                NN_LOG_TRACE_INFO("Failed to generate secure rand, result " << ret);
                continue;
            } else {
                NN_LOG_TRACE_INFO("Successfully generate secure random num, length: " << length);
                return true;
            }
        }

        NN_LOG_ERROR("Failed to generate secure rand after tried " << tryCount << " times");
        return false;
    }

private:
    /*
     * @brief RandPoll() to get enough rand
     */
    static void Poll()
    {
        const uint16_t maxPollCount = NN_NO1000;
        uint16_t pollCount = 0;

        while (HcomSsl::RandStatus() <= 0 && pollCount < maxPollCount) {
            pollCount++;
            NN_LOG_TRACE_INFO("Rand start to poll");
            HcomSsl::RandPoll();
        }
    }
};

class NetSecrets {
public:
    NetSecrets() = default;
    ~NetSecrets()
    {
        bzero(mKeySecret, NN_NO32 * sizeof(char));
        bzero(mAADSecret, NN_NO32 * sizeof(char));
        bzero(mIVSecret, NN_NO32 * sizeof(char));
    };

    inline const void *GetKeySecret() const
    {
        return mKeySecret;
    }

    inline const void *GetAADSecret() const
    {
        return mAADSecret;
    }

    inline const void *GetIVSecret() const
    {
        return mIVSecret;
    }

    bool Init(UBSHcomNetCipherSuite cipherSuite)
    {
        if (cipherSuite == AES_GCM_128 || cipherSuite == AES_CCM_128) {
            mKeySecretLen = NN_NO16;
        } else if (cipherSuite == AES_GCM_256 || cipherSuite == CHACHA20_POLY1305) {
            mKeySecretLen = NN_NO32;
        } else {
            NN_LOG_ERROR("Failed to init secret, because unknown cipher suite.");
            return false;
        }

        return InitSSLRandSecret();
    }
    /*
     * @brief update the secret, generate new sn and new secert
     *
     * because now RAND is use static method, so it call SslRand() iternal
     *
     * @return true for success, false for failed
     */
    bool InitSSLRandSecret()
    {
        mAADSecretLen = NN_NO16;
        mIVSecretLen = NN_NO12;

        if (!SecurityRandGenerator::SslRand(mKeySecret, mKeySecretLen)) {
            NN_LOG_WARN("Update keySecret failed");
            return false;
        }

        if (!SecurityRandGenerator::SslRand(mAADSecret, mAADSecretLen)) {
            NN_LOG_WARN("Update mAADSecret failed");
            return false;
        }

        if (!SecurityRandGenerator::SslRand(mIVSecret, mIVSecretLen)) {
            NN_LOG_WARN("Update mIVSecret failed");
            return false;
        }
        return true;
    }

    /*
     * @brief Format:|SN| |KeySecret| |AADSecret| |IVSecret|
     *
     * Bytes:  mSN
     * mKeySecretLen
     * mAADSecrete
     * mIVSecret
     */
    inline size_t GetSerializeLen() const
    {
        return sizeof(uint8_t) + mKeySecretLen + mAADSecretLen + mIVSecretLen;
    }

    inline bool Serialize(char *dest, size_t len) const
    {
        if (NN_UNLIKELY(dest == nullptr) || NN_UNLIKELY(len != GetSerializeLen())) {
            NN_LOG_ERROR("Invalid param secret is null or length:" << len << " is not equal to serialized len:" <<
                GetSerializeLen());
            return false;
        }

        if (memcpy_s(dest, sizeof(uint8_t), &mSN, sizeof(uint8_t)) != EOK) {
            NN_LOG_ERROR("memcpy_s sn failed.");
            return false;
        }

        if (memcpy_s(dest + sizeof(uint8_t), mKeySecretLen, mKeySecret, mKeySecretLen) != EOK) {
            NN_LOG_ERROR("memcpy_s key failed.");
            return false;
        }

        if (memcpy_s(dest + sizeof(uint8_t) + mKeySecretLen, mAADSecretLen, mAADSecret, mAADSecretLen) != EOK) {
            NN_LOG_ERROR("memcpy_s aad failed.");
            return false;
        }

        if (memcpy_s(dest + sizeof(uint8_t) + mKeySecretLen + mAADSecretLen, mIVSecretLen, mIVSecret, mIVSecretLen) !=
            EOK) {
            NN_LOG_ERROR("memcpy_s iv failed.");
            return false;
        }

        return true;
    }

    inline bool Deserialize(const char *secret, size_t len)
    {
        if (NN_UNLIKELY(secret == nullptr) || NN_UNLIKELY(len != GetSerializeLen())) {
            NN_LOG_ERROR("Invalid param secret is null or length:" << len << " is not equal to serialized len:" <<
                GetSerializeLen());
            return false;
        }

        if (memcpy_s(mKeySecret, mKeySecretLen, secret + sizeof(uint8_t), mKeySecretLen) != EOK) {
            NN_LOG_ERROR("memcpy_s key failed.");
            return false;
        }

        if (memcpy_s(mAADSecret, mAADSecretLen, secret + sizeof(uint8_t) + mKeySecretLen, mAADSecretLen) != EOK) {
            NN_LOG_ERROR("memcpy_s aad failed.");
            return false;
        }

        if (memcpy_s(mIVSecret, mIVSecretLen, secret + sizeof(uint8_t) + mKeySecretLen + mAADSecretLen, mIVSecretLen) !=
            EOK) {
            NN_LOG_ERROR("memcpy_s iv failed.");
            return false;
        }

        return true;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    // reserve for secret time out
    uint8_t mSN = { 0 };

    char mKeySecret[NN_NO32] = { 0 } ;
    char mAADSecret[NN_NO32] = { 0 } ;
    char mIVSecret[NN_NO32] = { 0 } ;

    /* the real secret length */
    size_t mKeySecretLen = NN_NO0;
    size_t mAADSecretLen = NN_NO0;
    size_t mIVSecretLen = NN_NO0;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif
