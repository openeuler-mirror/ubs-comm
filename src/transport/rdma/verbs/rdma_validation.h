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
#ifndef OCK_HCOM_NET_RDMA_VALIDATION_H
#define OCK_HCOM_NET_RDMA_VALIDATION_H
#ifdef RDMA_BUILD_ENABLED

#include "hcom.h"

#include "rdma_composed_endpoint.h"
#include "net_monotonic.h"
#include "net_rdma_driver_oob.h"
#include "net_security_alg.h"
#include "hcom_utils.h"

namespace ock {
namespace hcom {
static __always_inline NResult StateValidate(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverRDMAWithOob *driver)
{
    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(!driver->IsStarted())) {
        NN_LOG_ERROR("Verbs Failed to validate state as driver " << driver << " is not started");
        return NN_ERROR;
    }
    return NN_OK;
}

static __always_inline NResult LocalRequestValidate(const UBSHcomNetTransRequest &request)
{
    if (NN_UNLIKELY(request.lAddress == 0 || request.size == 0)) {
        NN_LOG_ERROR("Failed to validate request as source data is null or size is zero");
        return NN_PARAM_INVALID;
    }
    if (NN_UNLIKELY(request.upCtxSize > sizeof(RDMAOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to validate request as up ctx size invalid " << request.upCtxSize);
        return NN_PARAM_INVALID;
    }
    return NN_OK;
}

static __always_inline NResult SizeValidate(const UBSHcomNetTransRequest &request, uint32_t allowedSize,
    bool mIsNeedEncrypt, AesGcm128 mAes)
{
    size_t compareSize = request.size;
    if (mIsNeedEncrypt) {
        compareSize = mAes.EstimatedEncryptLen(request.size);
    }
    if (NN_UNLIKELY(compareSize > allowedSize)) {
        NN_LOG_ERROR("Failed to post message as message size " << request.size <<
            " is too large, use one side post");
        return NN_TWO_SIDE_MESSAGE_TOO_LARGE;
    }
    return NN_OK;
}

static __always_inline NResult PostSendValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverRDMAWithOob *driver, uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t allowedSize,
    bool mIsNeedEncrypt, AesGcm128 mAes)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY(result = StateValidate(state, id, driver)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(result = LocalRequestValidate(request)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(result = SizeValidate(request, allowedSize, mIsNeedEncrypt, mAes)) != NN_OK) {
        NN_LOG_INFO("res: " << result);
        return result;
    }
    if (NN_UNLIKELY(opCode >= MAX_OPCODE)) {
        NN_LOG_ERROR("Failed to post message as opcode is invalid, which should with the range 0~" << (MAX_OPCODE - 1));
        return NN_INVALID_OPCODE;
    }
    return NN_OK;
}

static __always_inline NResult PostSendRawValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverRDMAWithOob *driver, uint32_t seqNo, const UBSHcomNetTransRequest &request, uint32_t allowedSize,
    bool mIsNeedEncrypt, AesGcm128 mAes)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY(result = StateValidate(state, id, driver)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(result = LocalRequestValidate(request)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(result = SizeValidate(request, allowedSize, mIsNeedEncrypt, mAes)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(seqNo == 0)) {
        NN_LOG_ERROR("Verbs Failed to post raw message as seqNo must > 0");
        return NN_PARAM_INVALID;
    }
    return NN_OK;
}

static __always_inline NResult ReadWriteValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverRDMAWithOob *driver, const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY(result = StateValidate(state, id, driver)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(result = LocalRequestValidate(request)) != NN_OK) {
        return result;
    }
    if (NN_UNLIKELY(request.size > NET_SGE_MAX_SIZE)) {
        NN_LOG_ERROR("Failed to validate request size " << request.size << " as over limit");
        return NN_PARAM_INVALID;
    }
    if (NN_UNLIKELY(request.rAddress == 0)) {
        NN_LOG_ERROR("Failed to validate request as remote data is null");
        return NN_PARAM_INVALID;
    }
    if (NN_OK != driver->ValidateMemoryRegion(request.lKey, request.lAddress, request.size)) {
        NN_LOG_ERROR("Invalid MemoryRegion or local key");
        return NN_INVALID_LKEY;
    }
    return NN_OK;
}

static __always_inline NResult SglValidation(const UBSHcomNetTransSglRequest &request, size_t &totalSize,
    NetDriverRDMAWithOob *driver)
{
    if (NN_UNLIKELY(request.iov == nullptr || request.iovCount > NET_SGE_MAX_IOV || request.iovCount == 0)) {
        NN_LOG_ERROR("Invalid iov ptr:" << request.iov << " or iov cnt:" << request.iovCount);
        return NN_PARAM_INVALID;
    }
    if (NN_UNLIKELY(request.upCtxSize > sizeof(RDMAOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to validate request as up ctx size invalid " << request.upCtxSize);
        return NN_PARAM_INVALID;
    }
    for (uint16_t i = 0; i < request.iovCount; ++i) {
        if (NN_UNLIKELY(request.iov[i].size > NET_SGE_MAX_SIZE)) {
            NN_LOG_ERROR("Failed to validate request size " << request.iov[i].size << " as over limit");
            return NN_PARAM_INVALID;
        }

        if (NN_OK != driver->ValidateMemoryRegion(request.iov[i].lKey, request.iov[i].lAddress,
            request.iov[i].size)) {
            NN_LOG_ERROR("Failed to validate as invalid MemoryRegion or lKey in iov");
            return NN_INVALID_LKEY;
        }
        totalSize += request.iov[i].size;
    }
    return NN_OK;
}

static __always_inline NResult ReadWriteSglValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state,
    uint64_t id, NetDriverRDMAWithOob *driver, const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY(result = StateValidate(state, id, driver)) != NN_OK) {
        return result;
    }
    size_t tmpTotalSize = 0;
    if (NN_UNLIKELY(result = SglValidation(request, tmpTotalSize, driver)) != NN_OK) {
        return result;
    }
    for (uint16_t i = 0; i < request.iovCount; ++i) {
        if (NN_UNLIKELY(request.iov[i].rAddress == NN_NO0)) {
            NN_LOG_ERROR("Failed to validate request as remote data is null, index " << i);
            return NN_PARAM_INVALID;
        }
    }
    return NN_OK;
}

static __always_inline NResult PostSendSglValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverRDMAWithOob *driver, uint32_t seqNo, const UBSHcomNetTransSglRequest &request, uint32_t allowedSize,
    size_t &totalSize, bool mIsNeedEncrypt, AesGcm128 mAes)
{
    NResult ret = NN_OK;
    if (NN_UNLIKELY(ret = StateValidate(state, id, driver)) != NN_OK) {
        return ret;
    }
    if (NN_UNLIKELY(seqNo == 0)) {
        NN_LOG_ERROR("Failed to post raw message as seqNo must > 0");
        return NN_PARAM_INVALID;
    }
    if (NN_UNLIKELY(ret = SglValidation(request, totalSize, driver)) != NN_OK) {
        return ret;
    }
    size_t compareSize = totalSize;
    if (mIsNeedEncrypt) {
        compareSize = mAes.EstimatedEncryptLen(totalSize);
    }
    if (NN_UNLIKELY(compareSize > allowedSize)) {
        NN_LOG_ERROR("Failed to post send raw sgl as message size " << compareSize <<
            " is too large, use one side post");
        return NN_TWO_SIDE_MESSAGE_TOO_LARGE;
    }
    return NN_OK;
}

