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
#include "net_sock_sync_endpoint.h"

namespace ock {
namespace hcom {
NetSyncEndpointSock::NetSyncEndpointSock(uint64_t id, Sock *sock, NetDriverSockWithOOB *driver,
    const UBSHcomNetWorkerIndex &workerIndex)
    : NetEndpointImpl(id, workerIndex), mSock(sock), mDriver(driver)
{
    if (mSock != nullptr) {
        mSock->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize;
        mAllowedSize = mSegSize - sizeof(SockTransHeader);
        mDriver->IncreaseRef();

        mOpCtxInfoPool.Initialize(mDriver->GetOpCtxMemPool());
        mSglCtxInfoPool.Initialize(mDriver->GetSglCtxMemPool());
    }

    OBJ_GC_INCREASE(NetSyncEndpointSock);
}

NetSyncEndpointSock::~NetSyncEndpointSock()
{
    if (mSock != nullptr) {
        mSock->Close();
        mSock->DecreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
    }

    OBJ_GC_DECREASE(NetSyncEndpointSock);
    // do later
}

NResult NetSyncEndpointSock::SetEpOption(UBSHcomEpOptions &epOptions)
{
    if (mDefaultTimeout > 0 && epOptions.sendTimeout > mDefaultTimeout) {
        NN_LOG_WARN("send timeout should not longer than mDefaultTimeout " << mDefaultTimeout);
        return NN_ERROR;
    }

    if (NN_UNLIKELY(mSock->SetBlockingSendTimeout(epOptions.sendTimeout) != SS_OK)) {
        NN_LOG_WARN("Unable to set sock " << mSock->Name() << " timeout options");
        return NN_ERROR;
    }

    return NN_OK;
}

#define TIMEOUT_PROCESS()                  \
    do {                                   \
        mSock->Close();                    \
        mState.Set(NEP_BROKEN);            \
        mDriver->DestroyEndpointById(mId); \
    } while (0)

NResult NetSyncEndpointSock::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send with seqNo as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send with seqNo as buff validation failed");
        return result;
    }
    REQ_SIZE_VALIDATION();
    OPCODE_VALIDATION();

    UBSHcomNetTransHeader header {};
    header.dataLength = request.size;
    header.seqNo = seqNo == 0 ? NextSeq() : seqNo;
    header.flags = NTH_TWO_SIDE;
    header.opCode = opCode;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastFlag = header.flags;
    mLastSendSeqNo = header.seqNo;

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_SEND);
    do {
        result = mSock->PostSend(header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NetMonotonic::TimeNs() < finishTime && NeedRetry(result) && mDefaultTimeout != 0) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to sync post send request, result " << result);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpointSock::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send with opInfo as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send with opInfo as buff validation failed");
        return result;
    }
    REQ_SIZE_VALIDATION();
    OPCODE_VALIDATION();

    UBSHcomNetTransHeader header {};
    header.opCode = opCode;
    header.seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header.flags = ((uint16_t)opInfo.flags << NN_NO8) | ((uint16_t)NTH_TWO_SIDE);
    header.errorCode = opInfo.errorCode;
    header.timeout = opInfo.timeout;
    header.dataLength = request.size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastSendSeqNo = header.seqNo;
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_SEND);
    do {
        result = mSock->PostSend(header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to sync post send request with opInfo, result is " << result);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetSyncEndpointSock::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send raw as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send raw as buff validation failed");
        return result;
    }
    REQ_SIZE_VALIDATION();

    UBSHcomNetTransHeader header {};
    header.seqNo = seqNo == 0 ? NextSeq() : seqNo;
    header.immData = 1;
    header.flags = NTH_TWO_SIDE;
    header.dataLength = request.size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastSendSeqNo = header.seqNo;
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_SEND_RAW);
    do {
        result = mSock->PostSend(header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // sleep is not suitable for scenes like LWT
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to sync post send raw request, result " << result);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetSyncEndpointSock::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send raw sgl as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = TwoSideSglValidation(request, mDriver, mSegSize, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post send raw sgl as sgl validation failed");
        return result;
    }

    UBSHcomNetTransHeader header {};
    header.flags = NTH_TWO_SIDE_SGL;
    header.immData = 1;
    header.dataLength = totalSize;
    header.seqNo = seqNo == 0 ? NextSeq() : seqNo;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastSendSeqNo = header.seqNo;
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_SEND_RAW_SGL);
    do {
        result = mSock->PostSendSgl(header, request);
        if (result == SS_OK) {
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND_RAW_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // sleep is not suitable for scenes like LWT
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        break;
    } while (true);

    NN_LOG_ERROR("Failed to post send request, result " << result);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_SEND_RAW_SGL, result);
    return result;
}

