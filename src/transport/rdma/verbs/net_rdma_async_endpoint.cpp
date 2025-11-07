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
#include "net_rdma_async_endpoint.h"

namespace ock {
namespace hcom {
NetAsyncEndpoint::NetAsyncEndpoint(uint64_t id, RDMAAsyncEndPoint *ep, NetDriverRDMAWithOob *driver,
    const UBSHcomNetWorkerIndex &workerIndex)
    : NetEndpointImpl(id, workerIndex), mEp(ep), mDriver(driver)
{
    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mEp != nullptr) {
        mEp->IncreaseRef();
    }

    if (mEp != nullptr && mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize < mEp->Qp()->PostSendMaxSize() ?
            mDriver->mOptions.mrSendReceiveSegSize :
            mEp->Qp()->PostSendMaxSize();
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
    }

    mIsNeedSendHb = true;
    if (mDriver != nullptr) {
        mHeartBeatIdleTime = mDriver->GetHbIdleTime();
        UpdateTargetHbTime();
    }

    OBJ_GC_INCREASE(NetAsyncEndpoint);
}

NetAsyncEndpoint::~NetAsyncEndpoint()
{
    if (mEp != nullptr) {
        mEp->DecreaseRef();
        mEp = nullptr;
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
        mDriver = nullptr;
    }

    OBJ_GC_DECREASE(NetAsyncEndpoint);
}

uint32_t NetAsyncEndpoint::GetSendQueueCount()
{
    return mEp->Qp()->GetSendQueueSize();
}

NResult NetAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post send as validate fail");
        return result;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with seqNo as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    header->flags = NTH_TWO_SIDE;

    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("RDMA Failed to async post send with seq no as encryption failure");
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("RDMA Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    // change lAddress to mrAddress and set lKey
    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());

    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto sendFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mEp->Qp(), rdmaReq);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        sendFlag = false;
    } while (sendFlag);

    NN_LOG_ERROR("Failed to async post send with seq no, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo, const UBSHcomExtHeaderType extHeaderType, const void *extHeader,
    uint32_t extHeaderSize)
{
    if (NN_UNLIKELY(extHeaderType == UBSHcomExtHeaderType::RAW)) {
        NN_LOG_ERROR("Shouldn't use RAW type when extHeader is given.");
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
        NN_LOG_ERROR("RDMA failed to async post send as validate fail");
        return result;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("RDMA failed to async post send with op info as failed to get mr buffer from pool");
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

    if (mIsNeedEncrypt) {
        NN_LOG_WARN("postsend encrypt is not supported now!");
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

    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());

    auto sendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mEp->Qp(), rdmaReq);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, result);
            return RR_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);  // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        sendOpFlag = false;
    } while (sendOpFlag);

    NN_LOG_ERROR("Failed to async post send with op info, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult res = NN_OK;
    if (NN_UNLIKELY((res = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post send as validate fail");
        return res;
    }

    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with opInfo as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;

    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Mlx5 Failed to async post send with op info as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            mDriver->mDriverSendMR->GetSingleSegSize() - sizeof(UBSHcomNetTransHeader),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
    }
    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());

    auto sendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_SEND);
    do {
        res = worker->PostSend(mEp->Qp(), rdmaReq);
        if (res == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, res);
            return NN_OK;
        } else if (NeedRetry(res) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        sendOpFlag = false;
    } while (sendOpFlag);

    NN_LOG_ERROR("Failed to async post send with op info, result " << res);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND, res);
    return res;
}

