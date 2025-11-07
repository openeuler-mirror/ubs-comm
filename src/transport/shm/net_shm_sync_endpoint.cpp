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
#include "net_shm_sync_endpoint.h"

namespace ock {
namespace hcom {
NetSyncEndpointShm::NetSyncEndpointShm(uint64_t id, ShmChannel *ch, NetDriverShmWithOOB *driver,
    const UBSHcomNetWorkerIndex &workerIndex, ShmSyncEndpoint *shmEp, ShmMRHandleMap &handleMap)
    : NetEndpointImpl(id, workerIndex), mShmCh(ch), mDriver(driver), mShmEp(shmEp), mrHandleMap(handleMap)
{
    if (mShmCh != nullptr) {
        mShmCh->IncreaseRef();
    }
    if (mShmEp != nullptr) {
        mShmEp->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mShmCh != nullptr && mDriver != nullptr) {
        mSegSize = mDriver->GetOptions().mrSendReceiveSegSize;
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
    }

    OBJ_GC_INCREASE(NetSyncEndpointShm);
}

NetSyncEndpointShm::~NetSyncEndpointShm()
{
    if (mShmCh != nullptr) {
        mShmCh->DecreaseRef();
    }
    if (mShmEp != nullptr) {
        mShmEp->DecreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
    }

    OBJ_GC_DECREASE(NetSyncEndpointShm);
}

NResult NetSyncEndpointShm::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, opCode, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send as validate fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mAllowedSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send as validate size fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to get free buck from Shm Channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    /* copy header */
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(address);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    header->flags = NTH_TWO_SIDE;

    mLastSendSeqNo = header->seqNo;

    /* copy message */
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Shm Failed to post send message as encryption failed");
            mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)),
            mShmCh->GetSendDCBuckSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mShmCh->DCMarkBuckFree(address);
            NN_LOG_ERROR("Failed to copy request to address");
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
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_SEND);
    do {
        result = mShmEp->PostSend(mShmCh, innerReq, offset, 0, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post send request, result: " << result);
    mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpointShm::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, opCode, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send as validation fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mAllowedSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send as validate size failed");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    /* copy header */
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(address);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;
    mLastSendSeqNo = header->seqNo;

    /* copy message */
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Failed to post send message as encryption failure");
            mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + sizeof(UBSHcomNetTransHeader)),
            mShmCh->GetSendDCBuckSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mShmCh->DCMarkBuckFree(address);
            NN_LOG_ERROR("Failed to copy request to address");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    UBSHcomNetTransRequest innerReq = request;
    innerReq.lAddress = address;
    innerReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_SEND);
    do {
        result = mShmEp->PostSend(mShmCh, innerReq, offset, 0, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpointShm::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendRawValidation(mState, mId, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send raw as validate fail");
        return result;
    }
 
    if (NN_UNLIKELY((result = PostSendValidationMaxSize(request, mSegSize, mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send raw as validate size fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    UBSHcomNetTransRequest innerReq = request;
    innerReq.lAddress = address;
    mLastSendSeqNo = seqNO;

    /* copy message */
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(address), cipherLen)) {
            NN_LOG_ERROR("Failed to post send message as encryption failure");
            mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }
        innerReq.size = cipherLen;
    } else {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address), mShmCh->GetSendDCBuckSize(),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy request to address");
            mShmCh->DCMarkBuckFree(address);
            return NN_INVALID_PARAM;
        }
        innerReq.size = request.size;
    }

    /* if result is timeout, need to retry */
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_SEND_RAW);
    do {
        result = mShmEp->PostSend(mShmCh, innerReq, offset, seqNO, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetSyncEndpointShm::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendSglValidation(mState, mId, mDriver, seqNo, request, mSegSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post send raw sgl as validate fail");
        return result;
    }

    /* get free buffer from channel */
    uintptr_t address = 0;
    uint64_t offset = 0;
    result = mShmCh->DCGetFreeBuck(address, offset, NN_NO100, mDefaultTimeout);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Failed to get free buck from shm channel " << mShmCh->Id() << ", result " << result);
        return result;
    }

    uint32_t dataLen = 0;
    uint32_t iovOffset = 0;

    UBSHcomNetTransRequest innerReq = {};
    innerReq.lAddress = address;
    mLastSendSeqNo = seqNo;

    /* copy message */
    if (mIsNeedEncrypt) {
        for (uint16_t i = 0; i < request.iovCount; i++) {
            dataLen += request.iov[i].size;
        }

        UBSHcomNetMessage tmpMsg {};
        bool messageReady = tmpMsg.AllocateIfNeed(dataLen);
        if (NN_UNLIKELY(!messageReady)) {
            NN_LOG_ERROR("Failed to allocate net msg buffer failed");
            mShmCh->DCMarkBuckFree(address);
            return NN_MALLOC_FAILED;
        }
        for (uint16_t i = 0; i < request.iovCount; i++) {
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(tmpMsg.mBuf) + iovOffset),
                request.iov[i].size, reinterpret_cast<const void *>(request.iov[i].lAddress),
                request.iov[i].size) != NN_OK)) {
                mShmCh->DCMarkBuckFree(address);
                NN_LOG_WARN("Invalid operation to memcpy_s in shm encrypt PostSendRawSgl");
                return NN_ERROR;
            }
            iovOffset += request.iov[i].size;
        }

        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, tmpMsg.mBuf, dataLen, reinterpret_cast<void *>(address), cipherLen)) {
            NN_LOG_ERROR("Failed to post send message as encryption failure");
            mShmCh->DCMarkBuckFree(address);
            return NN_ENCRYPT_FAILED;
        }

        innerReq.size = cipherLen;
    } else {
        for (uint16_t i = 0; i < request.iovCount; i++) {
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(address + iovOffset), request.iov[i].size,
                reinterpret_cast<const void *>(request.iov[i].lAddress), request.iov[i].size) != NN_OK)) {
                mShmCh->DCMarkBuckFree(address);
                NN_LOG_WARN("Invalid operation to memcpy_s in shm PostSendRawSgl");
                return NN_ERROR;
            }
            dataLen += request.iov[i].size;
            iovOffset += request.iov[i].size;
        }
        innerReq.size = dataLen;
    }

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_SEND_RAW_SGL);
    do {
        result = mShmEp->PostSendRawSgl(mShmCh, innerReq, request, offset, seqNo, mDefaultTimeout);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    mShmCh->DCMarkBuckFree(address);
    TRACE_DELAY_END(SHM_EP_SYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetSyncEndpointShm::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post read as validate fail");
        return result;
    }

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_READ);
    do {
        result = mShmEp->PostRead(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_SYNC_POST_READ, result);
    return result;
}

