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

#ifndef HCOM_NET_ENDPOINT_IMPL_H
#define HCOM_NET_ENDPOINT_IMPL_H

#include "hcom.h"
#include "net_security_alg.h"

namespace ock {
namespace hcom {

class NetEndpointImpl : public UBSHcomNetEndpoint {
public:
    uint64_t EstimatedEncryptLen(uint64_t rawLen) override;
    NResult Encrypt(const void *rawData, uint64_t rawLen, void *cipher, uint64_t &cipherLen) override;
    uint64_t EstimatedDecryptLen(uint64_t verbsCipherLen) override;
    NResult Decrypt(const void *cipher, uint64_t cipherLen, void *rawData, uint64_t &rawLen) override;

public:
    inline void EnableEncrypt(UBSHcomNetDriverOptions options)
    {
        mIsNeedEncrypt = true;
        mAes.SetEncryptOptions(options.cipherSuite);
    }

    inline void SetSecrets(NetSecrets &verbsSecrets)
    {
        mSecrets = verbsSecrets;
    }

protected:
    NetEndpointImpl(uint64_t id, const UBSHcomNetWorkerIndex &workerWholeIndex)
        : UBSHcomNetEndpoint(id, workerWholeIndex) {}

protected:
    bool mIsNeedEncrypt = false;
    AesGcm128 mAes;
    NetSecrets mSecrets;
};

}
}

#endif // HCOM_NET_ENDPOINT_IMPL_H
