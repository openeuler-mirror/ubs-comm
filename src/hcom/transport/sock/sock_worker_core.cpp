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
#include "sock_worker.h"

namespace ock {
namespace hcom {
constexpr uint32_t MAX_EPOLL_SIZE = NN_NO8192;

SResult SockWorker::Validate()
{
    /* do later */
    return SS_OK;
}

SResult SockWorker::Initialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mInited) {
        return SS_OK;
    }

    SResult result = SS_OK;
    if (NN_UNLIKELY((result = Validate()) != SS_OK)) {
        NN_LOG_ERROR("Failed to validate in sock worker initialize");
        return result;
    }

    NN_LOG_INFO("Try to initialize sock worker '" << mName << "' with " << mOptions.ToString());

    /* create epoll */
    if (NN_UNLIKELY((mEpollHandle = epoll_create(MAX_EPOLL_SIZE)) < 0)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create epoll in sock worker " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return SS_WORKER_EPOLL_FAILED;
    }

    if (NN_UNLIKELY((result = InitContextInfoPool()) != SS_OK)) {
        return result;
    }

    mInited = true;
    return SS_OK;
}

SResult SockWorker::InitContextInfoPool()
{
    SResult result = SS_OK;
    if (mType == SOCK_UDS) {
        if ((result = mOpCtxInfoPool.Initialize(mOpCtxMemPool, UBSHcomNetDriverProtocol::UDS)) != SS_OK) {
            NN_LOG_ERROR("Failed to initialize operation context info pool in SockWorker " << DetailName());
            return result;
        }

        if ((result = mSglCtxInfoPool.Initialize(mSglCtxMemPool, UBSHcomNetDriverProtocol::UDS)) != SS_OK) {
            NN_LOG_ERROR("Failed to initialize sgl context info pool in SockWorker " << DetailName());
            return result;
        }

        if (mOptions.tcpSendZCopy) {
            if ((result = mHeaderReqInfoPool.Initialize(mHeaderReqMemPool, UBSHcomNetDriverProtocol::UDS)) != SS_OK) {
                NN_LOG_ERROR("Failed to initialize header request info pool in SockWorker " << DetailName());
                return result;
            }
        }
    } else {
        if ((result = mOpCtxInfoPool.Initialize(mOpCtxMemPool)) != SS_OK) {
            NN_LOG_ERROR("Failed to initialize operation context info pool in SockWorker " << DetailName());
            return result;
        }

        if ((result = mSglCtxInfoPool.Initialize(mSglCtxMemPool)) != SS_OK) {
            NN_LOG_ERROR("Failed to initialize sgl context info pool in SockWorker " << DetailName());
            return result;
        }

        if (mOptions.tcpSendZCopy) {
            if ((result = mHeaderReqInfoPool.Initialize(mHeaderReqMemPool)) != SS_OK) {
                NN_LOG_ERROR("Failed to initialize header request info pool in SockWorker " << DetailName());
                return result;
            }
        }
    }
    return result;
}

void SockWorker::UnInitialize()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        return;
    }

    UnInitializeInner();
    mOpCtxInfoPool.UnInitialize();
    mSglCtxInfoPool.UnInitialize();
    mHeaderReqInfoPool.UnInitialize();
}

void SockWorker::UnInitializeInner()
{
    if (mEpollHandle != -1) {
        NetFunc::NN_SafeCloseFd(mEpollHandle);
    }

    // do later
}

SResult SockWorker::HandleReceiveCtx(SockOpContextInfo &opCtx)
{
    switch ((opCtx.header->flags & 0xff)) {
        case NTH_TWO_SIDE:
        case NTH_TWO_SIDE_SGL:
            NN_LOG_TRACE_INFO("Receive new request " << opCtx.sock->Id() << " head imm data " <<
                opCtx.header->immData << ", flags " << opCtx.header->flags << ", seqNo " << opCtx.header->seqNo <<
                ", data len " << opCtx.header->dataLength);
            return mNewRequestHandler(opCtx);
        case NTH_READ:
            return PostReadAck(opCtx);
        case NTH_READ_ACK:
            return PostReadAckHandle(opCtx);
        case NTH_READ_SGL:
            return PostReadSglAck(opCtx);
        case NTH_READ_SGL_ACK:
            return PostReadSglAckHandle(opCtx);
        case NTH_WRITE:
            return PostWriteAck(opCtx);
        case NTH_WRITE_ACK:
            return PostWriteAckHandle(opCtx);
        case NTH_WRITE_SGL:
            return PostWriteSglAck(opCtx);
        case NTH_WRITE_SGL_ACK:
            return PostWriteSglAckHandle(opCtx);
        default:
            NN_LOG_ERROR("Receive head invalid flags " << opCtx.header->flags);
            return SS_PARAM_INVALID;
    }
}

