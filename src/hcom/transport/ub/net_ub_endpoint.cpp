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

#ifdef UB_BUILD_ENABLED

#include "net_ub_endpoint.h"
#include "ub_worker.h"

namespace ock {
namespace hcom {
#define STATE_VALIDATION(state, id, driver)                                                                           \
    do {                                                                                                              \
        if (NN_UNLIKELY(!(state).Compare(NEP_ESTABLISHED))) {                                                         \
            NN_LOG_ERROR("Endpoint " << (id) << " is not established, state is " <<                                   \
            UBSHcomNEPStateToString((state).Get()));                                                                  \
            return NN_EP_NOT_ESTABLISHED;                                                                             \
        }                                                                                                             \
                                                                                                                      \
        if (NN_UNLIKELY(!(driver)->IsStarted())) {                                                                    \
            NN_LOG_ERROR("Failed to validate state as driver " << (driver) << " is not started");                     \
            return NN_ERROR;                                                                                          \
        }                                                                                                             \
    } while (0)

#define LOCAL_REQUEST_VALIDATION(request)                                                              \
    do {                                                                                               \
        if (NN_UNLIKELY((request).lAddress == 0 || (request).size == 0)) {                             \
            NN_LOG_ERROR("Failed to validate request as source data is null or size is zero");         \
            return UB_PARAM_INVALID;                                                                   \
        }                                                                                              \
        if (NN_UNLIKELY((request).upCtxSize > sizeof(UBOpContextInfo::upCtx))) {                       \
            NN_LOG_ERROR("Failed to validate request as up ctx size invalid " << (request).upCtxSize); \
            return UB_PARAM_INVALID;                                                                   \
        }                                                                                              \
    } while (0)

#define SIZE_VALIDATION(request, allowedSize)                                             \
    do {                                                                                  \
        size_t compareSize = (request).size;                                              \
        if (mIsNeedEncrypt) {                                                             \
            compareSize = mAes.EstimatedEncryptLen((request).size);                       \
        }                                                                                 \
                                                                                          \
        if (NN_UNLIKELY(compareSize > (allowedSize))) {                                   \
            NN_LOG_ERROR("Failed to post message as message size " << ((request).size) << \
                " is too large, use one side post");                                      \
            return NN_TWO_SIDE_MESSAGE_TOO_LARGE;                                         \
        }                                                                                 \
    } while (0)

#define POST_SEND_VALIDATION(state, id, driver, opCode, request, allowedSize)                             \
    do {                                                                                                  \
        STATE_VALIDATION(state, id, driver);                                                              \
        LOCAL_REQUEST_VALIDATION(request);                                                                \
        SIZE_VALIDATION(request, allowedSize);                                                            \
        if (NN_UNLIKELY((opCode) >= MAX_OPCODE)) {                                                        \
            NN_LOG_ERROR("Failed to post message as opcode is invalid, which should with the range 0~" << \
                (MAX_OPCODE - 1));                                                                        \
            return NN_INVALID_OPCODE;                                                                     \
        }                                                                                                 \
    } while (0)

#define POST_SEND_RAW_VALIDATION(state, id, driver, seqNo, request, allowedSize) \
    do {                                                                         \
        STATE_VALIDATION(state, id, driver);                                     \
        LOCAL_REQUEST_VALIDATION(request);                                       \
        SIZE_VALIDATION(request, allowedSize);                                   \
        if (NN_UNLIKELY((seqNo) == 0)) {                                         \
            NN_LOG_ERROR("Failed to post raw message as seqNo must > 0");        \
            return UB_PARAM_INVALID;                                             \
        }                                                                        \
    } while (0)

#define READ_WRITE_VALIDATION(state, id, driver, request)                                                  \
    do {                                                                                                   \
        STATE_VALIDATION(state, id, driver);                                                               \
        LOCAL_REQUEST_VALIDATION(request);                                                                 \
        if (NN_UNLIKELY((request).rAddress == 0)) {                                                        \
            NN_LOG_ERROR("Failed to validate request as remote data is null");                             \
            return UB_PARAM_INVALID;                                                                       \
        }                                                                                                  \
        if (NN_OK != (driver)->ValidateMemoryRegion((request).lKey, (request).lAddress, (request).size)) { \
            NN_LOG_ERROR("Invalid MemoryRegion or local key");                                             \
            return NN_INVALID_LKEY;                                                                        \
        }                                                                                                  \
    } while (0)

#define SGL_VALIDATION(request, totalSize)                                                               \
    do {                                                                                                 \
        if (NN_UNLIKELY((request).iov == nullptr || (request).iovCount > NET_SGE_MAX_IOV ||              \
            (request).iovCount == 0)) {                                                                  \
            NN_LOG_ERROR("Invalid iov ptr:" << (request).iov << " or iov cnt:" << (request).iovCount);   \
            return UB_PARAM_INVALID;                                                                     \
        }                                                                                                \
        if (NN_UNLIKELY((request).upCtxSize > sizeof(UBOpContextInfo::upCtx))) {                         \
            NN_LOG_ERROR("Failed to validate request as up ctx size invalid " << (request).upCtxSize);   \
            return UB_PARAM_INVALID;                                                                     \
        }                                                                                                \
        for (int i = 0; i < (request).iovCount; ++i) {                                                   \
            if (NN_OK != mDriver->ValidateMemoryRegion((request).iov[i].lKey, (request).iov[i].lAddress, \
                (request).iov[i].size)) {                                                                \
                NN_LOG_ERROR("Invalid MemoryRegion or lKey in iov in async PostWrite");                  \
                return NN_INVALID_LKEY;                                                                  \
            }                                                                                            \
            (totalSize) += (request).iov[i].size;                                                        \
        }                                                                                                \
    } while (0)

#define READ_WRITE_SGL_VALIDATION(state, id, driver, request)                                   \
    do {                                                                                        \
        STATE_VALIDATION(state, id, driver);                                                    \
        size_t tmpTotalSize = 0;                                                                \
        SGL_VALIDATION(request, tmpTotalSize);                                                  \
        for (int i = 0; i < (request).iovCount; ++i) {                                          \
            if (NN_UNLIKELY((request).iov[i].rAddress == NN_NO0)) {                             \
                NN_LOG_ERROR("Failed to validate request as remote data is null, index " << i); \
                return UB_PARAM_INVALID;                                                        \
            }                                                                                   \
        }                                                                                       \
    } while (0)

#define POST_SEND_SGL_VALIDATION(state, id, driver, seqNo, request, allowedSize, totalSize) \
    do {                                                                                    \
        STATE_VALIDATION(state, id, driver);                                                \
        if (NN_UNLIKELY((seqNo) == 0)) {                                                    \
            NN_LOG_ERROR("Failed to post raw message as seqNo must > 0");                   \
            return UB_PARAM_INVALID;                                                        \
        }                                                                                   \
                                                                                            \
        SGL_VALIDATION(request, (totalSize));                                               \
        size_t compareSize = (totalSize);                                                   \
        if (mIsNeedEncrypt) {                                                               \
            compareSize = mAes.EstimatedEncryptLen((totalSize));                            \
        }                                                                                   \
                                                                                            \
        if (NN_UNLIKELY(compareSize > (allowedSize))) {                                     \
            NN_LOG_ERROR("Failed to post send raw sgl as message size " << compareSize <<   \
                " is too large, use one side post");                                        \
            return NN_TWO_SIDE_MESSAGE_TOO_LARGE;                                           \
        }                                                                                   \
    } while (0)

#define ENCRYPT_RAW_SGL(tlsReq, mrBufAddress, size, mAes, mDriver)                                         \
    do {                                                                                                   \
        uintptr_t tmpBuff = 0;                                                                             \
        if (NN_UNLIKELY(!(mDriver)->mDriverSendMR->GetFreeBuffer(tmpBuff))) {                              \
            NN_LOG_ERROR("Failed to post message as failed to get tmp mr buffer from pool from driver " << \
                (mDriver)->Name());                                                                        \
            return NN_GET_BUFF_FAILED;                                                                     \
        }                                                                                                  \
                                                                                                           \
        uint32_t iovOffset = 0;                                                                            \
        for (int i = 0; i < request.iovCount; i++) {                                                       \
            if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(tmpBuff + iovOffset), request.iov[i].size,   \
                reinterpret_cast<const void *>(request.iov[i].lAddress), request.iov[i].size) != NN_OK)) { \
                NN_LOG_ERROR("Failed to copy request to buff");                                            \
                return NN_ERROR;                                                                           \
            }                                                                                              \
            iovOffset += request.iov[i].size;                                                              \
        }                                                                                                  \
                                                                                                           \
        if (NN_UNLIKELY(!(mDriver)->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {                         \
            NN_LOG_ERROR("Failed to post message as failed to get mr buffer from pool from driver " <<     \
                (mDriver)->Name());                                                                        \
            (void)(mDriver)->mDriverSendMR->ReturnBuffer(tmpBuff);                                         \
            return NN_GET_BUFF_FAILED;                                                                     \
        }                                                                                                  \
                                                                                                           \
        uint32_t cipherLen = 0;                                                                            \
        if (!(mAes).Encrypt(mSecrets, reinterpret_cast<void *>(tmpBuff), size,                             \
            reinterpret_cast<void *>(mrBufAddress), cipherLen)) {                                          \
            NN_LOG_ERROR("Failed to post send message as encryption failure");                             \
            (void)(mDriver)->mDriverSendMR->ReturnBuffer(tmpBuff);                                         \
            (void)(mDriver)->mDriverSendMR->ReturnBuffer(mrBufAddress);                                    \
            return NN_ENCRYPT_FAILED;                                                                      \
        }                                                                                                  \
                                                                                                           \
        (tlsReq).lAddress = mrBufAddress;                                                                  \
        (tlsReq).lKey = (mDriver)->mDriverSendMR->GetLKey();                                               \
        (tlsReq).srcSeg = (mDriver)->mDriverSendMR->GetMemorySeg();                                        \
        (tlsReq).size = cipherLen;                                                                         \
        (size) = cipherLen;                                                                                \
                                                                                                           \
        (void)(mDriver)->mDriverSendMR->ReturnBuffer(tmpBuff);                                             \
    } while (0)

static inline GetSglTseg(NetDriverUBWithOob *driver, UBSHcomNetTransSglRequest &sglReq)
{
    for (uint16_t i = 0; i < sglReq.iovCount; i++) {
        urma_target_seg_t *tseg = nullptr;
        if (driver->GetTseg(sglReq.iov[i].lKey, tseg) != NN_OK) {
            NN_LOG_ERROR("Failed to post read request, as get tseg failed");
            return UB_PARAM_INVALID;
        }
        sglReq.iov[i].srcSeg = static_cast<void *>(tseg);
    }
    return NN_OK;
}

NetUBAsyncEndpoint::NetUBAsyncEndpoint(uint64_t id, UBJetty *qp, NetDriverUBWithOob *driver, UBWorker *worker)
    : NetEndpointImpl(id, worker != nullptr ? worker->Index() : UBSHcomNetWorkerIndex{}),
      mJetty(qp), mWorker(worker), mDriver(driver)
{
    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mWorker != nullptr) {
        mWorker->IncreaseRef();
    }

    if (mJetty != nullptr) {
        mJetty->IncreaseRef();
        mIsNeedSendHb = mJetty->GetExchangeInfo().isNeedSendHb;
    }

    if (mJetty != nullptr && mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize < mJetty->GetPostSendMaxSize() ?
            mDriver->mOptions.mrSendReceiveSegSize :
            mJetty->GetPostSendMaxSize();
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
        mDmSize = mDriver->mOptions.dmSegSize;
        mSendRawAllowedSize = mSegSize < NN_NO65536 ? mSegSize : NN_NO65536;
    }

    if (mIsNeedSendHb && mDriver != nullptr) {
        mHeartBeatIdleTime = mDriver->GetHbIdleTime();
        UpdateTargetHbTime();
    }

    OBJ_GC_INCREASE(NetUBAsyncEndpoint);
}

