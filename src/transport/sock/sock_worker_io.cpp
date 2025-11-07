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


/* async endpoint operation */
SResult SockWorker::PostSend(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostSend with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    opCtxInfo->sock = sock;
    opCtxInfo->opType =
        header.immData == 0 ? SockOpContextInfo::SockOpType::SS_SEND : SockOpContextInfo::SockOpType::SS_SEND_RAW;
    opCtxInfo->upCtxSize = req.upCtxSize;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != NN_OK)) {
            ReturnResources(sock, opCtxInfo, nullptr);
            NN_LOG_ERROR("Failed to copy req to opCtxInfo");
            return SS_PARAM_INVALID;
        }
    }
    if (mOptions.tcpSendZCopy) {
        auto headerReqInfo = mHeaderReqInfoPool.Get();
        if (NN_UNLIKELY(headerReqInfo == nullptr)) {
            NN_LOG_ERROR("Failed to PostSend with sock worker " << DetailName() << " as no ctx left");
            ReturnResources(sock, opCtxInfo, nullptr);
            return SS_CTX_FULL;
        }
        headerReqInfo->sendHeader = header;
        headerReqInfo->request = reinterpret_cast<void *>(req.lAddress);
        opCtxInfo->headerRequest = headerReqInfo;
    } else {
        opCtxInfo->sendBuff = &header;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;

    auto result = sock->PostSend(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        mSendPostedHandler(opCtxInfo);
        mOpCtxInfoPool.Return(opCtxInfo);
        NN_LOG_TRACE_INFO("PostSend cb sock " << sock->Id() << " head imm data " << header.immData << ", flags " <<
            header.flags << ", seqNo " << header.seqNo << ", data len " << header.dataLength);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    sock->ReturnQueueSpace(NN_NO1);
    if (mOptions.tcpSendZCopy) {
        mHeaderReqInfoPool.Return(opCtxInfo->headerRequest);
        opCtxInfo->headerRequest = nullptr;
    }
    mOpCtxInfoPool.Return(opCtxInfo);
    opCtxInfo = nullptr;

    return result;
}

SResult SockWorker::PostSendRawSgl(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendRawSgl with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendRawSgl with sock worker " << DetailName() << " as no sglCtx left");
        sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL;
    opCtxInfo->upCtxSize = req.upCtxSize;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy req to opCtxInfo");
            ReturnResources(sock, opCtxInfo, sglCtx);
            return SS_PARAM_INVALID;
        }
    }

    sglCtx->Clone(header, req.iov, req.iovCount);
    opCtxInfo->sendCtx = sglCtx;

    auto result = sock->PostSendSgl(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        mSendPostedHandler(opCtxInfo);
        mOpCtxInfoPool.Return(opCtxInfo);
        NN_LOG_TRACE_INFO("PostSendRawSgl cb sock " << sock->Id() << " head imm data " << header.immData <<
            ", flags " << header.flags << ", seqNo " << header.seqNo << ", data len " << header.dataLength);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    ReturnResources(sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostRead(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_PARAM_INVALID;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with sock worker " << DetailName() << " as no sgl ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_READ;
    opCtxInfo->upCtxSize = req.upCtxSize;
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != SS_OK)) {
            ReturnResources(sock, opCtxInfo, sglCtx);
            NN_LOG_ERROR("Failed to copy request to opCtxInfo");
            return SS_PARAM_INVALID;
        }
    }

    UBSHcomNetTransSgeIov iov(req.lAddress, req.rAddress, req.lKey, req.rKey, req.size);
    sglCtx->Clone(header, &iov, NN_NO1);
    opCtxInfo->sendCtx = sglCtx;

    sock->AddOpCtx(header.seqNo, opCtxInfo);
    sock->IncreaseRef();

    auto result = sock->PostRead(opCtxInfo);
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    (void)sock->RemoveOpCtx(header.seqNo);
    sock->DecreaseRef();
    ReturnResources(sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostRead(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostRead with sock worker " << DetailName() << " as no sglCtx left");
        sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_SGL_READ;
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    opCtxInfo->upCtxSize = req.upCtxSize;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != SS_OK)) {
            ReturnResources(sock, opCtxInfo, sglCtx);
            NN_LOG_ERROR("Failed to copy request to opCtxInfo");
            return SS_PARAM_INVALID;
        }
    }

    sglCtx->Clone(header, req.iov, req.iovCount);
    opCtxInfo->sendCtx = sglCtx;

    sock->AddOpCtx(header.seqNo, opCtxInfo);
    sock->IncreaseRef();

    auto result = sock->PostReadSgl(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    (void)sock->RemoveOpCtx(header.seqNo);
    sock->DecreaseRef();
    ReturnResources(sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostWrite(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostWrite with sock worker " << DetailName() << " as no sglCtx left");
        sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_WRITE;
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    opCtxInfo->upCtxSize = req.upCtxSize;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != SS_OK)) {
            ReturnResources(sock, opCtxInfo, sglCtx);
            NN_LOG_ERROR("Failed to copy req to opCtxInfo");
            return SS_PARAM_INVALID;
        }
    }

    UBSHcomNetTransSgeIov iov(req.lAddress, req.rAddress, req.lKey, req.rKey, req.size);
    sglCtx->Clone(header, &iov, NN_NO1);
    opCtxInfo->sendCtx = sglCtx;

    sock->AddOpCtx(header.seqNo, opCtxInfo);
    sock->IncreaseRef();

    auto result = sock->PostWrite(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    (void)sock->RemoveOpCtx(header.seqNo);
    sock->DecreaseRef();
    ReturnResources(sock, opCtxInfo);

    return result;
}