static __always_inline NResult EncryptRawSgl(UBSHcomNetTransRequest &tlsReq, uintptr_t &mrBufAddress, size_t &size,
    AesGcm128 mAes, NetDriverRDMAWithOob *driver, const UBSHcomNetTransSglRequest &request, NetSecrets &mSecrets)
{
    uintptr_t tmpBuffer = 0;
    if (NN_UNLIKELY(!driver->GetDriverSendMr()->GetFreeBuffer(tmpBuffer))) {
        NN_LOG_ERROR("Failed to post message as failed to get tmp mr buffer from pool from driver " << driver->Name());
        return NN_GET_BUFF_FAILED;
    }

    uint32_t iovOffset = 0;
    for (uint16_t i = 0; i < request.iovCount; i++) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(tmpBuffer + iovOffset),
            driver->GetDriverSendMr()->GetSingleSegSize() - iovOffset,
            reinterpret_cast<const void *>(request.iov[i].lAddress), request.iov[i].size) != NN_OK)) {
            (void)driver->GetDriverSendMr()->ReturnBuffer(tmpBuffer);
            NN_LOG_ERROR("Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
        iovOffset += request.iov[i].size;
    }

    if (NN_UNLIKELY(!driver->GetDriverSendMr()->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post message as failed to get mr buffer from pool from driver " << driver->Name());
        (void)driver->GetDriverSendMr()->ReturnBuffer(tmpBuffer);
        return NN_GET_BUFF_FAILED;
    }

    uint32_t cipherLen = 0;
    if (!(mAes).Encrypt(mSecrets, reinterpret_cast<void *>(tmpBuffer), size, reinterpret_cast<void *>(mrBufAddress),
        cipherLen)) {
        NN_LOG_ERROR("Failed to post send message as encryption failure");
        (void)driver->GetDriverSendMr()->ReturnBuffer(tmpBuffer);
        (void)driver->GetDriverSendMr()->ReturnBuffer(mrBufAddress);
        return NN_ENCRYPT_FAILED;
    }

    tlsReq.lAddress = mrBufAddress;
    tlsReq.lKey = driver->GetDriverSendMr()->GetLKey();
    tlsReq.size = cipherLen;
    size = cipherLen;

    (void)driver->GetDriverSendMr()->ReturnBuffer(tmpBuffer);
    return NN_OK;
}
}
}

#endif
#endif // OCK_HCOM_NET_RDMA_VALIDATION_H