NetUBAsyncEndpoint::~NetUBAsyncEndpoint()
{
    // jetty 析构时要求 worker、driver都存活
    if (mJetty != nullptr) {
        // 当 EP 析构时，说明它不再被用户使用、已经从全局 EP 表中被删除、上层的 channel 也被 DelayEraseChannel 真正删除。
        // 如果存在 UBJetty 的 PostedCount > 0，说明存在过在 FLUSH_ERR_DONE 之后用户绕过了 EP 和 jetty 的状态检查进行
        // post 的情况。
        //
        // \see NetDriverUBWithOob::ProcessEpError
        if (mJetty->GetPostedCount() > 0) {
            NN_LOG_WARN("There are OPs posted though jetty is in error state, flushing...");
            mJetty->Flush();
        }

        mJetty->DecreaseRef();
        mJetty = nullptr;
    }

    // worker 会使用 driver 层注册的函数
    if (mWorker != nullptr) {
        mWorker->DecreaseRef();
        mWorker = nullptr;
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
        mDriver = nullptr;
    }
    OBJ_GC_DECREASE(NetUBAsyncEndpoint);
}

NResult NetUBAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize);
    // get mr from pool
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with seq no as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    header->flags = NTH_TWO_SIDE;

    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress +
            sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to async post send with seq no as encryption failure");
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            request.size, reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            NN_LOG_ERROR("Failed to async post send with seq no as memcpy fail");
            return NN_ERROR;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    // change lAddress to mrAddress and set lKey
    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());

    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto sendFlag = true;
    uint64_t finishTime = GetFinishTime();
    NResult result = NN_OK;
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mJetty, ubReq,
            reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, result);
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
    TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize);
    // get mr from pool
    NResult res = NN_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with op info as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->opCode = opCode;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;

    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress +
            sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Failed to async post send with op info as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        header->dataLength = request.size;
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            request.size, reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            NN_LOG_ERROR("Failed to async post send with op info as memcpy fail");
            return NN_ERROR;
        }
    }
    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());

    auto sendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_SEND);
    do {
        res = worker->PostSend(mJetty, ubReq,
            reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (res == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, res);
            return NN_OK;
        } else if (NeedRetry(res) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry res or timeout = 0
        sendOpFlag = false;
    } while (sendOpFlag);

    NN_LOG_ERROR("Failed to async post send with op info, result " << res);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, res);
    return res;
}

