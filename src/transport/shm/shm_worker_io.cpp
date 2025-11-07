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
#include "shm_worker.h"
#include "shm_handle.h"
#include "shm_queue.h"

namespace ock {
namespace hcom {


HResult ShmWorker::FillSglCtx(ShmSglOpContextInfo *sglCtx, const UBSHcomNetTransSglRequest &sglReq)
{
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSendRawSgl with ShmWorker as no ctx left");
        return SH_PARAM_INVALID;
    }

    sglCtx->result = SH_OK;
    if (NN_UNLIKELY(memcpy_s(sglCtx->iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV,
        sglReq.iov, sizeof(UBSHcomNetTransSgeIov) * sglReq.iovCount) != SH_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return SH_PARAM_INVALID;
    }
    sglCtx->iovCount = sglReq.iovCount;
    sglCtx->upCtxSize = sglReq.upCtxSize;
    if (sglReq.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx->upCtx, NN_NO16, sglReq.upCtxData, sglReq.upCtxSize) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            return SH_PARAM_INVALID;
        }
    }

    return SH_OK;
}

HResult ShmWorker::SendLocalEvent(uintptr_t ctx, ShmChannel *ch, ShmOpContextInfo::ShmOpType type)
{
    ShmEvent eventSent(ctx, type);
    eventSent.SetChannel(ch);
    /* if failed decrease in this thread, if success or broken decrease worker thread */
    eventSent.shmChannel->IncreaseRef();

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    HResult result = SH_OK;
    do {
        if (mOptions.mode == SHM_EVENT_POLLING) {
            result = mEventQueue->EnqueueAndNotify(eventSent);
        } else if (mOptions.mode == SHM_BUSY_POLLING) {
            result = mEventQueue->Enqueue(eventSent);
        }
        if (result == SH_OK) {
            return SH_OK;
        } else if (NeedRetry(result, ch) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            eventSent.shmChannel->DecreaseRef();
            return SH_SEND_COMPLETION_CALLBACK_FAILURE;
        }

        flag = false;
    } while (flag);

    eventSent.shmChannel->DecreaseRef();
    return result;
}

HResult ShmWorker::PostSendRawSgl(ShmChannel *ch, const UBSHcomNetTransRequest &req,
    const UBSHcomNetTransSglRequest &sglReq, uint64_t offset, uint32_t immData, int32_t defaultTimeout = -1)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(sglReq.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Shm Failed to PostSend with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(ch->State().Compare(CH_BROKEN))) {
        NN_LOG_ERROR("Shm Failed to PostSend with ShmWorker " << mName << " as ch status is broken");
        return SH_CH_BROKEN;
    }

    mDefaultTimeout = defaultTimeout;

    ShmEvent event(immData, req.size, offset, ch->Id(), ch->PeerChannelId(), ch->PeerChannelAddress(),
        ShmOpContextInfo::ShmOpType::SH_RECEIVE);
    auto result = ch->EQEventEnqueue(event);
    if (NN_UNLIKELY(result != SH_OK)) {
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            result = SH_RETRY_FULL;
        }
        return result;
    }

    /* get op sgl ctx */
    auto sglCtx = mSglCtxInfoPool.Get();
    result = FillSglCtx(sglCtx, sglReq);
    if (NN_UNLIKELY(result != SH_OK)) {
        return result;
    }

    /* get op completion ctx */
    auto ctx = mOpCompInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Shm Failed to PostSend with ShmWorker " << mName << " as no opCtx left");
        return SH_OP_CTX_FULL;
    }

    bzero(ctx, sizeof(ShmOpCompInfo));
    if (immData == 0) {
        ctx->header = *(reinterpret_cast<UBSHcomNetTransHeader *>(req.lAddress));
    }
    ctx->header.immData = immData;
    ctx->channel = ch;
    ctx->request = req;
    ctx->opType = ShmOpContextInfo::ShmOpType::SH_SEND_RAW_SGL;
    ctx->upCtxSize = sizeof(ShmSglOpCompInfo);
    auto upCtx = static_cast<ShmSglOpCompInfo *>((void *)&(ctx->upCtx));
    upCtx->ctx = sglCtx;
    ch->IncreaseRef();
    ch->AddOpCompInfo(ctx);

    /* send local event for send completion callback */
    result = SendLocalEvent(reinterpret_cast<uintptr_t>(ctx), ch, ShmOpContextInfo::ShmOpType::SH_SEND_RAW_SGL);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        /* if state is broken ch of ctx has already decreased\remove\return in keeper thread, ensure not deal twice */
        /* if state is ok ch of ctx has already decreased in worker thread */
        if (NN_UNLIKELY(ch->RemoveOpCompInfo(ctx) != SH_OK)) {
            return result;
        }
        ch->DecreaseRef();
        mOpCompInfoPool.Return(ctx);
        mSglCtxInfoPool.Return(sglCtx);
    }

    return result;
}