NResult NetSyncEndpointShm::PostRead(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostReadWriteSglValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post read sgl as validate fail");
        return result;
    }

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_READ_SGL);
    do {
        result = mShmEp->PostRead(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_SYNC_POST_READ_SGL, result);
    return result;
}

NResult NetSyncEndpointShm::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post write as validate fail");
        return result;
    }

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_WRITE);
    do {
        result = mShmEp->PostWrite(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_SYNC_POST_WRITE, result);
    return result;
}

NResult NetSyncEndpointShm::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostReadWriteSglValidation(mState, mId, mDriver, mShmCh, request)) != NN_OK)) {
        NN_LOG_ERROR("Shm failed to sync post write sgl as validate fail");
        return result;
    }

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SHM_EP_SYNC_POST_WRITE_SGL);
    do {
        result = mShmEp->PostWrite(mShmCh, request, mrHandleMap);
        if (result == SH_OK) {
            TRACE_DELAY_END(SHM_EP_SYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        flag = false;
    } while (flag);

    TRACE_DELAY_END(SHM_EP_SYNC_POST_WRITE_SGL, result);
    return result;
}

NResult NetSyncEndpointShm::Receive(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    ShmOpContextInfo opCtx {};
    NResult result = NN_OK;
    mDemandPollingOpType = ShmOpContextInfo::SH_RECEIVE;
    uint32_t immData = 0;

    if (NN_UNLIKELY(mExistDelayEvent)) {
        mExistDelayEvent = false;

        auto *ch = reinterpret_cast<ShmChannel *>(mDelayHandleReceiveEvent.peerChannelAddress);
        if (NN_UNLIKELY(ch == nullptr)) {
            NN_LOG_ERROR("Shm Got invalid event in " << mShmEp->GetName() << ", dropped it");
            return NN_ERROR;
        }

        uintptr_t address = 0;
        if (NN_UNLIKELY((result = ch->GetPeerDataAddressByOffset(mDelayHandleReceiveEvent.dataOffset, address)) !=
            SH_OK)) {
            NN_LOG_ERROR("Shm Got invalid event " << mShmEp->GetName() << " as get data address failed, dropped it");
            return result;
        }

        opCtx = ShmOpContextInfo(ch, address, mDelayHandleReceiveEvent.dataSize,
            static_cast<ShmOpContextInfo::ShmOpType>(mDelayHandleReceiveEvent.opType),
            ShmOpContextInfo::ShmErrorType::SH_NO_ERROR);
    } else if (NN_UNLIKELY((result = mShmEp->Receive(timeout, opCtx, immData)) != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to receive response from peer, result " << result);
        return result;
    }

    if (NN_UNLIKELY(opCtx.opType != mDemandPollingOpType)) {
        NN_LOG_ERROR("Shm Got un-demand operation type " << opCtx.opType << ", ignored");
        opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
        return NN_ERROR;
    }

    auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(opCtx.dataAddress);
    result = NetFunc::ValidateHeaderWithSeqNo(*tmpHeader, opCtx.dataSize, mLastSendSeqNo);
    if (NN_UNLIKELY(result != NN_OK)) {
        NN_LOG_ERROR("Shm Failed to validate received header param, ep " << Id());
        opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
        return result;
    }

    size_t realDataSize = 0;
    if (mIsNeedEncrypt) {
        const void *cipherData = reinterpret_cast<const void *>(opCtx.dataAddress + sizeof(UBSHcomNetTransHeader));
        realDataSize = mAes.GetRawLen(tmpHeader->dataLength);
        uint32_t decryptLen = 0;
        bool msgReady = mRespMessage.AllocateIfNeed(realDataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Shm Failed to allocate memory for response size " << opCtx.dataSize <<
                ", probably out of memory");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_MALLOC_FAILED;
        }
        if (!mAes.Decrypt(mSecrets, cipherData, tmpHeader->dataLength, mRespMessage.mBuf, decryptLen)) {
            NN_LOG_ERROR("Shm Failed to decrypt data");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_DECRYPT_FAILED;
        }
    } else {
        realDataSize = tmpHeader->dataLength;
        auto msgReady = mRespMessage.AllocateIfNeed(realDataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Failed to allocate memory for response size " << realDataSize << ", probably out of memory");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_MALLOC_FAILED;
        }

        auto tmpDataAddress = reinterpret_cast<void *>(opCtx.dataAddress + sizeof(UBSHcomNetTransHeader));
        if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf, mRespMessage.GetBufLen(), tmpDataAddress, realDataSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy tmpDataAddress to mRespMessage");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_INVALID_PARAM;
        }
    }

    if (NN_UNLIKELY(memcpy_s(&(mRespCtx.mHeader), sizeof(UBSHcomNetTransHeader), tmpHeader,
        sizeof(UBSHcomNetTransHeader)) != NN_OK)) {
        opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
        NN_LOG_ERROR("Failed to copy tmpHeader to mRespCtx");
        return NN_INVALID_PARAM;
    }
    mRespMessage.mDataLen = realDataSize;
    mRespCtx.mHeader.dataLength = realDataSize;
    mRespCtx.mMessage = &mRespMessage;
    ctx.mHeader = mRespCtx.mHeader;
    ctx.mMessage = mRespCtx.mMessage;

    opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
    return result;
}