NResult NetUBAsyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo, const UBSHcomExtHeaderType extHeaderType, const void *extHeader,
    uint32_t extHeaderSize)
{
    if (NN_UNLIKELY(extHeaderType == UBSHcomExtHeaderType::RAW)) {
        NN_LOG_ERROR("Should not use RAW type when extHeader is given.");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!extHeader)) {
        NN_LOG_ERROR("The extHeader is invalid.");
        return NN_INVALID_PARAM;
    }

    // 保证 extHeaderSize + request.size <= mAllowedSize.
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize - extHeaderSize);

    // get mr from pool
    NResult result = NN_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with opInfo as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->timeout = opInfo.timeout;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->errorCode = opInfo.errorCode;
    header->dataLength = request.size + extHeaderSize;
    header->extHeaderType = extHeaderType;

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
        NN_LOG_ERROR("Failed to copy request to mrBufAddress in async ep");
        return NN_INVALID_PARAM;
    }

    // 头部全部写入完毕后才生成 crc32
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);

    // lAddress -> mrAddress
    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;
    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());

    auto sendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_SEND);
    do {
        result = worker->PostSend(mJetty, ubReq,
                                  reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);  // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        sendOpFlag = false;
    } while (sendOpFlag);

    NN_LOG_ERROR("Failed to async post send with op info, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostSendSglInline(
    uint16_t opCode, const UBSHcomNetTransRequest &request, const UBSHcomNetTransOpInfo &opInfo)
{
    // 仅支持UBC，同时需要加密必定会涉及到内存拷贝，仍然走非inline方式
    if (mIsNeedEncrypt || mJetty->GetProtocol() != UBSHcomNetDriverProtocol::UBC) {
        return PostSend(opCode, request, opInfo);
    }

    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize);

    NResult result = NN_OK;
    UBSHcomNetTransHeader header;
    header.opCode = opCode;
    header.seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header.flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header.timeout = opInfo.timeout;
    header.errorCode = opInfo.errorCode;
    header.dataLength = request.size;
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    bool sendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    do {
        result = worker->PostSendSglInline(mJetty, header, request);
        if (result == UB_OK) {
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        sendOpFlag = false;
    } while (sendOpFlag);
    return result;
}

NResult NetUBAsyncEndpoint::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    POST_SEND_RAW_VALIDATION(mState, mId, mDriver, seqNo, request, mSendRawAllowedSize);

    /* get mr from pool */
    NResult result = UB_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post message as failed to get mr buffer from pool from driver " << mDriver->Name());
        return NN_GET_BUFF_FAILED;
    }

    size_t msgSize = 0;
    if (!mIsNeedEncrypt) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress), request.size,
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to copy request to send mr");
            return UB_PARAM_INVALID;
        }
        msgSize = request.size;
    } else {
        uint32_t cipherLen = 0;
        result = mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress), cipherLen);
        if (!result) {
            NN_LOG_ERROR("Failed to send raw message as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        msgSize = cipherLen;
    }

    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = msgSize;

    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    auto sendRawAsyncFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_SEND_RAW);
    do {
        result = worker->PostSend(mJetty, ubReq,
            reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()), seqNo);
        if (NN_LIKELY(result == UB_OK)) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND_RAW, result);
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
    TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t size = 0;
    POST_SEND_SGL_VALIDATION(mState, mId, mDriver, seqNo, request, mSendRawAllowedSize, size);
    UBSHcomNetTransSglRequest sglReq = request;
    if (GetSglTseg(mDriver, sglReq) != NN_OK) {
        NN_LOG_ERROR("GetSglTseg failed");
        return UB_PARAM_INVALID;
    }

    UBSHcomNetTransRequest tlsReq {}; // used in encryption, to do...
    uintptr_t mrBufAddress = 0;
    if (mIsNeedEncrypt) {
        ENCRYPT_RAW_SGL(tlsReq, mrBufAddress, size, mAes, mDriver);
    }

    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    NResult result = NN_OK;
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_SEND_RAW_SGL);
    do {
        result = worker->PostSendSgl(mJetty, request, tlsReq, seqNo, mIsNeedEncrypt);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND_RAW_SGL, result);
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

    NN_LOG_ERROR("Failed to post send raw sgl request, result: " << result);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostRead(const UBSHcomNetTransRequest &request)
{
    READ_WRITE_VALIDATION(mState, mId, mDriver, request);
    UBSHcomNetTransRequest reqInner = request;
    urma_target_seg_t *tseg = nullptr;
    if (mDriver->GetTseg(request.lKey, tseg) != NN_OK) {
        NN_LOG_ERROR("Failed to post read request, as get tseg failed.");
        return UB_PARAM_INVALID;
    }
    reqInner.srcSeg = static_cast<void *>(tseg);
    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    NResult result = NN_OK;
    auto asyncReadFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_READ);
    do {
        result = worker->PostRead(mJetty, reqInner);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        asyncReadFlag = false;
    } while (asyncReadFlag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_READ, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostRead(const UBSHcomNetTransSglRequest &request)
{
    READ_WRITE_SGL_VALIDATION(mState, mId, mDriver, request);

    UBSHcomNetTransSglRequest sglReq = request;
    if (GetSglTseg(mDriver, sglReq) != NN_OK) {
        NN_LOG_ERROR("Failed to get sgl tseg");
        return UB_PARAM_INVALID;
    }

    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    NResult result = UB_OK;
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_READ_SGL);
    do {
        result = worker->PostOneSideSgl(mJetty, sglReq);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_READ_SGL, result);
            return UB_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_READ_SGL, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostWrite(const UBSHcomNetTransRequest &request)
{
    READ_WRITE_VALIDATION(mState, mId, mDriver, request);
    UBSHcomNetTransRequest reqInner = request;
    urma_target_seg_t *tseg = nullptr;
    if (mDriver->GetTseg(request.lKey, tseg) != NN_OK) {
        NN_LOG_ERROR("Failed to post read request, as get tseg failed");
        return UB_PARAM_INVALID;
    }
    reqInner.srcSeg = static_cast<void *>(tseg);
    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());

    NResult result = NN_OK;
    auto asyncWriteFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_WRITE);
    do {
        result = worker->PostWrite(mJetty, reqInner);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        asyncWriteFlag = false;
    } while (asyncWriteFlag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_WRITE, result);
    return result;
}

NResult NetUBAsyncEndpoint::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    READ_WRITE_SGL_VALIDATION(mState, mId, mDriver, request);

    UBSHcomNetTransSglRequest sglReq = request;
    if (GetSglTseg(mDriver, sglReq) != NN_OK) {
        NN_LOG_ERROR("GetSglTseg failed");
        return UB_PARAM_INVALID;
    }

    auto worker = reinterpret_cast<UBWorker *>(mJetty->GetUpContext1());
    NResult result = UB_OK;
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_ASYNC_POST_WRITE_SGL);
    do {
        result = worker->PostOneSideSgl(mJetty, sglReq, false);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_ASYNC_POST_WRITE_SGL, result);
            return UB_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    TRACE_DELAY_END(UB_EP_ASYNC_POST_WRITE_SGL, result);
    return result;
}

void NetUBAsyncEndpoint::UpdateTargetHbTime()
{
    mTargetHbTime = NetMonotonic::TimeSec() + mHeartBeatIdleTime;
}