#define RETURN_RESOURCES(opCtx)                                       \
    do {                                                              \
        (void)mSock->RemoveOpCtx((opCtx)->sendCtx->sendHeader.seqNo); \
        mSock->DecreaseRef();                                         \
        mSglCtxInfoPool.Return((opCtx)->sendCtx);                     \
        mOpCtxInfoPool.Return((opCtx));                               \
    } while (0)

NResult NetSyncEndpointSock::PostRead(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post read as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post read as buff validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = OneSideValidation(request, mDriver)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post read as one side validation failed");
        return result;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to post read with sock " << mSock->Name() << " as no ctx left");
        return SS_CTX_FULL;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with sock " << mSock->Name() << " as no sglCtx left");
        mOpCtxInfoPool.Return(ctx);
        return SS_CTX_FULL;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_READ;
    header.dataLength = sizeof(UBSHcomNetTransSgeIov);

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastFlag = header.flags;

    SockOpContextInfo::SockOpType opType = SockOpContextInfo::SockOpType::SS_READ;
    if (NN_UNLIKELY(FillReadWriteCtx(ctx, sglCtx, request, opType, header) != NN_OK)) {
        NN_LOG_ERROR("Failed to fill read ctx");
        mSglCtxInfoPool.Return(sglCtx);
        mOpCtxInfoPool.Return(ctx);
        return NN_INVALID_PARAM;
    }

    mSock->AddOpCtx(header.seqNo, ctx);
    mSock->IncreaseRef();

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_READ);
    do {
        result = mSock->PostRead(ctx);
        if (result == SS_OK) {
            mSglCtxInfoPool.Return(sglCtx);
            mOpCtxInfoPool.Return(ctx);
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    RETURN_RESOURCES(ctx);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_READ, result);
    return result;
}

NResult NetSyncEndpointSock::PostRead(const UBSHcomNetTransSglRequest &request)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post read sgl as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = OneSideSglValidation(request, mDriver, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post read sgl as sgl validation failed");
        return result;
    }

    auto opCtx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtx == nullptr)) {
        NN_LOG_ERROR("Failed to post read sgl with sock " << mSock->Name() << " as no op ctx left");
        return SS_CTX_FULL;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to post read sgl with sock " << mSock->Name() << " as no op ctx left");
        mOpCtxInfoPool.Return(opCtx);
        return SS_CTX_FULL;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_READ_SGL;
    header.dataLength = sizeof(request.iovCount) + sizeof(UBSHcomNetTransSgeIov) * request.iovCount;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();

    SockOpContextInfo::SockOpType opType = SockOpContextInfo::SockOpType::SS_SGL_READ;
    if (NN_UNLIKELY(FillReadWriteSglCtx(opCtx, sglCtx, request, opType, header) != NN_OK)) {
        NN_LOG_ERROR("Failed to fill read sgl ctx");
        mSglCtxInfoPool.Return(sglCtx);
        mOpCtxInfoPool.Return(opCtx);
        return NN_INVALID_PARAM;
    }

    mSock->AddOpCtx(header.seqNo, opCtx);
    mSock->IncreaseRef();
    bool readSglFlag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_READ_SGL);
    do {
        result = mSock->PostReadSgl(opCtx);
        if (result == SS_OK) {
            mSglCtxInfoPool.Return(sglCtx);
            mOpCtxInfoPool.Return(opCtx);
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_READ_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // sleep is not suitable for scenes like LWT
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        readSglFlag = false;
    } while (readSglFlag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    RETURN_RESOURCES(opCtx);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_READ_SGL, result);
    return result;
}

