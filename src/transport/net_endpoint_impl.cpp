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

#include "transport/net_endpoint_impl.h"

namespace ock {
namespace hcom {

uint64_t NetEndpointImpl::EstimatedEncryptLen(uint64_t rawLen)
{
    if (NN_UNLIKELY(rawLen == 0)) {
        NN_LOG_ERROR("Failed to estimate encrypt length as input length is 0");
        return 0;
    }

    if (NN_UNLIKELY(!mIsNeedEncrypt)) {
        NN_LOG_ERROR("Failed to estimate encrypt length as options of encrypt is not enabled");
        return 0;
    }

    return mAes.EstimatedEncryptLen(rawLen);
}

NResult NetEndpointImpl::Encrypt(const void *rawData, uint64_t rawLen, void *cipher, uint64_t &cipherLen)
{
    if (NN_UNLIKELY(!mIsNeedEncrypt) || NN_UNLIKELY(rawLen > UINT32_MAX) || NN_UNLIKELY(cipherLen > UINT32_MAX)) {
        NN_LOG_ERROR("Failed to encrypt, options of encrypt is not enabled or len over uint32_max");
        return NN_ERROR;
    }

    if (NN_UNLIKELY(!mAes.Encrypt(mSecrets, rawData, rawLen, cipher, reinterpret_cast<uint32_t &>(cipherLen)))) {
        return NN_ERROR;
    }
    return NN_OK;
}

uint64_t NetEndpointImpl::EstimatedDecryptLen(uint64_t verbsCipherLen)
{
    if (NN_UNLIKELY(!mIsNeedEncrypt)) {
        NN_LOG_ERROR("Failed to estimate decrypt length as options of encrypt is not enabled");
        return 0;
    }

    return mAes.GetRawLen(verbsCipherLen);
}

NResult NetEndpointImpl::Decrypt(const void *cipher, uint64_t cipherLen, void *rawData, uint64_t &rawLen)
{
    if (NN_UNLIKELY(!mIsNeedEncrypt) || NN_UNLIKELY(rawLen > UINT32_MAX) || NN_UNLIKELY(cipherLen > UINT32_MAX)) {
        NN_LOG_ERROR("Failed to decrypt, options of decrypt not enabled or len over uint32_max");
        return NN_ERROR;
    }

    if (NN_UNLIKELY(!mAes.Decrypt(mSecrets, cipher, cipherLen, rawData, reinterpret_cast<uint32_t &>(rawLen)))) {
        return NN_ERROR;
    }
    return NN_OK;
}

}
}