NetUBSyncEndpoint::NetUBSyncEndpoint(uint64_t id, UBJetty *qp, UBJfc *cq, uint32_t ubOpCtxPoolSize,
    NetDriverUBWithOob *driver, const UBSHcomNetWorkerIndex &workerIndex)
    : NetEndpointImpl(id, workerIndex), mJetty(qp), mJfc(cq), mCtxPool("ctxPool", ubOpCtxPoolSize), mDriver(driver)
{
    if (mJetty != nullptr) {
        mJetty->IncreaseRef();
    }

    if (mJfc != nullptr) {
        mJfc->IncreaseRef();
    }

    if (mDriver != nullptr) {
        mDriver->IncreaseRef();
    }

    if (mJetty != nullptr && mDriver != nullptr) {
        mSegSize = mDriver->mOptions.mrSendReceiveSegSize < mJetty->GetPostSendMaxSize() ?
            mDriver->mOptions.mrSendReceiveSegSize :
            mJetty->GetPostSendMaxSize();
        mAllowedSize = mSegSize - sizeof(UBSHcomNetTransHeader);
        mDmSize = mDriver->mOptions.dmSegSize;
        mSendRawAllowedSize = mSegSize < NN_NO65536 ? mSegSize : NN_NO65536;
    }

    /* set worker index and group index to 0xFFFF */
    mWorkerIndex.idxInGrp = INVALID_WORKER_INDEX;
    mWorkerIndex.grpIdx = INVALID_WORKER_GROUP_INDEX;

    OBJ_GC_INCREASE(NetUBSyncEndpoint);
}

NetUBSyncEndpoint::~NetUBSyncEndpoint()
{
    if (mJetty != nullptr) {
        mJetty->DecreaseRef();
        mJetty = nullptr;
    }

    if (mDriver != nullptr) {
        mDriver->DecreaseRef();
        mDriver = nullptr;
    }

    OBJ_GC_DECREASE(NetUBSyncEndpoint);
}

NResult NetUBSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO)
{
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize);

    // get mr from pool
    NResult result = NN_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to sync post send with seq no as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    // copy message
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->seqNo = seqNO == 0 ? NextSeq() : seqNO;
    header->opCode = opCode;
    header->flags = NTH_TWO_SIDE;
    header->dataLength = request.size;

    mLastSendSeqNo = header->seqNo;
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress +
            sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Failed to sync post send with seq no as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        // copy message
        header->dataLength = request.size;

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            request.size, reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            NN_LOG_ERROR("Failed to sync post send with seq no as memcpy fail");
            return NN_ERROR;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    mDemandPollingOpType = UBOpContextInfo::SEND;

    // post request
    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto syncSendFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_SEND);
    do {
        result = InnerPostSend(ubReq, reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        syncSendFlag = false;
    } while (syncSendFlag);

    NN_LOG_ERROR("Failed to sync post send with seq no, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetUBSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo)
{
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize);

    // get mr from pool
    NResult result = NN_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to sync post send with opInfo as failed to get mr buffer from pool");
        return NN_GET_BUFF_FAILED;
    }

    // copy message
    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint16_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->dataLength = request.size;
    header->errorCode = opInfo.errorCode;

    mLastSendSeqNo = header->seqNo;
    if (mIsNeedEncrypt) {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress +
            sizeof(UBSHcomNetTransHeader)), cipherLen)) {
            NN_LOG_ERROR("Failed to sync post send with op info as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        header->dataLength = cipherLen;
    } else {
        // copy message
        header->dataLength = request.size;

        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress + sizeof(UBSHcomNetTransHeader)),
            request.size, reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            NN_LOG_ERROR("Failed to sync post send with op info as memcpy fail");
            return NN_ERROR;
        }
    }

    /* finally fill header crc */
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    mDemandPollingOpType = UBOpContextInfo::SEND;

    // post request
    // change lAddress to mrAddress and set lKey
    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto syncSendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_SEND);
    do {
        result = InnerPostSend(ubReq, reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
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
    TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetUBSyncEndpoint::PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
    const UBSHcomNetTransOpInfo &opInfo, const UBSHcomExtHeaderType extHeaderType, const void *extHeader,
    uint32_t extHeaderSize)
{
    if (NN_UNLIKELY(extHeaderType == UBSHcomExtHeaderType::RAW)) {
        NN_LOG_ERROR("RAW type should not be used when extHeader is given.");
        return NN_INVALID_PARAM;
    }

    if (NN_UNLIKELY(!extHeader)) {
        NN_LOG_ERROR("The ExtHeader is invalid.");
        return NN_INVALID_PARAM;
    }

    // 保证 extHeaderSize + request.size <= mAllowedSize.
    POST_SEND_VALIDATION(mState, mId, mDriver, opCode, request, mAllowedSize - extHeaderSize);

    // get mr from pool
    NResult result = NN_OK;
    uintptr_t mrBufAddress = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to async post send with op info as get mr buffer from send mr pool failed");
        return NN_GET_BUFF_FAILED;
    }

    auto *header = reinterpret_cast<UBSHcomNetTransHeader *>(mrBufAddress);
    bzero(header, sizeof(UBSHcomNetTransHeader));
    header->opCode = opCode;
    header->seqNo = opInfo.seqNo == 0 ? NextSeq() : opInfo.seqNo;
    header->flags = ((uint16_t)opInfo.flags << NN_NO8) | (uint64_t)NTH_TWO_SIDE;
    header->timeout = opInfo.timeout;
    header->errorCode = opInfo.errorCode;
    header->extHeaderType = extHeaderType;
    header->dataLength = request.size + extHeaderSize;

    mLastSendSeqNo = header->seqNo;
    if (mIsNeedEncrypt) {
        NN_LOG_WARN("postsent encrypt is not supported now.");
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

    // 头部全部写入完毕后才生成 crc32
    header->headerCrc = NetFunc::CalcHeaderCrc32(header);
    mDemandPollingOpType = UBOpContextInfo::SEND;

    // lAddress -> mrAddress
    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = sizeof(UBSHcomNetTransHeader) + header->dataLength;

    auto syncSendOpFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_SEND);
    do {
        result = InnerPostSend(ubReq, reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()));
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        syncSendOpFlag = false;
    } while (syncSendOpFlag);

    NN_LOG_ERROR("Failed to async post send with op info, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(UB_EP_SYNC_POST_SEND, result);
    return result;
}

NResult NetUBSyncEndpoint::PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo)
{
    POST_SEND_RAW_VALIDATION(mState, mId, mDriver, seqNo, request, mSendRawAllowedSize);

    /* get mr from pool */
    NResult result = UB_OK;
    uintptr_t mrBufAddress = 0;
    size_t msgSize = 0;
    if (NN_UNLIKELY(!mDriver->mDriverSendMR->GetFreeBuffer(mrBufAddress))) {
        NN_LOG_ERROR("Failed to post raw message as failed to get mr buffer from pool from driver " << mDriver->Name());
        return UB_MEMORY_ALLOCATE_FAILED;
    }

    if (!mIsNeedEncrypt) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(mrBufAddress), request.size,
            reinterpret_cast<const void *>(request.lAddress), request.size) != NN_OK)) {
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            NN_LOG_ERROR("Failed to copy request to mrBufAddress");
            return UB_PARAM_INVALID;
        }
        msgSize = request.size;
    } else {
        uint32_t cipherLen = 0;
        if (!mAes.Encrypt(mSecrets,
            (void *)request.lAddress, request.size, reinterpret_cast<void *>(mrBufAddress), cipherLen)) {
            NN_LOG_ERROR("Failed send message as encryption failure");
            mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
            return NN_ENCRYPT_FAILED;
        }
        msgSize = cipherLen;
    }

    UBSHcomNetTransRequest ubReq = request;
    ubReq.lAddress = mrBufAddress;
    ubReq.lKey = mDriver->mDriverSendMR->GetLKey();
    ubReq.size = msgSize;

    // 在 SEND_RAW 下，seqNo 不能为 0, 即表明 InnerPostSend 将会使用 `UBOpContextInfo::SEND_RAW`
    mDemandPollingOpType = UBOpContextInfo::SEND_RAW;

    mLastSendSeqNo = seqNo;

    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_SEND_RAW);
    do {
        result = InnerPostSend(ubReq, reinterpret_cast<urma_target_seg_t *>(mDriver->mDriverSendMR->GetMemorySeg()),
            seqNo);
        if (NN_LIKELY(result == UB_OK)) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_SEND_RAW, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL);
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    NN_LOG_ERROR("Failed to post raw send request, result " << result);
    mDriver->mDriverSendMR->ReturnBuffer(mrBufAddress);
    TRACE_DELAY_END(UB_EP_SYNC_POST_SEND_RAW, result);
    return result;
}

