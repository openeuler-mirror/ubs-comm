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
#include "shm_validation.h"
#include "hcom_log.h"
#include "net_shm_async_endpoint.h"

namespace ock {
namespace hcom {
NetAsyncEndpointShm::NetAsyncEndpointShm(uint64_t id, ShmChannel *ch, ShmWorker *worker, NetDriverShmWithOOB *driver,
                                         const UBSHcomNetWorkerIndex &workerIndex, ShmMRHandleMap &handleMap)
    : NetEndpointImpl(id, workerIndex),
      mShmCh(ch),
      mWorker(worker),
      mDriver(driver),
      mrHandleMap(handleMap)
{
    if (mShmCh != nullptr) {
        mShmCh->IncreaseRef();
    }

    if (mWorker != nullptr) {
        mWorker->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mDriver != nullptr && mShmCh != nullptr) {
        mSegSize = mDriver->GetOptions().mrSendReceiveSegSize;
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
    }

    OBJ_GC_INCREASE(NetAsyncEndpointShm);
}

NetAsyncEndpointShm::~NetAsyncEndpointShm()
{
    if (mShmCh != nullptr) {
        mShmCh->DecreaseRef();
    }

    if (mWorker != nullptr) {
        mWorker->DecreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
    }

    OBJ_GC_DECREASE(NetAsyncEndpointShm);
}

NResult NetAsyncEndpointShm::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, opCode, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send as validate fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mAllowedSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send as validate size fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    /* copy header */
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(address);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    header->opCode = opCode;
    header->flags = NTH_TWO_SIDE;

    /* copy message */
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Shm Failed to post send message as encryption failure");
            (void)mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)),
            mShmCh->GetSendDCBuckSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            (void)mShmCh->DCMarkBuckFree(address);
            NN_LOG_ERROR("Failed to copy the request to address");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    UBSHcomNetTransRequest innerReq = request;
    innerReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    innerReq.lAddress = address;

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_SEND);
    do {
        result = mWorker->PostSend(mShmCh, innerReq, offset, 0, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NetMonotonic::TimeNs() < finishTime && NeedRetry(result) && mDefaultTimeout != 0) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (result == SH_SEND_COMPLETION_CALLBACK_FAILURE) {
        NN_LOG_WARN("Post send successfully but unable to enqueue sent callback request, result " << result);
        return result;
    }

    /* mark data buck free if failed to send */
    (void)mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND, result);
    NN_LOG_ERROR("Failed to post send request, result " << result);
    return result;
}