#define HANDLE_SOCK_EVENT(sockOpResult, doUpperCall)                                                         \
    Sock *sock = static_cast<Sock *>(oneEv.data.ptr);                                                        \
    if (NN_UNLIKELY(sock == nullptr)) {                                                                      \
        NN_LOG_ERROR("Sock is null in polled event for sock worker " << mName);                              \
        continue;                                                                                            \
    }                                                                                                        \
    if (NN_UNLIKELY(fcntl(sock->FD(), F_GETFD) == -1 && errno == EBADF)) {                                   \
        NN_LOG_ERROR("Receive bad fd " << sock->FD() << " in sock worker " << mName);                        \
        continue;                                                                                            \
    }                                                                                                        \
                                                                                                             \
    static thread_local SockOpContextInfo opCtx {};                                                          \
    if (oneEv.events & EPOLLIN) {                                                                            \
        if (NN_LIKELY(((sockOpResult) = sock->HandleIn((doUpperCall)))) == SockOpContextInfo::SS_NO_ERROR) { \
            /* if fully receive a request */                                                                 \
                                                                                                             \
            if (doUpperCall) {                                                                               \
                /* set context */                                                                            \
                opCtx.header = sock->GetHeaderAddress();                                                     \
                opCtx.sock = sock;                                                                           \
                opCtx.dataAddress = sock->mReceiveBuff.DataIntPtr();                                         \
                opCtx.dataSize = sock->mReceiveBuff.ActualDataSize();                                        \
                opCtx.opType = SockOpContextInfo::SS_RECEIVE;                                                \
                opCtx.errType = SockOpContextInfo::SS_NO_ERROR;                                              \
                                                                                                             \
                /* handle by type */                                                                         \
                if (NN_UNLIKELY(HandleReceiveCtx(opCtx) == NN_EP_CLOSE)) {                                   \
                    /* fd is already removed from epoll, cannot be modified again */                         \
                    continue;                                                                                \
                }                                                                                            \
            }                                                                                                \
            if (NN_UNLIKELY(ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET) != SS_OK)) {                   \
                NN_LOG_WARN("Unable to modify sock " << sock->Id() << " in epoll in");                       \
            }                                                                                                \
            /* not fully received, continue to process next event */                                         \
            continue;                                                                                        \
        }                                                                                                    \
        NN_LOG_TRACE_INFO("Got error " << (sockOpResult) << " on sock " << sock->Id() << " with peer " <<    \
            sock->PeerIpPort() << " in sock worker " << mName);                                              \
                                                                                                             \
        /* do sock conn broken process */                                                                    \
        bzero(&opCtx, sizeof(SockOpContextInfo));                                                            \
        opCtx.sock = sock;                                                                                   \
        opCtx.opType = SockOpContextInfo::SS_RECEIVE;                                                        \
        opCtx.errType = sockOpResult;                                                                        \
                                                                                                             \
        /* do upper call */                                                                                  \
        mNewRequestHandler(opCtx);                                                                           \
        /* continue to process next event */                                                                 \
        continue;                                                                                            \
    } else if (oneEv.events & EPOLLOUT) {                                                                    \
        auto result = sock->ProcessQueueReq();                                                               \
        if (result == SS_SOCK_SEND_EAGAIN) {                                                                 \
            if (NN_UNLIKELY(ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET) != SS_OK)) {                   \
                NN_LOG_WARN("Unable to modify sock " << sock->Id() << " in epoll out");                      \
            }                                                                                                \
        }                                                                                                    \
        if (result == SS_RESET_BY_PEER || result == SS_SOCK_SEND_FAILED) {                                   \
            if (NN_UNLIKELY(ModifyInEpoll(sock, EPOLLWRNORM) != SS_OK)) {                                    \
                NN_LOG_WARN("Unable to modify sock " << sock->Id() << " when EPOLLWRNORM in epoll out");     \
            }                                                                                                \
        }                                                                                                    \
        continue;                                                                                            \
    } else if (oneEv.events & EPOLLWRNORM) {                                                                 \
        mEpCloseHandler(sock);                                                                               \
        continue;                                                                                            \
    }                                                                                                        \
                                                                                                             \
    NN_LOG_TRACE_INFO("Receive sock " << sock->Id() << " event " << oneEv.events);                           \
    /* continue to process next event */                                                                     \
    continue