NResult NetUBSyncEndpoint::InnerPostSendSgl(const UBSendSglRWRequest &req, const UBSendReadWriteRequest &tlsReq,
    uint32_t immData)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Failed to InnerPostSendSgl with NetUBSyncEndpoint as jetty is null");
        return UB_PARAM_INVALID;
    }

    static thread_local UBSglContextInfo sglCtx;
    sglCtx.result = UB_OK;
    sglCtx.qp = mJetty;
    if (NN_UNLIKELY(memcpy_s(sglCtx.iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV,
        req.iov, sizeof(UBSHcomNetTransSgeIov) * req.iovCount) != UB_OK)) {
        NN_LOG_ERROR("InnerPostSendSgl failed to copy the UBSHcomNetTransSgeIov to sglCtx");
        return UB_PARAM_INVALID;
    }
    sglCtx.iovCount = req.iovCount;
    sglCtx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
            NN_LOG_ERROR("InnerPostSendSgl Failed to copy req to sglCtx");
            return UB_PARAM_INVALID;
        }
    }
    static thread_local UBOpContextInfo ctx;
    // if not encrypt reqTls lAddress\size\lKey is 0
    ctx.dataSize = tlsReq.size;
    ctx.mrMemAddr = tlsReq.lAddress;
    ctx.ubJetty = mJetty;
    ctx.qpNum = mJetty->QpNum();
    ctx.opType = UBOpContextInfo::SEND_RAW_SGL;
    ctx.opResultType = UBOpContextInfo::SUCCESS;
    ctx.upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
    auto upCtx = reinterpret_cast<UBSgeCtxInfo *>(&ctx.upCtx);
    upCtx->ctx = &sglCtx;
    UBSHcomNetTransSglRequest sglReq = req;
    if (GetSglTseg(mDriver, sglReq) != NN_OK) {
        NN_LOG_ERROR("GetSglTseg failed");
        return UB_PARAM_INVALID;
    }
    mJetty->IncreaseRef();

    auto result = mJetty->PostSendSgl(sglReq.iov, sglReq.iovCount, reinterpret_cast<uint64_t>(&ctx), immData);
    if (NN_UNLIKELY(result != UB_OK)) {
        mJetty->DecreaseRef();
    }

    return result;
}

NResult NetUBSyncEndpoint::PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo)
{
    size_t size = 0;
    POST_SEND_SGL_VALIDATION(mState, mId, mDriver, seqNo, request, mSendRawAllowedSize, size);

    UBSHcomNetTransRequest tlsReq{};
    uintptr_t mrBufAddress = 0;
    if (mIsNeedEncrypt) {
        ENCRYPT_RAW_SGL(tlsReq, mrBufAddress, size, mAes, mDriver);
    }

    mDemandPollingOpType = UBOpContextInfo::SEND_RAW_SGL;
    NResult result = UB_OK;
    mLastSendSeqNo = seqNo;
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_SEND_RAW_SGL);
    do {
        result = InnerPostSendSgl(request, tlsReq, seqNo);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_SEND_RAW_SGL, result);
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

    NN_LOG_ERROR("NetUBSyncEndpoint Failed to post send raw sgl request, result " << result);
    TRACE_DELAY_END(UB_EP_SYNC_POST_SEND_RAW_SGL, result);
    return result;
}

NResult NetUBSyncEndpoint::PostRead(const UBSHcomNetTransRequest &request)
{
    READ_WRITE_VALIDATION(mState, mId, mDriver, request);

    mDemandPollingOpType = UBOpContextInfo::READ;
    NResult result = NN_OK;
    auto readFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_READ);
    do {
        result = InnerPostRead(request);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_READ, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        readFlag = false;
    } while (readFlag);

    NN_LOG_ERROR("Failed to post read request, result " << result);
    TRACE_DELAY_END(UB_EP_SYNC_POST_READ, result);
    return result;
}

NResult NetUBSyncEndpoint::PostRead(const UBSHcomNetTransSglRequest &request)
{
    READ_WRITE_SGL_VALIDATION(mState, mId, mDriver, request);

    mDemandPollingOpType = UBOpContextInfo::SGL_READ;
    NResult result = UB_OK;
    auto readSglFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_READ_SGL);
    do {
        result = PostOneSideSgl(request);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_READ_SGL, result);
            return UB_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        readSglFlag = false;
    } while (readSglFlag);

    NN_LOG_ERROR("Failed to post read sgl request, result " << result);
    TRACE_DELAY_END(UB_EP_SYNC_POST_READ_SGL, result);
    return result;
}

NResult NetUBSyncEndpoint::PostWrite(const UBSHcomNetTransRequest &request)
{
    READ_WRITE_VALIDATION(mState, mId, mDriver, request);

    mDemandPollingOpType = UBOpContextInfo::WRITE;
    NResult result = NN_OK;
    auto writeFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_WRITE);
    do {
        result = InnerPostWrite(request);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_WRITE, result);
            return NN_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        writeFlag = false;
    } while (writeFlag);

    NN_LOG_ERROR("Failed to post write request, result " << result);
    TRACE_DELAY_END(UB_EP_SYNC_POST_WRITE, result);
    return result;
}