SResult SockWorker::PostWrite(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &req)
{
    if (NN_UNLIKELY(!sock->GetQueueSpace())) {
        return SS_SOCK_ADD_QUEUE_FAILED;
    }

    auto opCtxInfo = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(opCtxInfo == nullptr)) {
        NN_LOG_ERROR("Failed to post write sgl with sock worker " << DetailName() << " as no ctx left");
        sock->ReturnQueueSpace(NN_NO1);
        return SS_CTX_FULL;
    }
    opCtxInfo->errType = SockOpContextInfo::SockErrorType::SS_NO_ERROR;
    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to post write sgl with sock worker " << DetailName() << " as no sglCtx left");
        sock->ReturnQueueSpace(NN_NO1);
        mOpCtxInfoPool.Return(opCtxInfo);
        return SS_CTX_FULL;
    }

    opCtxInfo->sock = sock;
    opCtxInfo->opType = SockOpContextInfo::SockOpType::SS_SGL_WRITE;
    opCtxInfo->upCtxSize = req.upCtxSize;
    if (opCtxInfo->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(opCtxInfo->upCtx, NN_NO16, req.upCtxData, opCtxInfo->upCtxSize) != SS_OK)) {
            ReturnResources(sock, opCtxInfo, sglCtx);
            NN_LOG_ERROR("Failed to copy req to opCtxInfo");
            return SS_PARAM_INVALID;
        }
    }

    sglCtx->Clone(header, req.iov, req.iovCount);
    opCtxInfo->sendCtx = sglCtx;

    sock->AddOpCtx(header.seqNo, opCtxInfo);
    sock->IncreaseRef();

    auto result = sock->PostWriteSgl(opCtxInfo);
    // blocking post send need call upper handle
    if (result == SS_OK) {
        sock->ReturnQueueSpace(NN_NO1);
        return result;
    } else if (result == SS_SOCK_SEND_EAGAIN) {
        return ModifyInEpoll(sock, EPOLLIN | EPOLLOUT | EPOLLET);
    } else if (result != SS_TCP_RETRY) {
        auto res = ModifyInEpoll(sock, EPOLLWRNORM);
        result = res == SS_OK ? result : res;
    }
    (void)sock->RemoveOpCtx(header.seqNo);
    sock->DecreaseRef();
    ReturnResources(sock, opCtxInfo);

    return result;
}
}
}