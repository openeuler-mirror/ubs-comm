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
#include "shm_composed_endpoint.h"
namespace ock {
namespace hcom {
HResult ShmSyncEndpoint::Create(const std::string &name, uint16_t eventQueueLength, ShmPollingMode mode,
    ShmSyncEndpointPtr &ep)
{
    ShmSyncEndpointPtr tmpEp = new (std::nothrow) ShmSyncEndpoint(name, eventQueueLength, mode);
    if (NN_UNLIKELY(tmpEp.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to create RDMASyncClientEndPoint, probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    auto result = tmpEp->CreateEventQueue();
    if (NN_UNLIKELY(result != SH_OK)) {
        return result;
    }

    ep.Set(tmpEp.Get());

    return result;
}

HResult ShmSyncEndpoint::CreateEventQueue()
{
    /* get data size */
    uint64_t dataSize = ShmEventQueue::MemSize(mEventQueueLength);

    /* create handle for event queue */
    HResult result = SH_OK;
    ShmHandlePtr tmpHandle = new (std::nothrow) ShmHandle(mName, SHM_F_EVENT_QUEUE_PREFIX, 1, dataSize, true);
    if (NN_UNLIKELY(tmpHandle.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new shm handle for sync ep " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* create and initialize event queue */
    ShmEventQueuePtr tmpQueue = new (std::nothrow) ShmEventQueue(mName, mEventQueueLength, tmpHandle);
    if (NN_UNLIKELY(tmpQueue.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new shm event queue for sync ep " << mName);
        return SH_NEW_OBJECT_FAILED;
    }

    if ((result = tmpQueue->Initialize()) != SH_OK) {
        NN_LOG_ERROR("Failed to initialize shm event queue");
        return result;
    }

    /* assign member variables */
    mHandleEventQueue.Set(tmpHandle.Get());
    mEventQueue = tmpQueue.Get();
    mEventQueue->IncreaseRef();

    return result;
}

HResult ShmSyncEndpoint::PostSend(ShmChannel *ch, const UBSHcomNetTransRequest &req, uint64_t offset, uint32_t immData,
    int32_t defaultTimeout)
{
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }
    mDefaultTimeout = defaultTimeout;

    /* get op completion ctx */
    static thread_local ShmOpCompInfo ctx {};
    if (immData == 0) {
        ctx.header = *(reinterpret_cast<UBSHcomNetTransHeader *>(req.lAddress));
    }
    ctx.header.immData = immData;
    ctx.channel = ch;
    ctx.request = req;
    ctx.opType = immData == 0 ? ShmOpContextInfo::ShmOpType::SH_SEND : ShmOpContextInfo::ShmOpType::SH_SEND_RAW;
    ch->IncreaseRef();

    ShmEvent event(immData, req.size, offset, ch->Id(), ch->PeerChannelId(), ch->PeerChannelAddress(),
        ShmOpContextInfo::ShmOpType::SH_RECEIVE);
    auto result = ch->EQEventEnqueue(event);
    if (NN_UNLIKELY(result != SH_OK)) {
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            result = SH_RETRY_FULL;
        }
        ch->DecreaseRef();
        return result;
    }

    /* send local event for send Waitcompletion */
    ShmEvent eventSent(reinterpret_cast<uintptr_t>(&ctx), ShmOpContextInfo::ShmOpType::SH_SEND);
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    do {
        result = mEventQueue->EnqueueAndNotify(eventSent);
        if (result == SH_OK) {
            flag = false;
            return SH_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            result = SH_SEND_COMPLETION_CALLBACK_FAILURE;
        }

        flag = false;
    } while (flag);

    ch->DecreaseRef();
    return result;
}

HResult ShmSyncEndpoint::FillSglCtx(ShmSglOpContextInfo *sglCtx, const UBSHcomNetTransSglRequest &sglReq)
{
    if (NN_UNLIKELY(sglCtx == nullptr)) {
        NN_LOG_ERROR("Shm Failed to PostSendRawSgl with ShmWorker as no ctx left");
        return SH_PARAM_INVALID;
    }

    sglCtx->result = SH_OK;
    if (NN_UNLIKELY(memcpy_s(sglCtx->iov, sizeof(UBSHcomNetTransSgeIov) * NET_SGE_MAX_IOV, sglReq.iov,
        sizeof(UBSHcomNetTransSgeIov) * sglReq.iovCount) != SH_OK)) {
        NN_LOG_ERROR("Failed to copy req to sglCtx");
        return SH_PARAM_INVALID;
    }
    sglCtx->upCtxSize = sglReq.upCtxSize;
    sglCtx->iovCount = sglReq.iovCount;
    if (sglReq.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(sglCtx->upCtx, NN_NO16, sglReq.upCtxData, sglReq.upCtxSize) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy req to sglCtx");
            return SH_PARAM_INVALID;
        }
    }

    return SH_OK;
}

HResult ShmSyncEndpoint::PostSendRawSgl(ShmChannel *ch, const UBSHcomNetTransRequest &req,
    const UBSHcomNetTransSglRequest &sglReq, uint64_t offset, uint32_t immData, int32_t defaultTimeout)
{
    if (NN_UNLIKELY(sglReq.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostSend with sync endpoint " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }
    mDefaultTimeout = defaultTimeout;

    thread_local ShmSglOpContextInfo sglCtx {};
    auto result = FillSglCtx(&sglCtx, sglReq);
    if (NN_UNLIKELY(result != SH_OK)) {
        return result;
    }

    /* get op completion ctx */
    thread_local ShmOpCompInfo ctx {};
    if (immData == 0) {
        ctx.header = *(reinterpret_cast<UBSHcomNetTransHeader *>(req.lAddress));
    }
    ctx.header.immData = immData;
    ctx.channel = ch;
    ctx.request = req;
    ctx.opType = ShmOpContextInfo::ShmOpType::SH_SEND_RAW_SGL;
    ctx.upCtxSize = sizeof(ShmSglOpCompInfo);
    auto upCtx = reinterpret_cast<ShmSglOpCompInfo *>(&ctx.upCtx);
    upCtx->ctx = &sglCtx;
    ch->IncreaseRef();

    ShmEvent event(immData, req.size, offset, ch->Id(), ch->PeerChannelId(), ch->PeerChannelAddress(),
        ShmOpContextInfo::ShmOpType::SH_RECEIVE);
    result = ch->EQEventEnqueue(event);
    if (NN_UNLIKELY(result != SH_OK)) {
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            result = SH_RETRY_FULL;
        }
        ch->DecreaseRef();
        return result;
    }

    /* send local event for send Waitcompletion */
    ShmEvent eventSent(reinterpret_cast<uintptr_t>(&ctx), ShmOpContextInfo::ShmOpType::SH_SEND);
    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    do {
        result = mEventQueue->EnqueueAndNotify(eventSent);
        if (result == SH_OK) {
            flag = false;
            return SH_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            result = SH_SEND_COMPLETION_CALLBACK_FAILURE;
        }

        flag = false;
    } while (flag);

    ch->DecreaseRef();
    return result;
}

/* Single-side transport process */
static inline HResult SyncReadWriteProcess(UBSHcomNetTransSgeIov &iov, ShmMRHandleMap &mrHandleMap, ShmChannel *ch,
    ShmOpContextInfo::ShmOpType type)
{
    auto localMrHandle = mrHandleMap.GetFromLocalMap(static_cast<uint32_t>(iov.lKey));
    if (NN_UNLIKELY(localMrHandle == nullptr)) {
        NN_LOG_ERROR("Local mr handle is nullptr");
        return SH_ERROR;
    }

    auto remoteMrHandle = mrHandleMap.GetFromRemoteMap(static_cast<uint32_t>(iov.rKey));
    if (remoteMrHandle == nullptr) {
        /* remote address not exist in local map, exchange mr fd and mmap before copy */
        auto result = ch->GetRemoteMrHandle(static_cast<uint32_t>(iov.rKey), iov.size, mrHandleMap);
        if (NN_UNLIKELY(result != NN_OK)) {
            return result;
        }
        remoteMrHandle = mrHandleMap.GetFromRemoteMap(static_cast<uint32_t>(iov.rKey));
    }

    /* address has mmap already, copy directly */
    if (type == ShmOpContextInfo::ShmOpType::SH_READ || type == ShmOpContextInfo::ShmOpType::SH_SGL_READ) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(localMrHandle->ShmAddress()), localMrHandle->DataSize(),
            reinterpret_cast<void *>(remoteMrHandle->ShmAddress()), iov.size) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy remoteMrHandle to localMrHandle");
            return SH_PARAM_INVALID;
        }
    } else if (type == ShmOpContextInfo::ShmOpType::SH_WRITE || type == ShmOpContextInfo::ShmOpType::SH_SGL_WRITE) {
        if (NN_UNLIKELY(memcpy_s(reinterpret_cast<void *>(remoteMrHandle->ShmAddress()), remoteMrHandle->DataSize(),
            reinterpret_cast<void *>(localMrHandle->ShmAddress()), iov.size) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy localMrHandle to remoteMrHandle");
            return SH_PARAM_INVALID;
        }
    } else {
        NN_LOG_ERROR("Failed to PostReadWrite unreachable path");
        return SH_ERROR;
    }

    return SH_OK;
}

HResult ShmSyncEndpoint::SendLocalEventForOneSideDone(ShmOpContextInfo *ctx, ShmOpContextInfo::ShmOpType type)
{
    /* send local event for send WaitCompletion */
    ShmEvent eventSent(reinterpret_cast<uintptr_t>(ctx), type);

    uint64_t finishTime = GetFinishTime();
    bool flag = true;
    HResult result = SH_OK;
    do {
        result = mEventQueue->EnqueueAndNotify(eventSent);
        if (result == SH_OK) {
            flag = false;
            return SH_OK;
        } else if (NeedRetry(result) && mDefaultTimeout != 0 && NetMonotonic::TimeNs() < finishTime) {
            usleep(100UL); // LWT situation is not suitable for calling system sleep
            continue;
        }
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            flag = false;
            return SH_SEND_COMPLETION_CALLBACK_FAILURE;
        }

        flag = false;
    } while (flag);

    return result;
}

HResult ShmSyncEndpoint::PostReadWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap,
    ShmOpContextInfo::ShmOpType type)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    UBSHcomNetTransSgeIov iov {};
    iov.lKey = req.lKey;
    iov.rKey = req.rKey;
    iov.size = req.size;