NResult NetAsyncEndpointShm::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, opCode, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send as validation fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mAllowedSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send as validate size fail");
        return result;
    }

    /* get free buffer from channel */
    uint64_t offset = 0;
    uintptr_t address = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to get free buck from shm channel " << mShmCh->Id() << "," << "result " << result);
        return result;
    }

    /* copy header */
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(address);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;

    /* copy message */
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = mAes.EstimatedEncryptLen(request.size);
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Failed to post send message as encryption failure");
            (void)mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)),
            mShmCh->GetSendDCBuckSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            (void)mShmCh->DCMarkBuckFree(address);
            NN_LOG_ERROR("Failed to copy request to address");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    UBSHcomNetTransRequest innerReq = request;
    innerReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    innerReq.lAddress = address;

    // if result is timeout, need to retry
    bool flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_SEND);
    do {
        result = mWorker->PostSend(mShmCh, innerReq, offset, 0, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);  // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);
    if (result == SH_SEND_COMPLETION_CALLBACK_FAILURE) {
        NN_LOG_ERROR("Post send request successfully, failed to send completion callback to owner result " << result);
        return result;
    }
    NN_LOG_ERROR("Failed to post send request, result is " << result);
    (void)mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpointShm::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendRawValidation(mState, mId, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send raw as validate fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mSegSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send raw as validate size fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    UBSHcomNetTransRequest innerReq = request;
    innerReq.lAddress = address;

    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address), cipherLen)) {
            NN_LOG_ERROR("Shm Failed to post send message as encryption failure");
            (void)mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        innerReq.size = cipherLen;
    } else {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address), mShmCh->GetSendDCBuckSize(),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            (void)mShmCh->DCMarkBuckFree(address);
            NN_LOG_ERROR("Shm Failed to copy the request to address");
            return NN_INVALID_PARAM;
        }
        innerReq.size = request.size;
    }

    // if result is timeout, need to retry
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_SEND_RAW);
    do {
        result = mWorker->PostSend(mShmCh, innerReq, offset, seqNO, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (result == SH_SEND_COMPLETION_CALLBACK_FAILURE) {
        NN_LOG_ERROR("Post send request successfully, failed to send completion callback to owner,result: " << result);
        return result;
    }
    NN_LOG_ERROR("Failed to post send request, result " << result);
    (void)mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetAsyncEndpointShm::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendSglValidation(mState, mId, mDriver, seqNo, request, mSegSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post send raw sgl as validate fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    uint32_t dataLen = 0;
    uint32_t iovOffset = 0;

    UBSHcomNetTransRequest innerReq = {};
    innerReq.lAddress = address;

    if (mIsNeedEncrypt) {
        for (uint16_t i = 0; i < request.iovCount; i++) {
            dataLen += request.iov[i].size;
        }

        UBSHcomNetMessage tmpMsg {};
        bool messageReady = tmpMsg.AllocateIfNeed(dataLen);
        if (NN_UNLIKELY(!messageReady)) {
            NN_LOG_ERROR("Shm Failed to allocate net msg buffer failed");
            (void)mShmCh->DCMarkBuckFree(address);
            return NN_MALLOC_FAILED;
        }
        for (uint16_t i = 0; i < request.iovCount; i++) {
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(tmpMsg.mBuf) + iovOffset),
                tmpMsg.GetBufLen() - iovOffset, reinterpret_cast<const void *>(request.iov[i].lAddress),
                request.iov[i].size) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy request to tmpMsg");
                mShmCh->DCMarkBuckFree(address);
                return NN_INVALID_PARAM;
            }
            iovOffset += request.iov[i].size;
        }

        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, tmpMsg.mBuf, dataLen, reinterpret_cast<void *>(address), cipherLen)) {
            NN_LOG_ERROR("Shm Failed to post send message as encryption failure");
            (void)mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }

        innerReq.size = cipherLen;
    } else {
        for (uint16_t i = 0; i < request.iovCount; i++) {
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + iovOffset),
                mShmCh->GetSendDCBuckSize() - iovOffset, reinterpret_cast<const void *>(request.iov[i].lAddress),
                request.iov[i].size) != NN_OK)) {
                (void)mShmCh->DCMarkBuckFree(address);
                NN_LOG_ERROR("Failed to copy request to address");
                return NN_INVALID_PARAM;
            }
            dataLen += request.iov[i].size;
            iovOffset += request.iov[i].size;
        }
        innerReq.size = dataLen;
    }

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_SEND_RAW_SGL);
    do {
        result = mWorker->PostSendRawSgl(mShmCh, innerReq, request, offset, seqNo, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (result == SH_SEND_COMPLETION_CALLBACK_FAILURE) {
        NN_LOG_ERROR("Post send request successfully, failed to send completion callback to owner,result " << result);
        return result;
    }
    NN_LOG_ERROR("Failed to post send request, result " << result);
    (void)mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_ASYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetAsyncEndpointShm::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post read as validate fail");
        return result;
    }

    uint64_t finishTime = GetFinishTime();

    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_READ);
    do {
        result = mWorker->PostRead(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_ASYNC_POST_READ, result);
    return result;
}

NResult NetAsyncEndpointShm::PostRead(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostReadWriteSglValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post read sgl as validate fail");
        return result;
    }

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_READ_SGL);
    do {
        result = mWorker->PostReadSgl(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_ASYNC_POST_READ_SGL, result);
    return result;
}

NResult NetAsyncEndpointShm::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post write as validate fail");
        return result;
    }

    uint64_t finishTime = GetFinishTime();

    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_WRITE);
    do {
        result = mWorker->PostWrite(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_ASYNC_POST_WRITE, result);
    return result;
}

NResult NetAsyncEndpointShm::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostReadWriteSglValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to async post write sgl as validate fail");
        return result;
    }

    uint64_t finishTime = GetFinishTime();

    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_ASYNC_POST_WRITE_SGL);
    do {
        result = mWorker->PostWriteSgl(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_ASYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_ASYNC_POST_WRITE_SGL, result);
    return result;
}
}
}