#define HANDLE_EVENTS(count, sockOpResult, doUpperCall, ev)       \
    for (uint16_t i = 0; i < static_cast<uint16_t>(count); ++i) { \
        struct epoll_event &oneEv = (ev)[i];                      \
        HANDLE_SOCK_EVENT(sockOpResult, doUpperCall);             \
    }

void SockWorker::RunInThread(int16_t cpuId)
{
    BindCpuSetPthreadName(cpuId);

    if (mOptions.threadPriority != 0) {
        if (NN_UNLIKELY(setpriority(PRIO_PROCESS, 0, mOptions.threadPriority) != 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_WARN("Unable to set worker thread priority in sock worker " << mName <<
                ", as " << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    mProgressThrStarted.store(true);

    const uint16_t pollBatchSize = mOptions.pollingBatchSize;
    const uint32_t timeout = mOptions.pollingTimeoutMs;

    struct epoll_event ev[pollBatchSize];

    /* for new accept sock */
    bool doUpperCall = false;
    SockOpContextInfo::SockErrorType sockOpResult = SockOpContextInfo::SS_NO_ERROR;

    /* start epoll */
    while (!mNeedToStop) {
        /* do epoll wait */
        int count = epoll_wait(mEpollHandle, ev, pollBatchSize, timeout);
        if (count > 0) {
            /* there are events, handle it */
            NN_LOG_TRACE_INFO("Got " << count << " in sock worker " << mName);
            TRACE_DELAY_BEGIN(SOCK_WORKER_EPOLL_WAIT);
            HANDLE_EVENTS(count, sockOpResult, doUpperCall, ev)
            TRACE_DELAY_END(SOCK_WORKER_EPOLL_WAIT, 0);
        } else if (count == 0) {
            NN_LOG_TRACE_INFO("Got " << count << " in sock worker " << mName);
            /* if io request, call idle */
            if (mIdleHandler != nullptr) {
                mIdleHandler(mIndex);
            }
        } else if (errno == EINTR) {
            NN_LOG_TRACE_INFO("Got error no EINTR in sock worker " << mName);
            continue;
        } else {
            /* error happens */
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to do epoll_wait in sock worker " << mName << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            break;
        }
    }

    NN_LOG_INFO("Sock worker " << mName << ":" << mIndex.ToString() << " progress thread exiting");
}

SResult SockWorker::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        NN_LOG_ERROR("Failed to start sock worker " << mName << " as it is not initialized");
        return SS_ERROR;
    }

    if (mStarted) {
        NN_LOG_WARN("Unable to start sock worker " << mName << " as it is already started");
        return SS_OK;
    }

    /* validate handler */
    if (mNewRequestHandler == nullptr) {
        NN_LOG_ERROR("Failed to start sock worker " << mName << " as new request handler is null");
        return SS_PARAM_INVALID;
    }

    if (mSendPostedHandler == nullptr) {
        NN_LOG_ERROR("Failed to start sock worker " << mName << " as request posted handler is null");
        return SS_PARAM_INVALID;
    }

    if (mOneSideDoneHandler == nullptr) {
        NN_LOG_ERROR("Failed to start sock worker " << mName << " as one side done handler is null");
        return SS_PARAM_INVALID;
    }
    mNeedToStop = false;
    std::thread tmpThread(&SockWorker::RunInThread, this, mOptions.cpuId);
    mProgressThr = std::move(tmpThread);

    while (!mProgressThrStarted.load()) {
        usleep(NN_NO10);
    }

    mProgressThrStarted = false;

    mStarted = true;
    return SS_OK;
}

void SockWorker::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    mNeedToStop = true;
    if (mProgressThr.joinable()) {
        mProgressThr.join();
    }

    mStarted = false;
}

SResult SockWorker::PostReadAck(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    while (NN_UNLIKELY(!opCtx.sock->GetQueueSpace())) {
        (void)opCtx.sock->ProcessQueueReq();
    }
    if (NN_UNLIKELY(opCtx.dataSize < sizeof(UBSHcomNetTransSgeIov))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iov size");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    auto rawIov = reinterpret_cast<UBSHcomNetTransSgeIov *>(opCtx.dataAddress);
    if (NN_UNLIKELY(NN_OK != opCtx.sock->mMrChecker->Validate(rawIov->rKey, rawIov->rAddress, rawIov->size))) {
        NN_LOG_ERROR("Invalid memory region or local key");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return NN_INVALID_LKEY;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        NN_LOG_ERROR("Failed to PostReadAck with SockWorker " << DetailName() << " as no ctx left");
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadAck with SockWorker " << DetailName() << " as no sglCtx left");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = opCtx.sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_READ_ACK;

    SockTransHeader header = {};
    header.flags = NTH_READ_ACK;
    header.seqNo = opCtx.header->seqNo;
    header.dataLength = rawIov->size;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    UBSHcomNetTransSgeIov iov(rawIov->rAddress, 0, rawIov->size);
    UBSHcomNetTransSglRequest req(&iov, NN_NO1, 0);

    sglCtx->Clone(header, req.iov, req.iovCount);
    opCtxInfo->sendCtx = sglCtx;

    auto result = opCtx.sock->PostSendSgl(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(opCtx.sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_OK) {
        auto res = ModifyInEpoll(opCtx.sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    ReturnResources(opCtx.sock, opCtxInfo);
    return result;
}

SResult SockWorker::PostReadAckHandle(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    auto originalCtx = opCtx.sock->RemoveOpCtx(opCtx.header->seqNo);
    if (originalCtx == nullptr) {
        NN_LOG_ERROR("Failed to PostReadAckHandle with sock worker " << DetailName() << " as invalid seqNo " <<
            opCtx.header->seqNo);
        return SS_PARAM_INVALID;
    }
    if (originalCtx->sock != opCtx.sock) {
        NN_LOG_ERROR("Failed to check with sock worker " << DetailName() << " as sock different.");
        return SS_PARAM_INVALID;
    }
    // only the first iov is used, the mr info is recorded in this iov
    if (originalCtx->sendCtx->iov[0].size != opCtx.dataSize) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << DetailName() << " as size different.");
        return SS_PARAM_INVALID;
    }
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(originalCtx->sendCtx->iov[0].lAddress),
        originalCtx->sendCtx->iov[0].size, reinterpret_cast<void *>(opCtx.dataAddress),
        originalCtx->sendCtx->iov[0].size) != SS_OK)) {
        NN_LOG_ERROR("Failed to copy opCtx to iov");
        return SS_PARAM_INVALID;
    }
    NN_LOG_TRACE_INFO("PostReadAckHandle " << opCtx.sock->Id() << " head imm data " << opCtx.header->immData <<
        ", flags " << opCtx.header->flags << ", seqNo " << opCtx.header->seqNo << ", data len " <<
        opCtx.header->dataLength);
    return mOneSideDoneHandler(originalCtx);
}

SResult SockWorker::GenerateReadSglAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx,
    UBSHcomNetTransSgeIov *&rawIov, uint16_t iovCount, uint32_t dataSize)
{
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadSglAck with sock worker " << DetailName() << " as no sglCtx left");
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = opCtx.sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_SGL_READ_ACK;

    SockTransHeader header = {};
    header.flags = NTH_READ_SGL_ACK;
    header.seqNo = opCtx.header->seqNo;
    header.dataLength = opCtx.header->dataLength + dataSize;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    sglCtx->Clone(header, rawIov, iovCount);
    opCtxInfo->sendCtx = sglCtx;
    return SS_OK;
}

