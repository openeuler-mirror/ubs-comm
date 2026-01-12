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
#ifdef RDMA_BUILD_ENABLED
#include "net_common.h"
#include "net_rdma_driver_oob.h"
#include "net_security_rand.h"
#include "rdma_validation.h"
#include "net_rdma_sync_endpoint.h"

namespace ock {
namespace hcom {
NetSyncEndpoint::NetSyncEndpoint(uint64_t id, RDMASyncEndpoint *ep, NetDriverRDMAWithOob *driver,
    const UBSHcomNetWorkerIndex &workerIndex)
    : NetEndpointImpl(id, workerIndex), mEp(ep), mDriver(driver)
{
    if (mEp != nullptr) {
        mEp->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mEp != nullptr && mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize < mEp->Qp()->PostSendMaxSize() ?
            mDriver->mOptions.mrSendReceiveSegSize :
            mEp->Qp()->PostSendMaxSize();
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
    }

    /* set worker index and group index to 0xFFFF */
    mWorkerIndex.idxInGrp = INVALID_WORKER_INDEX;
    mWorkerIndex.grpIdx = INVALID_WORKER_GROUP_INDEX;

    OBJ_GC_INCREASE(NetSyncEndpoint);
}

NetSyncEndpoint::~NetSyncEndpoint()
{
    if (mEp != nullptr) {
        mEp->DecreaseRef();
        mEp = nullptr;
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
        mDriver = nullptr;
    }

    OBJ_GC_DECREASE(NetSyncEndpoint);
}

NResult NetSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post send as validate fail");
        return result;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Verbs Failed to sync post send with seq no as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    // copy message
    auto *verbsHeader = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(verbsHeader, sizeof(UBSHcomNetTransHeader));
    verbsHeader->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    verbsHeader->opCode = opCode;
    verbsHeader->flags = NTH_TWO_SIDE;
    verbsHeader->dataLength = request.size;