    // Prevent integer truncation, safely converts uint64_t to uint32_t
    if (NN_UNLIKELY(iov.lKey > UINT32_MAX || iov.rKey > UINT32_MAX)) {
        NN_LOG_ERROR("Shm failed to PostReadWrite with RDMAWorker as Key is larger than uint32max, lkey" <<
            iov.lKey << " rKey " << iov.rKey);
        return SH_PARAM_INVALID;
    }

    HResult result = SH_OK;
    if (NN_UNLIKELY((result = SyncReadWriteProcess(iov, mrHandleMap, ch, type)) != SH_OK)) {
        NN_LOG_ERROR("Failed to read/write data to/from server");
        return result;
    }

    /* get op ctx */
    thread_local ShmOpContextInfo ctx {};
    ctx.channel = ch;
    ctx.mrMemAddr = req.lAddress;
    ctx.lKey = static_cast<uint32_t>(req.lKey);
    ctx.dataSize = req.size;
    ctx.opType = type;
    ctx.upCtxSize = req.upCtxSize;
    if (req.upCtxSize > 0) {
        if (NN_UNLIKELY(memcpy_s(ctx.upCtx, NN_NO16, req.upCtxData, req.upCtxSize) != SH_OK)) {
            NN_LOG_ERROR("Failed to copy req to ctx");
            return SH_PARAM_INVALID;
        }
    }
    ch->IncreaseRef();