inline SResult SockWorker::CheckIovLen(SockOpContextInfo &opCtx, uint16_t &iovCount)
{
    if (NN_UNLIKELY(opCtx.dataSize < sizeof(UBSHcomNetTransSglRequest::iovCount))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iovCount size");
        return false;
    }
    auto count = reinterpret_cast<uint16_t *>(opCtx.dataAddress);

    if (*count == 0 || *count > NN_NO4) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << mName << " as iov count is illegal.");
        return false;
    }
    if (NN_UNLIKELY(opCtx.dataSize < (sizeof(UBSHcomNetTransSglRequest::iovCount) +
        sizeof(UBSHcomNetTransSgeIov) * (*count)))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iov size");
        return false;
    }
    iovCount = *count;
    return true;
}

SResult SockWorker::PostReadSglAck(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    while (NN_UNLIKELY(!opCtx.sock->GetQueueSpace())) {
        (void)opCtx.sock->ProcessQueueReq();
    }
    uint16_t iovCount = 0;
    if (NN_UNLIKELY(!CheckIovLen(opCtx, iovCount))) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    auto rawIov = reinterpret_cast<UBSHcomNetTransSgeIov *>(opCtx.dataAddress +
        sizeof(UBSHcomNetTransSglRequest::iovCount));
    uint32_t dataSize = 0;
    for (uint16_t i = 0; i < iovCount; i++) {
        if (NN_UNLIKELY(NN_OK !=
            opCtx.sock->mMrChecker->Validate(rawIov[i].rKey, rawIov[i].rAddress, rawIov[i].size))) {
            NN_LOG_ERROR("Invalid memory region or local key");
            opCtx.sock->ReturnQueueSpace(NN_NO1);
            return NN_INVALID_LKEY;
        }
        dataSize += rawIov[i].size;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadSglAck with sock worker " << DetailName() << " as no ctx left");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }

    if (NN_UNLIKELY(GenerateReadSglAckOpCtxInfo(opCtxInfo, opCtx, rawIov, iovCount, dataSize) != SS_OK)) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    auto result = opCtx.sock->PostReadSglAck(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(opCtx.sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_OK) {
        auto res = ModifyInEpoll(opCtx.sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    ReturnResources(opCtx.sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostReadSglAckHandle(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    auto originalCtx = opCtx.sock->RemoveOpCtx(opCtx.header->seqNo);
    if (originalCtx == nullptr) {
        NN_LOG_ERROR("Failed to handle ack with sock worker " << mName << " as invalid seqNo " << opCtx.header->seqNo);
        return SS_PARAM_INVALID;
    }

    if (originalCtx->sock != opCtx.sock) {
        NN_LOG_ERROR("Failed to check read sgl sock ptr with sock worker " << mName << " as sock different.");
        return SS_PARAM_INVALID;
    }

    if (NN_UNLIKELY(opCtx.dataSize < sizeof(UBSHcomNetTransSglRequest::iovCount))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iovCount size");
        return SS_PARAM_INVALID;
    }
    /* write data */
    auto iovCount = reinterpret_cast<uint16_t *>(opCtx.dataAddress);
    if (*iovCount == 0 || *iovCount > NN_NO4 || *iovCount != originalCtx->sendCtx->iovCount) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << mName << " as iov count is illegal.");
        return SS_PARAM_INVALID;
    }
    if (NN_UNLIKELY(opCtx.dataSize < (sizeof(UBSHcomNetTransSglRequest::iovCount) +
        sizeof(UBSHcomNetTransSgeIov) * (*iovCount)))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iov size");
        return SS_PARAM_INVALID;
    }
    auto sgeIov = reinterpret_cast<UBSHcomNetTransSgeIov *>(opCtx.dataAddress +
        sizeof(UBSHcomNetTransSglRequest::iovCount));
    auto data = reinterpret_cast<char *>(opCtx.dataAddress + sizeof(UBSHcomNetTransSglRequest::iovCount) +
        sizeof(UBSHcomNetTransSgeIov) * (*iovCount));

    uint32_t dataSize = 0;
    for (uint16_t i = 0; i < *iovCount; i++) {
        dataSize += sgeIov[i].size;
    }

    if (originalCtx->sendCtx->sendHeader.dataLength + dataSize != opCtx.header->dataLength) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << mName << " as size different.");
        return SS_PARAM_INVALID;
    }

    uint32_t copyOffset = 0;
    for (uint16_t i = 0; i < *iovCount; i++) {
        UBSHcomNetTransSgeIov iov = originalCtx->sendCtx->iov[i];
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(iov.lAddress), iov.size, data + copyOffset, iov.size) !=
            NN_OK)) {
            NN_LOG_ERROR("Failed to copy data to iov");
            return NN_INVALID_PARAM;
        }
        copyOffset += iov.size;
    }

    NN_LOG_TRACE_INFO("PostReadSglAckHandle " << opCtx.sock->Id() << " head imm data " << opCtx.header->immData <<
        ", flags " << opCtx.header->flags << ", seqNo " << opCtx.header->seqNo << ", data len " <<
        opCtx.header->dataLength);
    return mOneSideDoneHandler(originalCtx);
}