static inline HResult ReadWriteProcess(UBSHcomNetTransSgeIov iov, ShmMRHandleMap &mrHandleMap, ShmChannel *ch,
    ShmOpContextInfo::ShmOpType type)
{
    auto localMemHandle = mrHandleMap.GetFromLocalMap(static_cast<uint32_t>(iov.lKey));
    if (NN_UNLIKELY(localMemHandle == nullptr)) {
        return SH_ERROR;
    }

    auto remoteMemHandle = mrHandleMap.GetFromRemoteMap(static_cast<uint32_t>(iov.rKey));
    if (remoteMemHandle == nullptr) {
        /* remote address not exist in local map, exchange mr fd and mmap before copy */
        auto result = ch->GetRemoteMrHandle(static_cast<uint32_t>(iov.rKey), iov.size, mrHandleMap);
        if (NN_UNLIKELY(result != NN_OK)) {
            return result;
        }
        remoteMemHandle = mrHandleMap.GetFromRemoteMap(static_cast<uint32_t>(iov.rKey));
    }

    /* address has mmap already, copy directly */
    if (type == ShmOpContextInfo::ShmOpType::SH_READ || type == ShmOpContextInfo::ShmOpType::SH_SGL_READ) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(localMemHandle->ShmAddress()), localMemHandle->DataSize(),
            reinterpret_cast<void *>(remoteMemHandle->ShmAddress()), iov.size) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy remoteMemHandle to localMemHandle");
            return SH_PARAM_INVALID;
        }
    } else if (type == ShmOpContextInfo::ShmOpType::SH_WRITE || type == ShmOpContextInfo::ShmOpType::SH_SGL_WRITE) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(remoteMemHandle->ShmAddress()), remoteMemHandle->DataSize(),
            reinterpret_cast<void *>(localMemHandle->ShmAddress()), iov.size) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy localMemHandle to remoteMemHandle");
            return SH_PARAM_INVALID;
        }
    } else {
        NN_LOG_INFO("Failed to PostReadWrite unreachable path");
        return SH_ERROR;
    }

    return SH_OK;
}

HResult ShmWorker::PostReadWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap,
    ShmOpContextInfo::ShmOpType type)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostReadWrite type:" << type << " with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(ch->State().Compare(CH_BROKEN))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as ch status is broken");
        return SH_CH_BROKEN;
    }

    UBSHcomNetTransSgeIov iov {};
    iov.lKey = req.lKey;
    iov.rKey = req.rKey;
    iov.size = req.size;

    HResult result = SH_OK;
    if (NN_UNLIKELY((result = ReadWriteProcess(iov, mrHandleMap, ch, type)) != SH_OK)) {
        return result;
    }

    /* get op ctx */
    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadWrite type:" << type << " with ShmWorker " << mName << " as no opCtx left");
        return SH_OP_CTX_FULL;
    }

    bzero(ctx, sizeof(ShmOpContextInfo));
    ctx->channel = ch;
    ctx->mrMemAddr = req.lAddress;
    ctx->lKey = static_cast<uint32_t>(req.lKey);
    ctx->dataSize = req.size;
    ctx->opType = type;
    ctx->upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(ctx->upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            return NN_INVALID_PARAM;
        }
    }
    ch->IncreaseRef();
    ch->AddOpCtxInfo(ctx);

    /* send local event for one side done callback */
    result = SendLocalEvent(reinterpret_cast<uintptr_t>(ctx), ch, type);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        /* if state is broken ch of ctx has already decreased\remove\return in keeper thread, ensure not deal twice */
        /* if state is ok ch of ctx has already decreased in worker thread */
        if (NN_UNLIKELY(ch->RemoveOpCtxInfo(ctx) != SH_OK)) {
            return result;
        }
        ch->DecreaseRef();
        mOpCtxInfoPool.Return(ctx);
    }

    return result;
}