NResult NetSyncEndpointSock::PostWrite(const UBSHcomNetTransRequest &request)
{
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post write as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = BuffValidation(request)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post write as buff validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = OneSideValidation(request, mDriver)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post write as one side validation failed");
        return result;
    }

    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with sock " << mSock->Name() << " as no reqInfo left");
        return SS_CTX_FULL;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with sock " << mSock->Name() << " as no sglCtx left");
        mOpCtxInfoPool.Return(ctx);
        return SS_CTX_FULL;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_WRITE;
    header.dataLength = sizeof(UBSHcomNetTransSgeIov) + request.size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();

    SockOpContextInfo::SockOpType opType = SockOpContextInfo::SockOpType::SS_WRITE;
    if (NN_UNLIKELY(FillReadWriteCtx(ctx, sglCtx, request, opType, header) != NN_OK)) {
        NN_LOG_ERROR("Failed to fill write ctx");
        mSglCtxInfoPool.Return(sglCtx);
        mOpCtxInfoPool.Return(ctx);
        return NN_INVALID_PARAM;
    }

    mSock->AddOpCtx(header.seqNo, ctx);
    mSock->IncreaseRef();

    bool flag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_WRITE);
    do {
        result = mSock->PostWrite(ctx);
        if (result == SS_OK) {
            mSglCtxInfoPool.Return(sglCtx);
            mOpCtxInfoPool.Return(ctx);
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    RETURN_RESOURCES(ctx);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_WRITE, result);
    return result;
}

NResult NetSyncEndpointSock::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    size_t totalSize = 0;
    NResult result = NN_OK;
    if (NN_UNLIKELY((result = StateValidation(mState, mId, mDriver, mSock)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post write sgl as state validation failed");
        return result;
    }

    if (NN_UNLIKELY((result = OneSideSglValidation(request, mDriver, totalSize)) != NN_OK)) {
        NN_LOG_ERROR("Sock failed to sync post write sgl as sgl validation failed");
        return result;
    }

    auto opCtx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtx == nullptr)) {
        NN_LOG_ERROR("Failed to post write sgl with sock " << mSock->Name() << " as no op ctx left");
        return SS_PARAM_INVALID;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to post write sgl with sock " << mSock->Name() << " as no op ctx left");
        mOpCtxInfoPool.Return(opCtx);
        return SS_PARAM_INVALID;
    }

    UBSHcomNetTransHeader header {};
    header.seqNo = mSock->OneSideNextSeq();
    header.flags = NTH_WRITE_SGL;
    header.dataLength = sizeof(request.iovCount) + sizeof(UBSHcomNetTransSgeIov) * request.iovCount + totalSize;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
    mLastFlag = header.flags;

    uint64_t finishTime = GetFinishTime();

    SockOpContextInfo::SockOpType opType = SockOpContextInfo::SockOpType::SS_SGL_WRITE;
    if (NN_UNLIKELY(FillReadWriteSglCtx(opCtx, sglCtx, request, opType, header) != NN_OK)) {
        NN_LOG_ERROR("Failed to fill write sgl ctx");
        mSglCtxInfoPool.Return(sglCtx);
        mOpCtxInfoPool.Return(opCtx);
        return NN_INVALID_PARAM;
    }

    mSock->AddOpCtx(header.seqNo, opCtx);
    mSock->IncreaseRef();
    bool writeSglFlag = true;
    TRACE_DELAY_BEGIN(SOCK_EP_SYNC_POST_WRITE_SGL);
    do {
        result = mSock->PostWriteSgl(opCtx);
        if (result == SS_OK) {
            mSglCtxInfoPool.Return(sglCtx);
            mOpCtxInfoPool.Return(opCtx);
            TRACE_DELAY_END(SOCK_EP_SYNC_POST_WRITE_SGL, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        } else if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        // no retry result or timeout = 0
        writeSglFlag = false;
    } while (writeSglFlag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    RETURN_RESOURCES(opCtx);
    TRACE_DELAY_END(SOCK_EP_SYNC_POST_WRITE_SGL, result);
    return result;
}

static inline NResult WriteData(Sock *sock, SockTransHeader &header, SockOpContextInfo *originalCtx, void *buf)
{
    if (header.flags == NTH_READ_ACK) {
        if (originalCtx->sendCtx->iov[0].size != header.dataLength) {
            NN_LOG_ERROR("Failed to check sock with sock " << sock->Name() << " as size different.");
            return SS_PARAM_INVALID;
        }

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(originalCtx->sendCtx->iov[0].lAddress),
            originalCtx->sendCtx->iov[0].size, buf, originalCtx->sendCtx->iov[0].size) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy buf to sendCtx");
            return NN_INVALID_PARAM;
        }
    } else if (header.flags == NTH_READ_SGL_ACK) {
        /* write data */
        if (header.dataLength <
            (sizeof(UBSHcomNetTransSglRequest::iovCount) + sizeof(UBSHcomNetTransSgeIov) *
            originalCtx->sendCtx->iovCount)) {
            NN_LOG_ERROR("Failed to ReadSglAck as data size " << header.dataLength << " is less than iov size");
            return SS_PARAM_INVALID;
        }
        auto iovCount = reinterpret_cast<uint16_t *>(buf);
        if (*iovCount == 0 || *iovCount > NN_NO4 || *iovCount != originalCtx->sendCtx->iovCount) {
            NN_LOG_ERROR("Failed to check sock with sock " << sock->Name() << " as iov count is illegal.");
            return SS_PARAM_INVALID;
        }
        auto sgeIov =
            reinterpret_cast<UBSHcomNetTransSgeIov *>(reinterpret_cast<uintptr_t>(buf) +
            sizeof(UBSHcomNetTransSglRequest::iovCount));
        auto data = reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(buf) +
            sizeof(UBSHcomNetTransSglRequest::iovCount) + sizeof(UBSHcomNetTransSgeIov) * (*iovCount));

        uint32_t dataSize = 0;
        for (uint16_t i = 0; i < *iovCount; i++) {
            dataSize += sgeIov->size;
        }

        if (originalCtx->sendCtx->sendHeader.dataLength + dataSize != header.dataLength) {
            NN_LOG_ERROR("Failed to check sock with sock " << sock->Name() << " as size different.");
            return SS_PARAM_INVALID;
        }
        // do later check
        uint32_t copyOffset = 0;
        for (uint16_t i = 0; i < *iovCount; i++) {
            UBSHcomNetTransSgeIov iov = originalCtx->sendCtx->iov[i];
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(iov.lAddress), iov.size, data + copyOffset,
                iov.size) != NN_OK)) {
                NN_LOG_ERROR("Failed to copy data to iov");
                return NN_INVALID_PARAM;
            }
            copyOffset += iov.size;
        }
    }
    return NN_OK;
}