    /* send local event for one side completion */
    result = SendLocalEventForOneSideDone(&ctx, type);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        ch->DecreaseRef();
    }

    return result;
}

HResult ShmSyncEndpoint::PostReadWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmMRHandleMap &mrHandleMap, ShmOpContextInfo::ShmOpType type)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostReadWriteSgl type:" << type << " with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    HResult result = SH_OK;
    for (auto i = 0; i < req.iovCount; i++) {
        if (NN_UNLIKELY((result = SyncReadWriteProcess(req.iov[i], mrHandleMap, ch, type)) != SH_OK)) {
            NN_LOG_ERROR("Failed to read/write sgl data to/from server");
            return result;
        }
    }

    /* get op ctx */
    thread_local ShmOpContextInfo ctx {};
    thread_local ShmSglOpContextInfo sglCtx {};
    result = FillSglCtx(&sglCtx, req);
    if (NN_UNLIKELY(result != SH_OK)) {
        return result;
    }

    ctx.channel = ch;
    ctx.mrMemAddr = 0;
    ctx.lKey = 0;
    ctx.dataSize = sizeof(UBSHcomNetTransSgeIov) * req.iovCount;
    ctx.opType = type;
    ctx.upCtxSize = sizeof(ShmSglOpCompInfo);
    auto upCtx = reinterpret_cast<ShmSglOpCompInfo *>(&ctx.upCtx);
    upCtx->ctx = &sglCtx;
    ch->IncreaseRef();

    /* send local event for one side completion */
    result = SendLocalEventForOneSideDone(&ctx, type);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        ch->DecreaseRef();
    }

    return result;
}

