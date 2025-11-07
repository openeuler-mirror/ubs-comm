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
#include "net_sock_driver_oob.h"
#include "sock_validation.h"
#include "net_sock_async_endpoint.h"

namespace ock {
namespace hcom {
NetAsyncEndpointSock::NetAsyncEndpointSock(uint64_t id, Sock *sock, NetDriverSockWithOOB *driver,
    const UBSHcomNetWorkerIndex &workerIndex)
    : NetEndpointImpl(id, workerIndex), mSock(sock), mDriver(driver)
{
    if (mSock != nullptr) {
        mSock->IncreaseRef();
        mWorker = reinterpret_cast<SockWorker *>(mSock->UpContext1());
    }

    if (mWorker != nullptr) {
        mWorker->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize;
        mAllowedSize = mSegSize - sizeof(SockTransHeader);
        mDriver->IncreaseRef();
    }

    OBJ_GC_INCREASE(NetAsyncEndpointSock);
}

NetAsyncEndpointSock::~NetAsyncEndpointSock()
{
    if (mWorker != nullptr && mSock != nullptr) {
        mWorker->RemoveFromEpoll(mSock);
    }

    if (mSock != nullptr) {
        mSock->Close();
        mSock->DecreaseRef();
    }

    if (mWorker != nullptr) {
        mWorker->DecreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
    }

    OBJ_GC_DECREASE(NetAsyncEndpointSock);
    // do later
}

NResult NetAsyncEndpointSock::SetEpOption(UBSHcomEpOptions &epOptions)
{
    if (!epOptions.tcpBlockingIo) {
        NN_LOG_WARN("Tcp is nonblocking in default, there is no need to set it again");
        return NN_OK;
    }

    if (mDefaultTimeout > 0 && epOptions.sendTimeout > mDefaultTimeout) {
        NN_LOG_WARN("send timeout should not longer than mDefaultTimeout " << mDefaultTimeout);
        return NN_ERROR;
    }

    if (NN_UNLIKELY(mSock->SetBlockingIo(epOptions) != SS_OK)) {
        NN_LOG_WARN("Unable to set sock " << mSock->Name() << " blocking io mode.");
        return NN_ERROR;
    }

    return NN_OK;
}

uint32_t NetAsyncEndpointSock::GetSendQueueCount()
{
    return mSock->GetSendQueueCount();
}

NResult NetAsyncEndpointSock::PostSendZCopy(int16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    REQ_SIZE_VALIDATION_ZERO_COPY();

    UBSHcomNetTransHeader header{};
    if (opCode == -1) {
        header.immData = 1;
    } else {
        header.opCode = opCode;
    }
    header.seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header.flags = NTH_TWO_SIDE;
    header.timeout = opInfo.timeout;
    header.errorCode = opInfo.errorCode;
    header.dataLength = request.size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());