NResult NetUBSyncEndpoint::PostWrite(const UBSHcomNetTransSglRequest &request)
{
    READ_WRITE_SGL_VALIDATION(mState, mId, mDriver, request);

    mDemandPollingOpType = UBOpContextInfo::SGL_WRITE;
    NResult result = UB_OK;
    auto writeSglFlag = true;
    uint64_t finishTime = GetFinishTime();
    TRACE_DELAY_BEGIN(UB_EP_SYNC_POST_WRITE_SGL);
    do {
        result = PostOneSideSgl(request, false);
        if (result == UB_OK) {
            TRACE_DELAY_END(UB_EP_SYNC_POST_WRITE_SGL, result);
            return UB_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        writeSglFlag = false;
    } while (writeSglFlag);

    NN_LOG_ERROR("Failed to post write sgl request, result " << result);
    TRACE_DELAY_END(UB_EP_SYNC_POST_WRITE_SGL, result);
    return result;
}

NResult NetUBSyncEndpoint::WaitCompletion(int32_t timeout)
{
    NN_LOG_TRACE_INFO("wait completion mDemandPollingOpType " << mDemandPollingOpType);
    UBOpContextInfo *opCtx = nullptr;
    NResult result = NN_OK;
    uint32_t immData = 0;

POLL_CQ:
    if (NN_UNLIKELY(result = PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return result;
    }

    /* If opCtx->opType doesn't match with mDemandingPollingOpType, that means wrong cqe was polled.
     * Store opCtx and immData, and handle them later. Do polling cq again. */
    if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
        // repost if receive opType
        if (opCtx->opType == UBOpContextInfo::RECEIVE) {
            if (mDelayHandleReceiveCtx == nullptr) {
                mDelayHandleReceiveCtx = opCtx;
                mDelayHandleReceiveImmData = immData;
                goto POLL_CQ;
            } else {
                NN_LOG_ERROR("Receive operation type has double received, prev context is not process");
            }
        }
        NN_LOG_WARN("Got un-demand operation type: " << opCtx->opType << ", ignored by ep id: " << mId);
    }

    opCtx->ubJetty->DecreaseRef();
    if (opCtx->opType == UBOpContextInfo::SEND && !(mDriver->mDriverSendMR->ReturnBuffer(opCtx->mrMemAddr))) {
        NN_LOG_ERROR("Failed to return mr segment back in Driver " << mDriver->mName);
    }

    if (opCtx->opType == UBOpContextInfo::SEND_RAW_SGL && mIsNeedEncrypt) {
        // buffer should return when encrypt
        (void)mDriver->mDriverSendMR->ReturnBuffer(opCtx->mrMemAddr);
    }

    if (opCtx->opType == UBOpContextInfo::SGL_WRITE || opCtx->opType == UBOpContextInfo::SGL_READ) {
        auto sgeCtx = reinterpret_cast<UBSgeCtxInfo *>(opCtx->upCtx);
        auto sglCtx = sgeCtx->ctx;
        result = UBOpContextInfo::GetNResult(opCtx->opResultType);
        sglCtx->result = sglCtx->result < result ? sglCtx->result : result;
        auto refCount = __sync_add_and_fetch(&(sglCtx->refCount), 1);
        if (refCount == sglCtx->iovCount) {
            return sglCtx->result;
        }
        goto POLL_CQ;
    }

    return NN_OK;
}

NResult NetUBSyncEndpoint::Receive(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    NResult result = NN_OK;
    UBOpContextInfo *opCtx = nullptr;
    uint32_t immData = 0;

    mDemandPollingOpType = UBOpContextInfo::RECEIVE;
    NN_LOG_TRACE_INFO("receive mDemandPollingOpType " << mDemandPollingOpType);

    /* Handle ctx from incorrect polling */
    if (NN_UNLIKELY(mDelayHandleReceiveCtx != nullptr)) {
        opCtx = mDelayHandleReceiveCtx;
        mDelayHandleReceiveCtx = nullptr;
    } else if (NN_UNLIKELY(result = PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return result;
    }

    do {
        if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
            NN_LOG_ERROR("Got a cqe with un-demand operation type " << opCtx->opType << ", ignored");
            result = NN_ERROR;
            break;
        }

        auto *tmpHeader = reinterpret_cast<UBSHcomNetTransHeader *>(opCtx->mrMemAddr);
        result = NetFunc::ValidateHeaderWithDataSize(*tmpHeader, opCtx->dataSize);
        if (NN_UNLIKELY(result != NN_OK)) {
            break;
        }

        auto tmpDataAddress = reinterpret_cast<void *>(opCtx->mrMemAddr + sizeof(UBSHcomNetTransHeader));
        size_t realDataSize = 0;
        if (mIsNeedEncrypt) {
            uint32_t decryptLen = 0;
            realDataSize = mAes.GetRawLen(tmpHeader->dataLength);
            auto msgReady = mRespMessage.AllocateIfNeed(realDataSize);
            if (NN_UNLIKELY(!msgReady)) {
                NN_LOG_ERROR("Failed to allocate memory for response size: " << realDataSize <<
                    ", probably out of memory");
                result = NN_MALLOC_FAILED;
                break;
            }

            if (!mAes.Decrypt(mSecrets, tmpDataAddress, tmpHeader->dataLength, mRespMessage.mBuf,
                decryptLen)) {
                NN_LOG_ERROR("Failed to decrypt message");
                result = NN_DECRYPT_FAILED;
                break;
            }
            mRespMessage.mDataLen = decryptLen;
        } else {
            realDataSize = tmpHeader->dataLength;
            auto msgReady = mRespMessage.AllocateIfNeed(realDataSize);
            if (NN_UNLIKELY(!msgReady)) {
                NN_LOG_ERROR("Failed to allocate memory for response size: " << realDataSize <<
                    ", probably out of memory");
                result = NN_MALLOC_FAILED;
                break;
            }
            if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf,
                mRespMessage.GetBufLen(), tmpDataAddress, realDataSize) != UB_OK)) {
                NN_LOG_ERROR("Failed to copy tmpDataAddress to mRespMessage");
                result = NN_INVALID_PARAM;
                break;
            }
            mRespMessage.mDataLen = realDataSize;
        }

        if (NN_UNLIKELY(memcpy_s(&(mRespCtx.mHeader),
            sizeof(UBSHcomNetTransHeader), tmpHeader, sizeof(UBSHcomNetTransHeader)) != UB_OK)) {
            NN_LOG_ERROR("Failed to copy tmpHeader to mRespCtx");
            result = NN_INVALID_PARAM;
            break;
        }
    } while (false);

    auto receiveFlag = true;
    uint64_t finishTime = GetFinishTime();
    NResult rePostResult = UB_OK;
    do {
        rePostResult = RePostReceive(opCtx);
        if (rePostResult == UB_OK) {
            break;
        } else if (NeedRetry(rePostResult) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry rePostResult or timeout = 0
        receiveFlag = false;
    } while (receiveFlag);

    if (NN_UNLIKELY(rePostResult != UB_OK)) {
        NN_LOG_ERROR("Failed to repost receive, result " << rePostResult);
        mJetty->ReturnBuffer(opCtx->mrMemAddr);
        return rePostResult;
    }

    if (NN_LIKELY(result == NN_OK)) {
        mRespCtx.mMessage = &mRespMessage;
        ctx.mHeader = mRespCtx.mHeader;
        ctx.mMessage = mRespCtx.mMessage;
    }

    return result;
}