HResult ShmSyncEndpoint::Receive(int32_t timeout, ShmOpContextInfo &opCtx, uint32_t &immData)
{
    HResult result = SH_OK;
    ShmEvent event {};
    if (NN_UNLIKELY((result = DequeueEvent(timeout, event)) != SH_OK)) {
        NN_LOG_ERROR("Failed to dequeue event");
        return result;
    }

    auto *ch = reinterpret_cast<ShmChannel *>(event.peerChannelAddress);
    if (NN_UNLIKELY(ch == nullptr)) {
        NN_LOG_ERROR("Got invalid event in EP " << mName << ", dropped it");
        return SH_ERROR;
    }

    uintptr_t address = 0;
    if (NN_UNLIKELY((result = ch->GetPeerDataAddressByOffset(event.dataOffset, address)) != SH_OK)) {
        NN_LOG_ERROR("Got invalid event in worker " << mName << " as get data address failed, dropped it");
        return result;
    }

    ShmOpContextInfo ctx(ch, address, event.dataSize, static_cast<ShmOpContextInfo::ShmOpType>(event.opType),
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR);
    opCtx = ctx;
    immData = event.immData;

    return result;
}

HResult ShmSyncEndpoint::DequeueEvent(int32_t timeout, ShmEvent &opEvent)
{
    int32_t timeoutInMs = TimeSecToMs(timeout);
    HResult result = SH_OK;
    ShmEvent event {};

    if (mShmMode == SHM_BUSY_POLLING) {
        auto start = NetMonotonic::TimeMs();
        do {
            result = mEventQueue->Dequeue(event);
            auto end = NetMonotonic::TimeMs();
            auto pollTime = end - start;
            if (result == ShmEventQueue::SHM_QUEUE_EMPTY && timeoutInMs >= 0 && pollTime > (uint64_t)timeoutInMs) {
                return SH_TIME_OUT;
            }
        } while (result == ShmEventQueue::SHM_QUEUE_EMPTY);
    } else if (mShmMode == SHM_EVENT_POLLING) {
        // stopping param is for worker polling case, it is not used in self polling case, just a placeholder
        bool stopping = false;
        result = mEventQueue->DequeueOrWait(event, stopping, timeoutInMs);
        if (NN_UNLIKELY(result != SH_OK)) {
            return result;
        }
    }
    /* get event */
    opEvent = event;
    return result;
}
}
}