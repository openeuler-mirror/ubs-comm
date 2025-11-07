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
#ifndef OCK_HCOM_NET_SHM_VALIDATION_H
#define OCK_HCOM_NET_SHM_VALIDATION_H
#ifdef SHM_BUILD_ENABLED

#include "hcom.h"
#include "hcom_utils.h"
#include "net_common.h"
#include "net_monotonic.h"
#include "net_security_alg.h"
#include "net_shm_common.h"
#include "net_shm_driver_oob.h"


namespace ock {
namespace hcom {
#define VALIDATE_ENCRYPT_LENGTH(encryptLen, calLen, mShmCh, address)                                                  \
    if (NN_UNLIKELY((encryptLen) != (calLen))) {                                                                      \
        NN_LOG_ERROR("Failed to encrypt data as encrypt length " << (encryptLen) << " is not equal to cal length " << \
            (calLen));                                                                                                \
        (mShmCh)->DCMarkBuckFree((address));                                                                          \
        return NN_ENCRYPT_FAILED;                                                                                     \
    }

#define VALIDATE_DECRYPT_LENGTH(decryptLen, calLen, opCtx)                                                            \
    if (NN_UNLIKELY((decryptLen) != (calLen))) {                                                                      \
        NN_LOG_ERROR("Failed to decrypt data as decrypt length " << (decryptLen) << " is not equal to cal length " << \
            (calLen));                                                                                                \
        (opCtx).channel->DCMarkPeerBuckFree((opCtx).dataAddress);                                                     \
        return NN_DECRYPT_FAILED;                                                                                     \
    }

static __always_inline NResult PostSendValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    uint16_t opCode, const UBSHcomNetTransRequest &request)
{
    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }
    if (NN_UNLIKELY(opCode >= MAX_OPCODE)) {
        NN_LOG_ERROR("Failed to post message as opcode is invalid, which should with the range 0~" << (MAX_OPCODE - 1));
        return NN_INVALID_OPCODE;
    }
    if (NN_UNLIKELY(request.lAddress == 0 || request.size == 0)) {
        NN_LOG_ERROR("Failed to post message as source data is null or size is zero");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

static __always_inline NResult PostSendRawValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    const UBSHcomNetTransRequest &request)
{
    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(request.lAddress == 0 || request.size == 0)) {
        NN_LOG_ERROR("Failed to post message as source data is null or size is zero");
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

static __always_inline NResult PostSendValidationMaxSize(const UBSHcomNetTransRequest &request, uint32_t allowedSize,
    bool mIsNeedEncrypt, AesGcm128 mAes)
{
    size_t size = request.size;
    if (mIsNeedEncrypt) {
        size = mAes.EstimatedEncryptLen(request.size);
    }
    if (NN_UNLIKELY(size > allowedSize)) {
        NN_LOG_ERROR("Failed to post message as message size " << size << " is too large, use one side post");
        return NN_TWO_SIDE_MESSAGE_TOO_LARGE;
    }
    return NN_OK;
}

static __always_inline NResult PostSendSglValidationInner(uint64_t &size, const UBSHcomNetTransSglRequest &request,
    NetDriverShmWithOOB *driver, uint32_t allowedSize, bool mIsNeedEncrypt, AesGcm128 mAes)
{
    for (uint16_t i = 0; i < request.iovCount; ++i) {
        auto &&iov = request.iov[i];
        if (NN_OK != driver->ValidateMemoryRegion(iov.lKey, iov.lAddress, iov.size)) {
            NN_LOG_ERROR("Invalid MemoryRegion or lkey in iov");
            return NN_INVALID_LKEY;
        }
        size += iov.size;
    }

    if (mIsNeedEncrypt) {
        size = mAes.EstimatedEncryptLen(size);
        NN_LOG_INFO("size after encrypt is " << size << " allowedSize is " << allowedSize);
    }

    if (NN_UNLIKELY(size > allowedSize)) {
        NN_LOG_ERROR("Failed to post raw sgl message as size " << size << " is too large, use one side instead");
        return NN_TWO_SIDE_MESSAGE_TOO_LARGE;
    }
    return NN_OK;
}

static __always_inline NResult PostSendSglValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverShmWithOOB *driver, uint32_t seqNo, const UBSHcomNetTransSglRequest &request, uint32_t allowedSize,
    bool mIsNeedEncrypt, AesGcm128 mAes)
{
    if (NN_UNLIKELY(request.iov == nullptr || request.iovCount > NET_SGE_MAX_IOV || request.iovCount == 0)) {
        NN_LOG_ERROR("Invalid iov ptr:" << request.iov << " or iov cnt:" << request.iovCount);
        return NN_PARAM_INVALID;
    }

    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(seqNo == 0)) {
        NN_LOG_ERROR("Failed to post raw sgl message as seqNo must > 0");
        return NN_INVALID_PARAM;
    }
    
    uint64_t size = 0;
    if (NN_UNLIKELY(PostSendSglValidationInner(size, request, driver, allowedSize, mIsNeedEncrypt, mAes) != NN_OK)) {
        return NN_INVALID_PARAM;
    }
    return NN_OK;
}

static __always_inline NResult ReadWriteValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state, uint64_t id,
    NetDriverShmWithOOB *driver, ShmChannel *shmCh, const UBSHcomNetTransRequest &request)
{
    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(shmCh == nullptr || driver == nullptr)) {
        NN_LOG_ERROR("Invalid endpoint");
        return NN_ERROR;
    }

    if (NN_OK != driver->ValidateMemoryRegion(request.lKey, request.lAddress, request.size)) {
        NN_LOG_ERROR("Invalid MemoryRegion or lkey");
        return NN_INVALID_LKEY;
    }
    return NN_OK;
}

static __always_inline NResult PostReadWriteSglValidation(UBSHcomNetAtomicState<UBSHcomNetEndPointState> &state,
    uint32_t id, NetDriverShmWithOOB *driver, ShmChannel *shmCh, const UBSHcomNetTransSglRequest &request)
{
    if (NN_UNLIKELY(request.iov == nullptr || request.iovCount > NET_SGE_MAX_IOV || request.iovCount == 0)) {
        NN_LOG_ERROR("Invalid iov ptr: " << request.iov << " or iov cnt: " << request.iovCount);
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!state.Compare(NEP_ESTABLISHED))) {
        NN_LOG_ERROR("Endpoint " << id << " is not established, state is " << UBSHcomNEPStateToString(state.Get()));
        return NN_EP_NOT_ESTABLISHED;
    }

    if (NN_UNLIKELY(shmCh == nullptr || driver == nullptr)) {
        NN_LOG_ERROR("Invalid endpoint");
        return NN_ERROR;
    }

    auto iovCount = request.iovCount;
    for (auto i = 0; i < iovCount; i++) {
        auto iov = request.iov[i];
        if (NN_OK != driver->ValidateMemoryRegion(iov.lKey, iov.lAddress, iov.size)) {
            NN_LOG_ERROR("Invalid MemoryRegion or lkey");
            return NN_INVALID_LKEY;
        }
    }
    return NN_OK;
}
}
}
#endif
#endif // OCK_HCOM_NET_SHM_VALIDATION_H