    mLastSendSeqNo = verbsHeader->seqNo;
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("RDMA Failed to sync post send with seq no as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        verbsHeader->dataLength = cipherLen;
    } else {
        // copy message
        verbsHeader->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("RDMA Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill header crc */
    verbsHeader->headerCrc = NetFunc::CalcHeaderCrc32(verbsHeader);
    mDemandPollingOpType = RDMAOpContextInfo::SEND;

    // post request
    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + verbsHeader->dataLength;

    auto syncSendFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_SEND);
    do {
        result = mEp->PostSend(rdmaReq);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        syncSendFlag = false;
    } while (syncSendFlag);

    NN_LOG_ERROR("Failed to sync post send with seqNo, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post send as validate fail");
        return result;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("RDMA Failed to sync post send with op info as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    // copy message
    auto *verbsHeader = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(verbsHeader, sizeof(UBSHcomNetTransHeader));
    verbsHeader->opCode = opCode;
    verbsHeader->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    verbsHeader->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint16_t)NTH_TWO_SIDE;
    verbsHeader->timeout = opInfo.timeout;
    verbsHeader->errorCode = opInfo.errorCode;
    verbsHeader->dataLength = request.size;

    mLastSendSeqNo = verbsHeader->seqNo;
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("RDMA Failed to sync post send with op info as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        verbsHeader->dataLength = cipherLen;
    } else {
        // copy message
        verbsHeader->dataLength = request.size;

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill verbsHeader crc */
    verbsHeader->headerCrc = NetFunc::CalcHeaderCrc32(verbsHeader);
    mDemandPollingOpType = RDMAOpContextInfo::SEND;

    // post request
    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + verbsHeader->dataLength;

    auto syncSendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_SEND);
    do {
        result = mEp->PostSend(rdmaReq);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        syncSendOpFlag = false;
    } while (syncSendOpFlag);

    NN_LOG_ERROR("Failed to sync post send with op info, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo, const UBSHcomExtHeaderType extHeaderType, const void *extHeader,
    uint32_t extHeaderSize)
{
    if (NN_UNLIKELY(extHeaderType == UBSHcomExtHeaderType::RAW)) {
        NN_LOG_ERROR("You shouldn't use RAW type when extHeader is given in sync ep");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!extHeader)) {
        NN_LOG_ERROR("The ExtHeader is invalid.");
        return NN_INVALID_PARAM;
    }

    // 保证 extHeaderSize + request.size <= mAllowedSize.
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize - extHeaderSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post send as validate fail");
        return result;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to sync post send with op info as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;
    header->dataLength = request.size + extHeaderSize;
    header->extHeaderType = extHeaderType;

    mLastSendSeqNo = header->seqNo;
    if (mIsNeedEncrypt) {
        NN_LOG_WARN("postsent encrypt is not supported now!");
    }

    // 拷贝上层指定的 header，此时将要发送的结构为
    //     | UBSHcomNetTransHeader | extHeader | request body |
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
                             mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader), extHeader,
                             extHeaderSize) != NN_OK)) {
        mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
        NN_LOG_ERROR("Failed to copy request to mrBufAddress");
        return NN_INVALID_PARAM;
    }

    // 拷贝消息主体
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader) + extHeaderSize),
                             mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader) - extHeaderSize,
                             reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
        mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
        NN_LOG_ERROR("Failed to copy request to mrBufAddress");
        return NN_INVALID_PARAM;
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    mDemandPollingOpType = RDMAOpContextInfo::SEND;

    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto syncSendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_SEND);
    do {
        result = mEp->PostSend(rdmaReq);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);  // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        syncSendOpFlag = false;
    } while (syncSendOpFlag);

    NN_LOG_ERROR("Failed to sync post send with op info, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpoint::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendRawValidation(mState, mId, mDriver, seqNo, request, mSegSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post send raw as validate fail");
        return result;
    }

    /* get mr from pool */
    uintptr_t mrBufAddress = 0;
    size_t msgSize = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post message as failed to get mr buffer from pool from driver " << mDriver->Name());
        return NN_GET_BUFF_FAILED;
    }

    if (!mIsNeedEncrypt) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress), mDriver->mDriverSendMR->GetSingleSegSize(),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to memcpy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
        msgSize = request.size;
    } else {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress), cipherLen)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed send raw message as encryption failure");
            return NN_ENCRYPT_FAILED;
        }
        msgSize = cipherLen;
    }

    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = msgSize;

    /* still use send */
    mDemandPollingOpType = RDMAOpContextInfo::SEND;

    mLastSendSeqNo = seqNo;

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_SEND_RAW);
    do {
        result = mEp->PostSend(rdmaReq, seqNo);
        if (NN_LIKELY(result == RR_OK)) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetSyncEndpoint::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t size = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendSglValidation(mState, mId, mDriver, seqNo, request, mSegSize, size,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post send raw sgl as validate fail");
        return result;
    }

    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    if (mIsNeedEncrypt) {
        if (NN_UNLIKELY(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, request, mSecrets) != NN_OK)) {
            NN_LOG_ERROR("RDMA failed to sync post send raw sgl as encrypt fail");
            return NN_ENCRYPT_FAILED;
        }
    }

    mDemandPollingOpType = RDMAOpContextInfo::SEND_RAW_SGL;
    mLastSendSeqNo = seqNo;
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_SEND_RAW_SGL);
    do {
        result = mEp->PostSendSgl(request, tlsReq, seqNo, mIsNeedEncrypt);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (mIsNeedEncrypt) {
        (void)mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    }
    NN_LOG_ERROR("Failed to post send raw sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetSyncEndpoint::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post read as validate fail");
        return result;
    }

    mDemandPollingOpType = RDMAOpContextInfo::READ;
    auto readFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_READ);
    do {
        result = mEp->PostRead(request);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        readFlag = false;
    } while (readFlag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_READ, result);
    return result;
}

NResult NetSyncEndpoint::PostRead(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteSglValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post read sgl as validate fail");
        return result;
    }

    mDemandPollingOpType = RDMAOpContextInfo::SGL_READ;
    auto readSglFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_READ_SGL);
    do {
        result = mEp->PostOneSideSgl(request, true);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        readSglFlag = false;
    } while (readSglFlag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_READ_SGL, result);
    return result;
}

NResult NetSyncEndpoint::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post write as validate fail");
        return result;
    }

    mDemandPollingOpType = RDMAOpContextInfo::WRITE;
    auto writeFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_WRITE);
    do {
        result = mEp->PostWrite(request);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        writeFlag = false;
    } while (writeFlag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_WRITE, result);
    return result;
}

NResult NetSyncEndpoint::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteSglValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to sync post write sgl as validate fail");
        return result;
    }

    mDemandPollingOpType = RDMAOpContextInfo::SGL_WRITE;
    auto writeSglFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_SYNC_POST_WRITE_SGL);
    do {
        result = mEp->PostOneSideSgl(request, false);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_SYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        writeSglFlag = false;
    } while (writeSglFlag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_SYNC_POST_WRITE_SGL, result);
    return result;
}