NResult NetSyncEndpointShm::ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    ShmOpContextInfo opCtx {};
    NResult result = NN_OK;
    mDemandPollingOpType = ShmOpContextInfo::SH_RECEIVE;
    uint32_t immData = 0;
    if (NN_UNLIKELY(mExistDelayEvent)) {
        mExistDelayEvent = false;

        auto *ch = reinterpret_cast<ShmChannel *>(mDelayHandleReceiveEvent.peerChannelAddress);
        if (NN_UNLIKELY(ch == nullptr)) {
            NN_LOG_ERROR("Got invalid event in " << mShmEp->GetName() << ", dropped it");
            return NN_ERROR;
        }
        uintptr_t address = 0;
        if (NN_UNLIKELY((result = ch->GetPeerDataAddressByOffset(mDelayHandleReceiveEvent.dataOffset, address)) !=
            SH_OK)) {
            NN_LOG_ERROR("Got invalid event " << mShmEp->GetName() << " as get data address failed, dropped it");
            return result;
        }
        opCtx = ShmOpContextInfo(ch, address, mDelayHandleReceiveEvent.dataSize,
            static_cast<ShmOpContextInfo::ShmOpType>(mDelayHandleReceiveEvent.opType),
            ShmOpContextInfo::ShmErrorType::SH_NO_ERROR);
    } else if (NN_UNLIKELY((result = mShmEp->Receive(timeout, opCtx, immData)) != NN_OK)) {
        NN_LOG_ERROR("Failed to get operation,time out");
        return result;
    }

    if (NN_UNLIKELY(opCtx.opType != mDemandPollingOpType)) {
        NN_LOG_ERROR("Got un-demand operation type " << opCtx.opType << ", ignored");
        opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
        return NN_ERROR;
    }
    if (NN_UNLIKELY(immData != mLastSendSeqNo)) {
        NN_LOG_ERROR("Received un-matched seq no " << immData << ", demand seq no " << mLastSendSeqNo);
        opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
        return NN_SEQ_NO_NOT_MATCHED;
    }

    size_t realDataSize = 0;
    if (mIsNeedEncrypt) {
        const void *cipherData = reinterpret_cast<const void *>(opCtx.dataAddress);
        realDataSize = mAes.GetRawLen(opCtx.dataSize);
        uint32_t decryptLen = 0;
        bool msgReady = mRespMessage.AllocateIfNeed(realDataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Failed to allocate memory for response size " << opCtx.dataSize <<
                ", probably out of memory");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_MALLOC_FAILED;
        }
        if (!mAes.Decrypt(mSecrets, cipherData, opCtx.dataSize, mRespMessage.mBuf, decryptLen)) {
            NN_LOG_ERROR("Failed to decrypt data");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_DECRYPT_FAILED;
        }
    } else {
        realDataSize = opCtx.dataSize;
        auto msgReady = mRespMessage.AllocateIfNeed(realDataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Failed to allocate memory for response size " << realDataSize << ", probably out of memory");
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            return NN_MALLOC_FAILED;
        }

        auto tmpDataAddress = reinterpret_cast<void *>(opCtx.dataAddress);
        if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf, mRespMessage.GetBufLen(), tmpDataAddress, realDataSize) != NN_OK)) {
            opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
            NN_LOG_ERROR("Failed to copy tmpDataAddress to mRespMessage");
            return NN_INVALID_PARAM;
        }
    }

    mRespMessage.mDataLen = realDataSize;
    mRespCtx.mMessage = &mRespMessage;

    ctx.mHeader = {};
    ctx.mHeader.opCode = -1;
    ctx.mHeader.seqNo = immData;
    ctx.mHeader.dataLength = realDataSize;
    ctx.mMessage = mRespCtx.mMessage;

    opCtx.channel->DCMarkPeerBuckFree(opCtx.dataAddress);
    return result;
}