void NetUBSyncEndpoint::ReceiveRawHandle(UBOpContextInfo *opCtx, uint32_t immData, NResult &result)
{
    if (NN_UNLIKELY(opCtx->opType != mDemandPollingOpType)) {
        NN_LOG_ERROR("Got un-demand operation type " << opCtx->opType << " in ReceiveRaw, ignored");
        result = NN_ERROR;
        return;
    }

    if (NN_UNLIKELY(immData != mLastSendSeqNo)) {
        NN_LOG_ERROR("Received un-matched seq no " << immData << ", demand seq no " << mLastSendSeqNo);
        result = NN_SEQ_NO_NOT_MATCHED;
        return;
    }

    auto dataSize = opCtx->dataSize;
    auto msgReady = mRespMessage.AllocateIfNeed(dataSize);
    if (NN_UNLIKELY(!msgReady)) {
        NN_LOG_ERROR("Failed to allocate memory for response size " << opCtx->dataSize <<
            ", probably out of memory");
        result = NN_MALLOC_FAILED;
        return;
    }

    auto tmpDataAddress = reinterpret_cast<void *>(opCtx->mrMemAddr);
    if (mIsNeedEncrypt) {
        uint32_t decryptLen = 0;
        if (!mAes.Decrypt(mSecrets, tmpDataAddress, dataSize, mRespMessage.mBuf, decryptLen)) {
            NN_LOG_ERROR("Failed to decrypt data");
            result = NN_DECRYPT_FAILED;
            return;
        }
        mRespMessage.mDataLen = decryptLen;
    } else {
        if (NN_UNLIKELY(memcpy_s(mRespMessage.mBuf, mRespMessage.GetBufLen(), tmpDataAddress, dataSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy tmpDataAddress to mRespMessage");
            result = NN_INVALID_PARAM;
            return;
        }
        mRespMessage.mDataLen = dataSize;
    }
}

UResult NetUBSyncEndpoint::ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx)
{
    UBOpContextInfo *opCtx = nullptr;
    NResult result = NN_OK;
    uint32_t immData = 0;

    mDemandPollingOpType = UBOpContextInfo::RECEIVE;

    NN_LOG_TRACE_INFO("receive mDemandPollingOpType " << mDemandPollingOpType);

    /* Handle ctx and immData from incorrect polling */
    if (NN_UNLIKELY(mDelayHandleReceiveCtx != nullptr && mDelayHandleReceiveImmData != 0)) {
        opCtx = mDelayHandleReceiveCtx;
        immData = mDelayHandleReceiveImmData;
        mDelayHandleReceiveCtx = nullptr;
        mDelayHandleReceiveImmData = 0;
    } else if (NN_UNLIKELY(result = PollingCompletion(opCtx, timeout, immData)) != NN_OK) {
        // do later
        return result;
    }
    ReceiveRawHandle(opCtx, immData, result);
    auto flag = true;
    uint64_t finishTime = GetFinishTime();
    UResult rePostResult = NN_OK;
    uintptr_t mrMemAddr = opCtx->mrMemAddr;
    do {
        rePostResult = RePostReceive(opCtx);
        if (rePostResult == NN_OK) {
            break;
        } else if (NeedRetry(rePostResult) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        // no retry result or timeout = 0
        flag = false;
    } while (flag);

    if (NN_UNLIKELY(rePostResult != NN_OK)) {
        NN_LOG_ERROR("NetUBSyncEndpoint Failed to repost receive raw, result " << rePostResult);
        mJetty->ReturnBuffer(mrMemAddr);
        return rePostResult;
    }

    if (NN_LIKELY(result == NN_OK)) {
        mRespCtx.mMessage = &mRespMessage;
        ctx.mHeader = {};
        ctx.mHeader.opCode = -1;
        ctx.mHeader.seqNo = immData;
        ctx.mMessage = mRespCtx.mMessage;
    }

    return result;
}

NResult NetUBSyncEndpoint::PollingCompletion(UBOpContextInfo *&ctx, int32_t timeout, uint32_t &immData)
{
    if (NN_UNLIKELY(mJfc == nullptr)) {
        NN_LOG_ERROR("Failed to polling completion with UBSyncEndpoint as cq is null");
        return UB_EP_NOT_INITIALIZED;
    }

    int32_t timeoutInMs = TimeSecToMs(timeout);
    urma_cr_t wc{};
    uint32_t pollCount = 1;
    NResult result = UB_OK;
    if (mPollingMode == UB_BUSY_POLLING) {
        auto start = NetMonotonic::TimeMs();
        int64_t pollTime = 0;
        do {
            pollCount = 1;
            result = mJfc->ProgressV(&wc, pollCount);

            pollTime = (int64_t)(NetMonotonic::TimeMs() - start);
            if (pollCount == 0 && timeoutInMs >= 0 && pollTime > timeoutInMs) {
                return UB_CQ_EVENT_GET_TIMOUT;
            }
        } while (result == UB_OK && pollCount == 0);
    } else if (mPollingMode == UB_EVENT_POLLING) {
        result = mJfc->EventProgressV(&wc, pollCount, timeoutInMs);
    }

    if (NN_UNLIKELY(result != UB_OK)) {
        return result;
    }

    auto *contextInfo = reinterpret_cast<UBOpContextInfo *>(wc.user_ctx);
    contextInfo->dataSize = wc.completion_len;
    contextInfo->opResultType = UBOpContextInfo::OpResult(wc);
    ctx = contextInfo;
    if (NN_UNLIKELY(wc.status != URMA_CR_SUCCESS)) {
        NN_LOG_ERROR("Poll cq failed in UBSyncEndpoint, wcStatus " << wc.status << ", opType " << contextInfo->opType);
        return UB_CQ_WC_WRONG;
    }
    immData = wc.imm_data;

    return UB_OK;
}

NResult NetUBSyncEndpoint::PostReceive(uintptr_t bufAddress, uint32_t bufSize, urma_target_seg_t *localSeg)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Failed to PostReceive with NetUBSyncEndpoint as qp is null");
        return UB_PARAM_INVALID;
    }

    UBOpContextInfo *ctx = nullptr;
    if (NN_UNLIKELY(!mCtxPool.Dequeue(ctx))) {
        NN_LOG_ERROR("Failed to PostReceive with NetUBSyncEndpoint as no ctx left");
        return UB_PARAM_INVALID;
    }

    ctx->ubJetty = mJetty;
    ctx->mrMemAddr = bufAddress;
    ctx->localSeg = localSeg;
    ctx->dataSize = bufSize;
    ctx->qpNum = mJetty->QpNum();
    ctx->opType = UBOpContextInfo::RECEIVE;
    ctx->opResultType = UBOpContextInfo::SUCCESS;
    mJetty->IncreaseRef();

    // attach context to qp firstly, because post cloud be finished very fast
    // if posted failed, need to remove
    mJetty->AddOpCtxInfo(ctx);

    auto result = mJetty->PostReceive(bufAddress, bufSize, localSeg, reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        mJetty->DecreaseRef();
        mJetty->RemoveOpCtxInfo(ctx);
        mCtxPool.Enqueue(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

NResult NetUBSyncEndpoint::RePostReceive(UBOpContextInfo *ctx)
{
    if (NN_UNLIKELY(ctx == nullptr || ctx->ubJetty == nullptr)) {
        NN_LOG_ERROR("Failed to RePostReceive with UBSyncEndpoint as ctx or its qp is null");
        return UB_PARAM_INVALID;
    }

    auto result = ctx->ubJetty->PostReceive(ctx->mrMemAddr, mJetty->PostRegMrSize(), ctx->localSeg,
        reinterpret_cast<uint64_t>(ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        ctx->ubJetty->DecreaseRef();
        mJetty->RemoveOpCtxInfo(ctx);
        mCtxPool.Enqueue(ctx);
    }

    // ctx could not be used if post successfully
    return result;
}

NResult NetUBSyncEndpoint::CreateResources(const std::string &name, UBContext *ctx, UBPollingMode pollMode,
    const JettyOptions &options, UBJetty *&qp, UBJfc *&cq)
{
    if (ctx == nullptr || name.empty()) {
        return UB_PARAM_INVALID;
    }

    auto tmpCQ = new (std::nothrow) UBJfc(name, ctx, pollMode == UB_EVENT_POLLING);
    if (tmpCQ == nullptr) {
        NN_LOG_ERROR("Failed to create UBJfc, probably out of memory");
        return UB_NEW_OBJECT_FAILED;
    }

    auto tmpQP = new (std::nothrow) UBJetty(name, UBJetty::NewId(), ctx, tmpCQ, options);
    if (tmpQP == nullptr) {
        NN_LOG_ERROR("Failed to create UBJetty, probably out of memory");
        delete tmpCQ;
        return UB_NEW_OBJECT_FAILED;
    }

    qp = tmpQP;
    cq = tmpCQ;

    return UB_OK;
}

NResult NetUBSyncEndpoint::InnerPostSend(const UBSendReadWriteRequest &req, urma_target_seg_t *localSeg,
    uint32_t immData)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Failed to PostSend with UBSyncEndpoint as qp is null");
        return UB_PARAM_INVALID;
    }

    static thread_local UBOpContextInfo ctx{};
    ctx.ubJetty = mJetty;
    ctx.mrMemAddr = req.lAddress;
    ctx.dataSize = req.size;
    ctx.qpNum = mJetty->QpNum();
    ctx.lKey = req.lKey;
    ctx.opType = immData == 0 ? UBOpContextInfo::SEND : UBOpContextInfo::SEND_RAW;
    ctx.opResultType = UBOpContextInfo::SUCCESS;
    ctx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        return UB_ERROR;
    }
    mJetty->IncreaseRef();

    auto result = mJetty->PostSend(req.lAddress, req.size, localSeg, &ctx, immData);
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        mJetty->DecreaseRef();
    }

    // ctx could not be used if post successfully
    return result;
}