NResult NetSyncEndpointSock::WaitCompletion(int32_t timeout)
{
    if (mLastFlag == NTH_TWO_SIDE || mLastFlag == NTH_TWO_SIDE_SGL) {
        return NN_OK;
    }

    auto result = mSock->PostReceiveHeader(mRespCtx.mHeader, timeout);
    if (result != SS_OK) {
        if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        NN_LOG_ERROR("Failed to post receive header, result " << result);
        return result;
    }
    if (mRespCtx.mHeader.flags == NTH_READ_ACK || mRespCtx.mHeader.flags == NTH_READ_SGL_ACK) {
        auto msgReady = mRespMessage.AllocateIfNeed(mRespCtx.mHeader.dataLength);
        if (NN_UNLIKELY(!msgReady)) {
            NN_LOG_ERROR("Failed to allocate memory for response size " << mRespCtx.mHeader.dataLength <<
                ", probably out of memory");
            return NN_MALLOC_FAILED;
        }
        result = mSock->PostReceiveBody(mRespMessage.mBuf, mRespCtx.mHeader.dataLength, true);
        if (result != SS_OK) {
            NN_LOG_ERROR("Failed to receive body, result " << result << ", seqNo " << mRespCtx.mHeader.seqNo);
            return result;
        }
        NN_LOG_TRACE_INFO("Receive body successfully: sock " << mSock->Id() << ", head imm data " <<
            mRespCtx.mHeader.immData << ", flags " << mRespCtx.mHeader.flags << ", seqNo " << mRespCtx.mHeader.seqNo <<
            ", data len " << mRespCtx.mHeader.dataLength);

        auto originalReadCtx = mSock->RemoveOpCtx(mRespCtx.mHeader.seqNo);
        if (originalReadCtx == nullptr) {
            NN_LOG_ERROR("Failed to handle ack with sock " << mSock->Name() << " as invalid seqNo " <<
                mRespCtx.mHeader.seqNo);
            return SS_PARAM_INVALID;
        }
        if (originalReadCtx->sock != mSock) {
            NN_LOG_ERROR("Failed to check with sock " << mSock->Name() << " as sock different.");
            return SS_PARAM_INVALID;
        }

        return WriteData(mSock, mRespCtx.mHeader, originalReadCtx, mRespMessage.mBuf);
    } else if (mRespCtx.mHeader.flags == NTH_WRITE_ACK || mRespCtx.mHeader.flags == NTH_WRITE_SGL_ACK) {
        NN_LOG_TRACE_INFO("Post receive header successfully: sock " << mSock->Id() << ", head imm data " <<
            mRespCtx.mHeader.immData << ", flags " << mRespCtx.mHeader.flags << ", seqNo " << mRespCtx.mHeader.seqNo);

        auto originalWriteCtx = mSock->RemoveOpCtx(mRespCtx.mHeader.seqNo);
        if (originalWriteCtx == nullptr) {
            NN_LOG_ERROR("Failed to handle ack with sock " << mSock->Name() << " as invalid seqNo " <<
                mRespCtx.mHeader.seqNo);
            return SS_PARAM_INVALID;
        }
        if (originalWriteCtx->sock != mSock) {
            NN_LOG_ERROR("Failed to check with sock " << mSock->Name() << " as sock different.");
            return SS_PARAM_INVALID;
        }
        if (originalWriteCtx->sendCtx->sendHeader.dataLength != mRespCtx.mHeader.dataLength) {
            NN_LOG_ERROR("Failed to check sock with sock" << mSock->Name() << " as size different.");
            return SS_PARAM_INVALID;
        }
    }

    return NN_OK;
}