NResult NetSyncEndpointShm::WaitCompletion(int32_t timeout)
{
    ShmEvent event {};
    NResult result = NN_OK;

POLL_EVENT:
    if (NN_UNLIKELY(result = mShmEp->DequeueEvent(timeout, event)) != NN_OK) {
        return result;
    }

    // repost if receive opType
    if (event.opType == ShmOpContextInfo::SH_RECEIVE) {
        if (!mExistDelayEvent) {
            mDelayHandleReceiveEvent = event;
            mExistDelayEvent = true;
            goto POLL_EVENT;
        } else {
            NN_LOG_ERROR("Receive operation type has double received, prev context is not process");
            return SH_ERROR;
        }
    }

    if (event.opType == ShmOpContextInfo::ShmOpType::SH_SEND) {
        auto compEvent = reinterpret_cast<ShmOpCompInfo *>(event.peerChannelAddress);
        if (compEvent != nullptr && compEvent->channel != nullptr) {
            compEvent->channel->DecreaseRef();
        }
        return result;
    }

    if (event.opType == ShmOpContextInfo::SH_READ || event.opType == ShmOpContextInfo::SH_WRITE ||
        event.opType == ShmOpContextInfo::SH_SGL_READ || event.opType == ShmOpContextInfo::SH_SGL_WRITE) {
        auto opContextInfo = reinterpret_cast<ShmOpContextInfo *>(event.peerChannelAddress);
        if (opContextInfo != nullptr && opContextInfo->channel != nullptr) {
            opContextInfo->channel->DecreaseRef();
        }
        return result;
    }

    NN_LOG_ERROR("Got un-demand operation type " << event.opType << ", ignored");
    return SH_ERROR;
}
}
}