SResult SockWorker::GenerateWriteAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx)
{
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWriteAck with sock worker " << DetailName() << " as no sglCtx left");
        return SS_CTX_FULL;
    }

    SockTransHeader header = {};
    header.flags = NTH_WRITE_ACK;
    header.seqNo = opCtx.header->seqNo;
    header.dataLength = opCtx.header->dataLength;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    sglCtx->sendHeader = header;
    opCtxInfo->sendCtx = sglCtx;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_WRITE_ACK;
    return SS_OK;
}

SResult SockWorker::PostWriteAck(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    /* send ack */
    while (NN_UNLIKELY(!opCtx.sock->GetQueueSpace())) {
        (void)opCtx.sock->ProcessQueueReq();
    }
    if (NN_UNLIKELY(opCtx.dataSize < sizeof(UBSHcomNetTransSgeIov))) {
        NN_LOG_ERROR("Failed to PostWriteAck as data size " << opCtx.dataSize << " is less than iov size");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    auto rawIov = reinterpret_cast<UBSHcomNetTransSgeIov *>(opCtx.dataAddress);
    if (NN_UNLIKELY(NN_OK != opCtx.sock->mMrChecker->Validate(rawIov->rKey, rawIov->rAddress, rawIov->size))) {
        NN_LOG_ERROR("Invalid memory region or local key");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return NN_INVALID_LKEY;
    }
    /* write data */
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(rawIov->rAddress), rawIov->size,
        reinterpret_cast<void *>(opCtx.dataAddress + sizeof(UBSHcomNetTransSgeIov)), rawIov->size) != NN_OK)) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return SS_PARAM_INVALID;
    }
    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostWriteAck with sock worker " << DetailName() << " as no ctx left");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    if (NN_UNLIKELY(GenerateWriteAckOpCtxInfo(opCtxInfo, opCtx))) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }
    auto result = opCtx.sock->PostSendHead(opCtxInfo);
    if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(opCtx.sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_OK) {
        auto res = ModifyInEpoll(opCtx.sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    ReturnResources(opCtx.sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostWriteAckHandle(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    auto originalCtx = opCtx.sock->RemoveOpCtx(opCtx.header->seqNo);
    if (originalCtx == nullptr) {
        NN_LOG_ERROR("Failed to handle ack with sock worker " << mName << " as invalid seqNo " << opCtx.header->seqNo);
        return SS_PARAM_INVALID;
    }
    if (originalCtx->sock != opCtx.sock) {
        NN_LOG_ERROR("Failed to check write sock ptr with sock worker " << mName << " as sock different.");
        return SS_PARAM_INVALID;
    }
    if (originalCtx->sendCtx->sendHeader.dataLength != opCtx.header->dataLength) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << mName << " as size different.");
        return SS_PARAM_INVALID;
    }

    NN_LOG_TRACE_INFO("PostWriteAckHandle " << opCtx.sock->Id() << " head imm data " << opCtx.header->immData <<
        ", flags " << opCtx.header->flags << ", seqNo " << opCtx.header->seqNo << ", data len " <<
        opCtx.header->dataLength);
    return mOneSideDoneHandler(originalCtx);
}

SResult SockWorker::GenerateWriteSglAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx)
{
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWriteSglAck with sock worker " << DetailName() << " as no sglCtx left");
        return SS_CTX_FULL;
    }

    /* send ack */
    SockTransHeader header = {};
    header.flags = NTH_WRITE_SGL_ACK;
    header.seqNo = opCtx.header->seqNo;
    header.dataLength = opCtx.header->dataLength;

    /* finally fill header crc */
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);

    sglCtx->sendHeader = header;
    opCtxInfo->sendCtx = sglCtx;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_SGL_WRITE_ACK;
    return SS_OK;
}