static inline void FillReadWriteSglCtx(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmOpContextInfo::ShmOpType type, ShmOpContextInfo *ctx, ShmSglOpContextInfo *sglCtx)
{
    if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(sglCtx->iov), sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV,
        reinterpret_cast<void *>(req.iov), sizeof(UBSHcomNetTransSgeIov) * req.iovCount) != SH_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return;
    }
    sglCtx->iovCount = req.iovCount;
    sglCtx->upCtxSize = req.upCtxSize;
    if (sglCtx->upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx->upCtx, NN_NO16, req.upCtxData, sglCtx->upCtxSize) != NN_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            return;
        }
    }
    bzero(ctx, sizeof(ShmOpContextInfo));
    ctx->channel = ch;
    ctx->mrMemAddr = 0;
    ctx->lKey = 0;
    ctx->dataSize = sizeof(UBSHcomNetTransSgeIov) * req.iovCount;
    ctx->opType = type;
    ctx->upCtxSize = sizeof(ShmSglOpCompInfo);

    auto upCtx = reinterpret_cast<ShmSglOpCompInfo *>(&ctx->upCtx);
    upCtx->ctx = sglCtx;
}

HResult ShmWorker::PostReadWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap,
    ShmOpContextInfo::ShmOpType type)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostReadWriteSgl type:" << type << " with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(ch->State().Compare(CH_BROKEN))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as ch status is broken");
        return SH_CH_BROKEN;
    }

    /* get op ctx */
    auto ctx = mOpCtxInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadWriteSgl type:" << type << " with ShmWorker " << mName << " as no ctx left");
        return SH_OP_CTX_FULL;
    }

    auto sglCtx = mSglCtxInfoPool.Get();
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Failed to PostReadWriteSgl with ShmWorker " << mName << " as no  sglCtx left");
        mOpCtxInfoPool.Return(ctx);
        return SH_PARAM_INVALID;
    }

    FillReadWriteSglCtx(ch, req, type, ctx, sglCtx);

    ch->IncreaseRef();
    ch->AddOpCtxInfo(ctx);

    HResult result = SH_OK;
    for (auto i = 0; i < req.iovCount; i++) {
        if (NN_UNLIKELY((result = ReadWriteProcess(req.iov[i], mrHandleMap, ch, type)) != SH_OK)) {
            if (NN_UNLIKELY(ch->RemoveOpCtxInfo(ctx) != SH_OK)) {
                return result;
            }
            ch->DecreaseRef();
            mOpCtxInfoPool.Return(ctx);
            mSglCtxInfoPool.Return(sglCtx);
            return result;
        }
    }

    /* send local event for one side done callback */
    result = SendLocalEvent(reinterpret_cast<uintptr_t>(ctx), ch, type);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        /* if state is broken ch of ctx has already decreased\remove\return in keeper thread, ensure not deal twice */
        /* if state is ok ch of ctx has already decreased in worker thread */
        if (NN_UNLIKELY(ch->RemoveOpCtxInfo(ctx) != SH_OK)) {
            return result;
        }
        ch->DecreaseRef();
        mOpCtxInfoPool.Return(ctx);
        mSglCtxInfoPool.Return(sglCtx);
    }

    return result;
}
}
}