    NResult result = NN_OK;
    uint64_t finishTimeSend = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mSock, header, request);
        if (result == SS_OK) {
            NN_LOG_TRACE_INFO("Sock Post send ep id " << mId << ", flag " << header.flags << ", seqNo " <<
                header.seqNo << ", size " << request.size);
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTimeSend) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to async post send request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpointSock::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send as state validation failed.");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send as buff validation failed.");
        return result;
    }
    OPCODE_VALIDATION();

    if (mSendZCopy) {
        UBSHcomNetTransOpInfo opInfo(seqNo, 0, 0, 0);
        return PostSendZCopy(opCode, request, opInfo);
    }

    REQ_SIZE_VALIDATION();
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mSockDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send message as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = seqNo == 0 ? NextSeq() : seqNo;
    header->flags = NTH_TWO_SIDE;
    header->dataLength = request.size;
    auto dataAddress = mrBufAddress + sizeof(SockTransHeader); // req data start address
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(dataAddress), mDriver->mSockDriverSendMR->GetSingleSegSize() -
        sizeof(SockTransHeader), reinterpret_cast<void *>(request.lAddress), request.size) != NN_OK)) {
        mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
        NN_LOG_ERROR("Failed to copy request to dataAddress");
        return NN_INVALID_PARAM;
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());

    uint64_t finishTimeSend = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mSock, *header, request);
        if (result == SS_OK) {
            NN_LOG_TRACE_INFO("Post send ep id " << mId << ", flag " << header->flags << ", seqNo " << header->seqNo <<
                ", size " << request.size);
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTimeSend) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
    NN_LOG_ERROR("Failed to async post send request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpointSock::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send as buff validation failed");
        return result;
    }
    OPCODE_VALIDATION();

    if (mSendZCopy) {
        return PostSendZCopy(opCode, request, opInfo);
    }

    REQ_SIZE_VALIDATION();
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mSockDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send message with opInfo as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }
    auto *sockHeader = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(sockHeader, sizeof(UBSHcomNetTransHeader));
    sockHeader->opCode = opCode;
    sockHeader->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    sockHeader->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint16_t)NTH_TWO_SIDE;
    sockHeader->timeout = opInfo.timeout;
    sockHeader->errorCode = opInfo.errorCode;
    sockHeader->dataLength = request.size;
    auto dataAddress = mrBufAddress + sizeof(SockTransHeader); // req data start address
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(dataAddress), mDriver->mSockDriverSendMR->GetSingleSegSize() -
        sizeof(SockTransHeader), reinterpret_cast<void *>(request.lAddress), request.size) != NN_OK)) {
        mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
        NN_LOG_ERROR("Failed to copy request to dataAddress");
        return NN_INVALID_PARAM;
    }

    /* finally fill sockHeader crc */
    sockHeader->headerCrc = NetFunc::CalcHeaderCrc32(sockHeader);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());

    uint64_t finishTimeOpSend = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mSock, *sockHeader, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTimeOpSend) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
    NN_LOG_ERROR("Failed to async post send request with opInfo, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetAsyncEndpointSock::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send raw as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send raw as buff validation failed");
        return result;
    }

    if (mSendZCopy) {
        UBSHcomNetTransOpInfo opInfo(seqNo, 0, 0, 0);
        return PostSendZCopy(-1, request, opInfo);
    }

    REQ_SIZE_VALIDATION();
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mSockDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post async message as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->immData = 1;
    header->seqNo = seqNo == 0 ? NextSeq() : seqNo;
    header->flags = NTH_TWO_SIDE;
    header->dataLength = request.size;
    auto dataAddress = mrBufAddress + sizeof(SockTransHeader); // req data start address
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(dataAddress), mDriver->mSockDriverSendMR->GetSingleSegSize(),
        reinterpret_cast<void *>(request.lAddress), request.size) != NN_OK)) {
        mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
        NN_LOG_ERROR("Failed to copy request to dataAddress");
        return NN_INVALID_PARAM;
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_SEND_RAW);
    do {
        result = worker->PostSend(mSock, *header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    mDriver->mSockDriverSendMR->ReturnBuffer(mrBufAddress);
    NN_LOG_ERROR("Failed to async post send raw request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetAsyncEndpointSock::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send raw sgl as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = TwoSideSglValidation(request, mDriver, mSegSize, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post send raw sgl as sgl validation failed");
        return result;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = seqNo == 0 ? NextSeq() : seqNo;
    header.immData = 1;
    header.flags = NTH_TWO_SIDE_SGL;
    header.dataLength = totalSize;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_SEND_RAW_SGL);
    do {
        result = worker->PostSendRawSgl(mSock, header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetAsyncEndpointSock::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post read as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post read as buff validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = OneSideValidation(request, mDriver)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post read as one side validation failed");
        return result;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq(); // do later change to NextReq()
    header.flags = NTH_READ;
    header.dataLength = sizeof(UBSHcomNetTransSgeIov);

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_READ);
    do {
        result = worker->PostRead(mSock, header, request);
        if (result == SS_OK) {
            NN_LOG_TRACE_INFO("Post read ep id " << mId << ", flag " << header.flags << ", seqNo " << header.seqNo <<
                ", size " << request.size);
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_READ, result);
    return result;
}

NResult NetAsyncEndpointSock::PostRead(const UBSHcomNetTransSglRequest &request)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post read sgl as state validation failed");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY((result = OneSideSglValidation(request, mDriver, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post read sgl as sgl validation failed");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_READ_SGL;
    header.dataLength = sizeof(request.iovCount) + sizeof(UBSHcomNetTransSgeIov) * request.iovCount;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_READ_SGL);
    do {
        result = worker->PostRead(mSock, header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_READ_SGL, result);
    return result;
}

NResult NetAsyncEndpointSock::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post write as state validation failed");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post write as buff validation failed");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY((result = OneSideValidation(request, mDriver)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post write as one side validation failed");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_WRITE;
    header.dataLength = sizeof(UBSHcomNetTransSgeIov) + request.size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_WRITE);
    do {
        result = worker->PostWrite(mSock, header, request);
        if (result == SS_OK) {
            NN_LOG_TRACE_INFO("Post write ep id " << mId << ", flag " << header.flags << ", seqNo " << header.seqNo <<
                ", size " << request.size);
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_WRITE, result);
    return result;
}

NResult NetAsyncEndpointSock::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post write sgl as state validation failed");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY((result = OneSideSglValidation(request, mDriver, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to async post write sgl as sgl validation failed");
        return NN_INVALID_PARAM;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_WRITE_SGL;
    header.dataLength = sizeof(request.iovCount) + sizeof(UBSHcomNetTransSgeIov) * request.iovCount + totalSize;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    auto worker = reinterpret_cast<SockWorker *>(mSock->UpContext1());
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_ASYNC_POST_WRITE_SGL);
    do {
        result = worker->PostWrite(mSock, header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_ASYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    TRACE_DELAY_END(SOCK_EP_ASYNC_POST_WRITE_SGL, result);
    return result;
}
}
}