SResult SockWorker::PostWriteSglAck(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)
    while (NN_UNLIKELY(!opCtx.sock->GetQueueSpace())) {
        (void)opCtx.sock->ProcessQueueReq();
    }

    uint16_t iovCount = 0;
    if (NN_UNLIKELY(!CheckIovLen(opCtx, iovCount))) {
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    auto iov = reinterpret_cast<UBSHcomNetTransSgeIov *>(opCtx.dataAddress +
        sizeof(UBSHcomNetTransSglRequest::iovCount));
    uint32_t dataSize = 0;
    for (uint16_t i = 0; i < iovCount; i++) {
        dataSize += iov[i].size;
    }
    if (NN_UNLIKELY(opCtx.dataSize <
        (sizeof(UBSHcomNetTransSglRequest::iovCount) + sizeof(UBSHcomNetTransSgeIov) * iovCount + dataSize))) {
        NN_LOG_ERROR("Failed to PostReadAck as data size " << opCtx.dataSize << " is less than iov data size");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }

    auto data = reinterpret_cast<char *>(opCtx.dataAddress + sizeof(UBSHcomNetTransSglRequest::iovCount) +
        sizeof(UBSHcomNetTransSgeIov) * iovCount);

    uint32_t copyOffset = 0;
    for (uint16_t i = 0; i < iovCount; i++) {
        if (NN_UNLIKELY(NN_OK != opCtx.sock->mMrChecker->Validate(iov[i].rKey, iov[i].rAddress, iov[i].size))) {
            NN_LOG_ERROR("Invalid memory region or local key");
            opCtx.sock->ReturnQueueSpace(NN_NO1);
            return NN_INVALID_LKEY;
        }
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(iov[i].rAddress), iov[i].size, data + copyOffset,
            iov[i].size) != NN_OK)) {
            opCtx.sock->ReturnQueueSpace(NN_NO1);
            NN_LOG_ERROR("Failed to copy data to iov");
            return NN_INVALID_PARAM;
        }
        copyOffset += iov[i].size;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostWriteSglAck with sock worker " << DetailName() << " as no ctx left");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }

    if (GenerateWriteSglAckOpCtxInfo(opCtxInfo, opCtx) != SS_OK) {
        NN_LOG_ERROR("Failed to PostWriteSglAck with sock worker " << DetailName() << " as no Sglctx left");
        opCtx.sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_PARAM_INVALID;
    }

    auto result = opCtx.sock->PostSendHead(opCtxInfo);
    if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(opCtx.sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_OK) {
        auto res = ModifyInEpoll(opCtx.sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    ReturnResources(opCtx.sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostWriteSglAckHandle(SockOpContextInfo &opCtx)
{
    NN_ASSERT_LOG_RETURN(opCtx.sock->UpContext() != 0, SS_ERROR)

    auto originalCtx = opCtx.sock->RemoveOpCtx(opCtx.header->seqNo);
    if (originalCtx == nullptr) {
        NN_LOG_ERROR("Failed to handle ack with sock worker " << mName << " as invalid seqNo " << opCtx.header->seqNo);
        return SS_PARAM_INVALID;
    }
    if (originalCtx->sock != opCtx.sock) {
        NN_LOG_ERROR("Failed to check write sgl sock ptr with sock worker " << mName << " as sock different.");
        return SS_PARAM_INVALID;
    }
    if (originalCtx->sendCtx->sendHeader.dataLength != opCtx.header->dataLength) {
        NN_LOG_ERROR("Failed to check sock with sock worker " << mName << " as data length different.");
        return SS_PARAM_INVALID;
    }

    NN_LOG_TRACE_INFO("PostWriteSglAckHandle " << opCtx.sock->Id() << " head imm data " << opCtx.header->immData <<
        ", flags " << opCtx.header->flags << ", seqNo " << opCtx.header->seqNo << ", data len " <<
        opCtx.header->dataLength);
    return mOneSideDoneHandler(originalCtx);
}

void SockWorker::ReturnResources(Sock *sock, SockOpContextInfo *ctx, SockSglContextInfo *sglCtx)
{
    sock->ReturnQueueSpace(NN_NO1);
    if (sglCtx != nullptr) {
        mSglCtxInfoPool.Return(sglCtx);
        sglCtx = nullptr;
    }
    if (ctx != nullptr) {
        mOpCtxInfoPool.Return(ctx);
        ctx = nullptr;
    }
}

void SockWorker::EpCloseByUser(Sock *sock)
{
    if (mEpCloseHandler == nullptr) {
        NN_LOG_WARN("Worker ep close handler is null, worker name is" << mName);
    }
    mEpCloseHandler(sock);
}
}
}