NResult NetUBSyncEndpoint::InnerPostRead(const UBSendReadWriteRequest &req)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with UBSyncEndpoint as qp is null");
        return UB_PARAM_INVALID;
    }

    urma_target_seg_t *tseg = nullptr;
    if (mDriver->GetTseg(req.lKey, tseg) != NN_OK) {
        NN_LOG_ERROR("Failed to post read request as failed to get tseg");
        return UB_PARAM_INVALID;
    }

    static thread_local UBOpContextInfo ctx{};
    ctx.ubJetty = mJetty;
    ctx.mrMemAddr = req.lAddress;
    ctx.dataSize = req.size;
    ctx.qpNum = mJetty->QpNum();
    ctx.lKey = req.lKey;
    ctx.opType = UBOpContextInfo::READ;
    ctx.opResultType = UBOpContextInfo::SUCCESS;
    ctx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        return UB_ERROR;
    }
    mJetty->IncreaseRef();

    UResult result = UB_OK;
    result = mJetty->PostRead(req.lAddress, tseg, req.rAddress, req.rKey, req.size, reinterpret_cast<uint64_t>(&ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        mJetty->DecreaseRef();
    }

    // ctx could not be used if post successfully
    return result;
}

NResult NetUBSyncEndpoint::InnerPostWrite(const UBSendReadWriteRequest &req)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with UBSyncEndpoint as qp is null");
        return UB_PARAM_INVALID;
    }

    urma_target_seg_t *tseg = nullptr;
    if (mDriver->GetTseg(req.lKey, tseg) != NN_OK) {
        NN_LOG_ERROR("Failed to post read request, as get tseg failed");
        return UB_PARAM_INVALID;
    }

    static thread_local UBOpContextInfo ctx{};
    ctx.ubJetty = mJetty;
    ctx.mrMemAddr = req.lAddress;
    ctx.dataSize = req.size;
    ctx.qpNum = mJetty->QpNum();
    ctx.lKey = req.lKey;
    ctx.opType = UBOpContextInfo::WRITE;
    ctx.opResultType = UBOpContextInfo::SUCCESS;
    ctx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0 && NN_UNLIKELY(memcpy_s(ctx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
        NN_LOG_ERROR("Failed to copy req to ctx");
        return UB_ERROR;
    }
    mJetty->IncreaseRef();

    UResult result = UB_OK;
    result = mJetty->PostWrite(req.lAddress, tseg, req.rAddress, req.rKey, req.size, reinterpret_cast<uint64_t>(&ctx));
    if (NN_UNLIKELY(result != UB_OK)) {
        // remove ctx from qp firstly, then return to pool because, ctx maybe deleted
        mJetty->DecreaseRef();
    }

    // ctx could not be used if post successfully
    return result;
}

UResult NetUBSyncEndpoint::CreateOneSideCtx(const UBSgeCtxInfo &sgeInfo, const UBSHcomNetTransSgeIov *iov,
    uint32_t iovCount, uint64_t (&ctxArr)[NET_SGE_MAX_IOV], bool isRead)
{
    if (iov == nullptr || iovCount == NN_NO0 || iovCount > NN_NO4 || ctxArr == nullptr) {
        NN_LOG_ERROR("Urma failed to create oneSide operation ctx because param invalid");
        return UB_PARAM_INVALID;
    }
    static thread_local UBOpContextInfo ctx[NN_NO4] = {};
    for (uint32_t i = 0; i < iovCount; ++i) {
        ctx[i].ubJetty = mJetty;
        ctx[i].mrMemAddr = iov[i].lAddress;
        ctx[i].dataSize = iov[i].size;
        ctx[i].qpNum = mJetty->QpNum();
        ctx[i].lKey = iov[i].lKey;
        ctx[i].opType = isRead ? UBOpContextInfo::SGL_READ : UBOpContextInfo::SGL_WRITE;
        ctx[i].opResultType = UBOpContextInfo::SUCCESS;
        ctx[i].upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
        auto upCtx = static_cast<UBSgeCtxInfo *>((void *)&(ctx[i].upCtx));
        upCtx->ctx = sgeInfo.ctx;
        upCtx->idx = i;

        mJetty->IncreaseRef();
        ctxArr[i] = reinterpret_cast<uint64_t>(&ctx[i]);
    }
    return UB_OK;
}

UResult NetUBSyncEndpoint::PostOneSideSgl(const UBSendSglRWRequest &req, bool isRead)
{
    if (NN_UNLIKELY(mJetty == nullptr)) {
        NN_LOG_ERROR("Urma failed to Post oneSide with UBWorker as qp is null.");
        return UB_PARAM_INVALID;
    }

    static thread_local UBSglContextInfo sglCtx;
    sglCtx.qp = mJetty;
    sglCtx.result = UB_OK;
    if (NN_UNLIKELY(memcpy_s(sglCtx.iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, req.iov,
        sizeof(UBSHcomNetTransSgeIov) * req.iovCount) != UB_OK)) {
        NN_LOG_ERROR("Urma post oneSide failed to copy UBSHcomNetTransSgeIov to sglCtx");
        return UB_PARAM_INVALID;
    }
    sglCtx.iovCount = req.iovCount;
    sglCtx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != UB_OK)) {
            NN_LOG_ERROR("Urma failed to copy upCtx to sglCtx");
            return UB_PARAM_INVALID;
        }
    }

    UBSgeCtxInfo sgeInfo(&sglCtx);
    sglCtx.refCount = 0;
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    UResult result = CreateOneSideCtx(sgeInfo, req.iov, req.iovCount, ctxArr, isRead);
    if (result != UB_OK) {
        NN_LOG_ERROR("Urma failed to create one side ctx.");
        return result;
    }
    UBSHcomNetTransSglRequest sglReq = req;
    if (GetSglTseg(mDriver, sglReq) != NN_OK) {
        NN_LOG_ERROR("GetSglTseg failed");
        return UB_PARAM_INVALID;
    }
    result = mJetty->PostOneSideSgl(sglReq.iov, sglReq.iovCount, ctxArr, isRead, NET_SGE_MAX_IOV);
    if (NN_UNLIKELY(result != UB_OK)) {
        for (int i = 0; i < req.iovCount; ++i) {
            mJetty->DecreaseRef();
        }
    }
    return result;
}
}
}
#endif