NResult NetSyncEndpoint::WaitCompletion(int32_t timeout)
{
    NN_LOG_TRACE_INFO("wait completion mDemandPollingOpType " << mDemandPollingOpType);
    RDMAOpContextInfo *opCtx = nullptr;
    NResult result = NN_OK;
    uint32_t immData = 0;

POLL_CQ:
    if (NN_UNLIKELY(result = mEp->PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return result;
    }

    if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
        // repost if receive opType
        if (opCtx->opType == RDMAOpContextInfo::RECEIVE) {
            if (mDelayHandleReceiveCtx == nullptr) {
                mDelayHandleReceiveCtx = opCtx;
                goto POLL_CQ;
            } else {
                NN_LOG_ERROR("Receive operation type has double received, prev context is not process");
            }
        }
        NN_LOG_WARN("Got un-demand operation type " << opCtx->opType << ", ignored by ep id " << mId);
    }

    opCtx->qp->DecreaseRef();
    if (opCtx->opType == RDMAOpContextInfo::SEND) {
        (void)mDriver->mDriverSendMR->ReturnBuffer(opCtx->mrMemAddr);
    }

    if (mIsNeedEncrypt && opCtx->opType == RDMAOpContextInfo::SEND_RAW_SGL) {
        // buffer should return when encrypt
        (void)mDriver->mDriverSendMR->ReturnBuffer(opCtx->mrMemAddr);
    }

    if (opCtx->opType == RDMAOpContextInfo::SGL_WRITE || opCtx->opType == RDMAOpContextInfo::SGL_READ) {
        auto sgeCtx = reinterpret_cast<RDMASgeCtxInfo *>(opCtx->upCtx);
        auto sglCtx = sgeCtx->ctx;
        result = RDMAOpContextInfo::GetNResult(opCtx->opResultType);
        sglCtx->result = sglCtx->result < result ? sglCtx->result : result;
        auto refCount = __sync_add_and_fetch(&(sglCtx->refCount), 1);
        if (sglCtx->iovCount == refCount) {
            return sglCtx->result;
        }
        goto POLL_CQ;
    }

    return NN_OK;
}

NResult NetSyncEndpoint::Receive(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    NResult result = NN_OK;
    RDMAOpContextInfo *opCtx = nullptr;
    uint32_t immData = 0;

    mDemandPollingOpType = RDMAOpContextInfo::RECEIVE;
    NN_LOG_TRACE_INFO("Verbs receive mDemandPollingOpType " << mDemandPollingOpType);
    if (NN_UNLIKELY(mDelayHandleReceiveCtx != nullptr)) {
        opCtx = mDelayHandleReceiveCtx;
        mDelayHandleReceiveCtx = nullptr;
    } else if (NN_UNLIKELY(result = mEp->PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return result;
    }
    size_t realDataSize = 0;
    do {
        if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
            NN_LOG_ERROR("Verbs Got un-demand operation type " << opCtx->opType << ", ignored");
            result = NN_ERROR;
            break;
        }

        auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(opCtx->mrMemAddr);
        // 可能会收到多个小包，小包的 SeqNo 在对端回复时由定时器机制生成，与
        // SyncEp 本地记录的 SeqNo 不一致，所以不再检验 SeqNo.
        result = NetFunc::ValidateHeaderWithDataSize(*tmpHeader, opCtx->dataSize);
        if (NN_UNLIKELY(result != NN_OK)) {
            break;
        }

        realDataSize = tmpHeader->dataLength;
        if (mIsNeedEncrypt) {
            realDataSize = mAes.GetRawLen(tmpHeader->dataLength);
        }
        auto msgReady = mRespMessage.AllocateIfNeed(realDataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Verbs Failed to allocate memory for response size " << realDataSize <<
                ", probably out of memory");
            result = NN_MALLOC_FAILED;
            break;
        }

        if (NN_UNLIKELY(memcpy_s(&(mRespCtx.mHeader), sizeof(UBSHcomNetTransHeader), tmpHeader,
            sizeof(UBSHcomNetTransHeader)) != NN_OK)) {
            NN_LOG_WARN("Invalid operation to memcpy_s in Receive");
            result = NN_ERROR;
            break;
        }
        auto tmpDataAddress = reinterpret_cast<void *>(opCtx->mrMemAddr + sizeof(UBSHcomNetTransHeader));

        if (mIsNeedEncrypt) {
            uint32_t decryptLen = 0;
            if (!mAes.Decrypt(mSecrets, tmpDataAddress, tmpHeader->dataLength, mRespMessage.mBuf, decryptLen)) {
                NN_LOG_ERROR("Verbs Failed to decrypt data");
                result = NN_DECRYPT_FAILED;
                break;
            }
            mRespMessage.mDataLen = decryptLen;
        } else {
            if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf, mRespMessage.GetBufLen(), tmpDataAddress,
                tmpHeader->dataLength) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy tmpDataAddress to mRespMessage");
                result = NN_ERROR;
                break;
            }
            mRespMessage.mDataLen = tmpHeader->dataLength;
        }
    } while (false);

    auto receiveFlag = true;
    uint64_t finishTime = GetFinishTime();
    RResult rePostResult = RR_OK;
    uintptr_t mrMemAddr = opCtx->mrMemAddr;
    do {
        rePostResult = mEp->RePostReceive(opCtx);
        if (rePostResult == RR_OK) {
            break;
        }
        if (NeedRetry(rePostResult) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);  // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry rePostResult or timeout = 0
        receiveFlag = false;
    } while (receiveFlag);

    if (NN_UNLIKELY(rePostResult != RR_OK)) {
        NN_LOG_ERROR("Failed to repost receive, result " << rePostResult);
        mEp->ReturnBuffer(mrMemAddr);
        return rePostResult;
    }

    if (NN_LIKELY(result == NN_OK)) {
        mRespMessage.mDataLen = realDataSize;
        mRespCtx.mHeader.dataLength = realDataSize;
        mRespCtx.mMessage = &mRespMessage;
        ctx.mHeader = mRespCtx.mHeader;
        ctx.mMessage = mRespCtx.mMessage;
    }

    return result;
}