NResult NetSyncEndpointSock::Receive(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    auto result = mSock->PostReceiveHeader(mRespCtx.mHeader, timeout);
    if (result != SS_OK) {
        if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        NN_LOG_ERROR("Failed to post receive header, result " << result);
        return result;
    }
    NN_LOG_TRACE_INFO("Post receive header successfully: sock " << mSock->Id() << ", head imm data " <<
        mRespCtx.mHeader.immData << ", flags " << mRespCtx.mHeader.flags << ", seqNo " << mRespCtx.mHeader.seqNo);

    if (NN_UNLIKELY(mRespCtx.mHeader.seqNo != mLastSendSeqNo)) {
        NN_LOG_ERROR("Received un-matched seq no " << mRespCtx.mHeader.seqNo << ", demand seq no " << mLastSendSeqNo);
        return NN_SEQ_NO_NOT_MATCHED;
    }

    auto msgReady = mRespMessage.AllocateIfNeed(mRespCtx.mHeader.dataLength);
    if (NN_UNLIKELY(!msgReady)) {
        NN_LOG_ERROR("Failed to allocate memory for response size " << mRespCtx.mHeader.dataLength <<
            ", probably out of memory");
        return NN_MALLOC_FAILED;
    }

    result = mSock->PostReceiveBody(mRespMessage.mBuf, mRespCtx.mHeader.dataLength, false);
    if (result != SS_OK) {
        if (result == SS_TIMEOUT) {
            TIMEOUT_PROCESS();
        }
        NN_LOG_ERROR("Failed to receive body, result " << result << ", seqNo " << mRespCtx.mHeader.seqNo);
        return result;
    }
    NN_LOG_TRACE_INFO("Post receive body successfully: sock " << mSock->Id() << ", head imm data " <<
        mRespCtx.mHeader.immData << ", flags " << mRespCtx.mHeader.flags << ", seqNo " << mRespCtx.mHeader.seqNo <<
        ", data len " << mRespCtx.mHeader.dataLength);

    mRespMessage.mDataLen = mRespCtx.mHeader.dataLength;
    mRespCtx.mMessage = &mRespMessage;
    ctx.mHeader = mRespCtx.mHeader;
    ctx.mMessage = mRespCtx.mMessage;
    return NN_OK;
}

NResult NetSyncEndpointSock::ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    return Receive(timeout, ctx);
}
}
}