NResult NetAsyncEndpoint::PostSendSglInline(
    uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo)
{
    // 需要加密必定会涉及到内存拷贝，仍然走非inline方式
    if (mIsNeedEncrypt) {
        return PostSend(opCode, request, opInfo);
    }

    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendValidation(mState, mId, mDriver, opCode, request, mAllowedSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post send as validate fail");
        return result;
    }

    UBSHcomNetTransHeader header;
    header.opCode = opCode;
    header.seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header.flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header.timeout = opInfo.timeout;
    header.errorCode = opInfo.errorCode;
    header.dataLength = request.size;
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    bool flag = true;
    uint64_t finishTime = GetFinishTime();
    do {
        result = worker->PostSendSglInline(mEp->Qp(), header, request);
        if (result == RR_OK) {
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);
    return result;
}

NResult NetAsyncEndpoint::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendRawValidation(mState, mId, mDriver, seqNo, request, mSegSize,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post send raw as validate fail");
        return result;
    }

    /* get mr from pool */
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post message as failed to get mr buffer from pool from driver " << mDriver->Name());
        return NN_GET_BUFF_FAILED;
    }

    size_t msgSize = 0;
    if (!mIsNeedEncrypt) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress), mDriver->mDriverSendMR->GetSingleSegSize(),
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to copy request to mrBufAddress");
            return NN_INVALID_PARAM;
        }
        msgSize = request.size;
    } else {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets, reinterpret_cast<void *>(request.lAddress), request.size,
            reinterpret_cast<void *>(mrBufAddress), cipherLen)) {
            NN_LOG_ERROR("Failed send message as encryption failure in rdma");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        msgSize = cipherLen;
    }

    UBSHcomNetTransRequest rdmaReq = request;
    rdmaReq.lAddress = mrBufAddress;
    rdmaReq.lKey = mDriver->mDriverSendMR->GetLKey();
    rdmaReq.size = msgSize;

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    auto sendRawAsyncFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_SEND_RAW);
    do {
        result = worker->PostSend(mEp->Qp(), rdmaReq, seqNo);
        if (NN_LIKELY(result == RR_OK)) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(NN_NO128);
            continue;
        }
        // no retry result or timeout = 0
        sendRawAsyncFlag = false;
    } while (sendRawAsyncFlag);

    NN_LOG_ERROR("Failed to post send raw request, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetAsyncEndpoint::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t size = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = PostSendSglValidation(mState, mId, mDriver, seqNo, request, mSegSize, size,
        mIsNeedEncrypt, mAes)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post send raw sgl as validate fail");
        return result;
    }

    UBSHcomNetTransRequest tlsReq {};
    uintptr_t mrBufAddress = 0;
    if (mIsNeedEncrypt) {
        if (NN_UNLIKELY(EncryptRawSgl(tlsReq, mrBufAddress, size, mAes, mDriver, request, mSecrets) != NN_OK)) {
            NN_LOG_ERROR("RDMA failed to async post send raw sgl as encrypt fail");
            return NN_ENCRYPT_FAILED;
        }
    }

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_SEND_RAW_SGL);
    do {
        result = worker->PostSendSgl(mEp->Qp(), request, tlsReq, seqNo, mIsNeedEncrypt);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep眠
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (mIsNeedEncrypt) {
        (void)mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    }

    NN_LOG_ERROR("RDMA Failed to post send raw sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetAsyncEndpoint::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post read as validate fail");
        return result;
    }

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    auto asyncReadFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_READ);
    do {
        result = worker->PostRead(mEp->Qp(), request);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        asyncReadFlag = false;
    } while (asyncReadFlag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_READ, result);
    return result;
}

NResult NetAsyncEndpoint::PostRead(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteSglValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post read sgl as validate fail");
        return result;
    }

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_READ_SGL);
    do {
        result = worker->PostOneSideSgl(mEp->Qp(), request);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_READ_SGL, result);
    return result;
}

NResult NetAsyncEndpoint::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post write as validate fail");
        return result;
    }

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());

    auto asyncWriteFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_WRITE);
    do {
        result = worker->PostWrite(mEp->Qp(), request);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        asyncWriteFlag = false;
    } while (asyncWriteFlag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_WRITE, result);
    return result;
}

NResult NetAsyncEndpoint::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = ReadWriteSglValidation(mState, mId, mDriver, request)) != NN_OK)) {
        NN_LOG_ERROR("RDMA failed to async post write sgl as validate fail");
        return result;
    }

    auto worker = reinterpret_cast<RDMAWorker *>(mEp->Qp()->UpContext1());
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(RDMA_EP_ASYNC_POST_WRITE_SGL);
    do {
        result = worker->PostOneSideSgl(mEp->Qp(), request, false);
        if (result == RR_OK) {
            TRACE_DELAY_END(RDMA_EP_ASYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    TRACE_DELAY_END(RDMA_EP_ASYNC_POST_WRITE_SGL, result);
    return result;
}

void NetAsyncEndpoint::UpdateTargetHbTime()
{
    mTargetHbTime = NetMonotonic::TimeSec() + mHeartBeatIdleTime;
}
}
}
#endif