NResult NetSyncEndpoint::ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    RDMAOpContextInfo *opCtx = nullptr;
    NResult verbsResult = NN_OK;
    uint32_t immData = 0;

    mDemandPollingOpType = RDMAOpContextInfo::RECEIVE;
    if (NN_UNLIKELY(mDelayHandleReceiveCtx != nullptr)) {
        opCtx = mDelayHandleReceiveCtx;
        mDelayHandleReceiveCtx = nullptr;
    } else if (NN_UNLIKELY(verbsResult = mEp->PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return verbsResult;
    }

    do {
        if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
            NN_LOG_ERROR("Got un-demand operation type " << opCtx->opType << " in ReceiveRaw, ignored");
            verbsResult = NN_ERROR;
            break;
        }

        if (NN_UNLIKELY(immData != mLastSendSeqNo)) {
            NN_LOG_ERROR("Received un-matched seq no " << immData << ", demand seq no " << mLastSendSeqNo);
            verbsResult = NN_SEQ_NO_NOT_MATCHED;
            break;
        }

        auto dataSize = opCtx->dataSize;
        auto msgReady = mRespMessage.AllocateIfNeed(dataSize);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Failed to allocate memory for response size " << opCtx->dataSize <<
                ", probably out of memory");
            verbsResult = NN_MALLOC_FAILED;
            break;
        }

        auto tmpDataAddress = reinterpret_cast<void *>(opCtx->mrMemAddr);
        if (mIsNeedEncrypt) {
            uint32_t decryptLen = 0;
            if (!mAes.Decrypt(mSecrets, tmpDataAddress, dataSize, mRespMessage.mBuf, decryptLen)) {
                NN_LOG_ERROR("Failed to decrypt data");
                verbsResult = NN_DECRYPT_FAILED;
                break;
            }
            mRespMessage.mDataLen = decryptLen;
        } else {
            if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf, mRespMessage.GetBufLen(), tmpDataAddress, dataSize) != NN_OK)) {
                NN_LOG_ERROR("Failed to tmpDataAddress req to mRespMessage");
                verbsResult = NN_INVALID_PARAM;
                break;
            }
            mRespMessage.mDataLen = dataSize;
        }
    } while (false);

    RResult rePostResult = RR_OK;
    auto receiveRawFlag = true;
    uint64_t finishTime = GetFinishTime();
    uintptr_t mrMemAddr = opCtx->mrMemAddr;
    do {
        rePostResult = mEp->RePostReceive(opCtx);
        if (rePostResult == RR_OK) {
            break;
        } else if (NeedRetry(rePostResult) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry verbsResult or timeout = 0
        receiveRawFlag = false;
    } while (receiveRawFlag);

    if (NN_UNLIKELY(rePostResult != RR_OK)) {
        NN_LOG_ERROR("Failed to repost receive raw, result " << rePostResult);
        mEp->ReturnBuffer(mrMemAddr);
        return rePostResult;
    }

    if (NN_LIKELY(verbsResult == NN_OK)) {
        mRespCtx.mMessage = &mRespMessage;
        ctx.mHeader = {};
        ctx.mHeader.opCode = -1;
        ctx.mHeader.seqNo = immData;
        ctx.mMessage = mRespCtx.mMessage;
    }

    return verbsResult;
}
}
}
#endif
