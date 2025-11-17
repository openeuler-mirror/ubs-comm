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
#ifndef OCK_HCOM_SHM_WORKER_H
#define OCK_HCOM_SHM_WORKER_H

#include <sys/resource.h>

#include "net_monotonic.h"
#include "shm_common.h"
#include "shm_handle.h"
#include "shm_queue.h"
#include "shm_channel.h"

namespace ock {
namespace hcom {
using ShmNewReqHandler = std::function<int(ShmOpContextInfo &, int32_t)>;
using ShmPostedHandler = std::function<int(ShmOpCompInfo &)>;
using ShmOneSideHandler = std::function<int(ShmOpContextInfo *)>;

struct ShmWorkerOptions {
    ShmPollingMode mode = SHM_BUSY_POLLING;
    uint16_t eventQueueLength = NN_NO8192;
    int16_t cpuId = -1;
    uint32_t pollingTimeoutMs = NN_NO500; /* epoll or poll timeout */
    uint16_t pollingBatchSize = NN_NO8;
    /* worker thread priority [-20,20], 20 is the lowest, -20 is the highest, 0 (default) means do not set priority */
    int threadPriority = 0;

    std::string ToShortString() const
    {
        std::ostringstream oss;
        oss << "mode: " << ShmPollingModeToStr(mode) << ", poll-timeout: " << pollingTimeoutMs << "us, event-q-cap: " <<
            eventQueueLength;
        return oss.str();
    }
};

class ShmWorker {
public:
    ShmWorker(const std::string &name, const UBSHcomNetWorkerIndex &index, const ShmWorkerOptions &options,
        const NetMemPoolFixedPtr &opMemPool, const NetMemPoolFixedPtr &opCtxMemPool,
        const NetMemPoolFixedPtr &sglOpMemPool);

    ~ShmWorker()
    {
        Stop();
        UnInitialize();

        OBJ_GC_DECREASE(ShmWorker);
    }

    HResult Initialize();
    void UnInitialize();

    HResult Start();
    void Stop();

    inline void RegisterNewReqHandler(const ShmNewReqHandler &h)
    {
        mNewRequestHandler = h;
    }

    inline void RegisterReqPostedHandler(const ShmPostedHandler &h)
    {
        mSendPostedHandler = h;
    }

    inline void RegisterOneSideHandler(const ShmOneSideHandler &h)
    {
        mOneSideDoneHandler = h;
    }

    inline void RegisterIdleHandler(const ShmIdleHandler &h)
    {
        mIdleHandler = h;
    }

    inline const std::string &Name() const
    {
        return mName;
    }

    inline bool FillQueueExchangeInfo(ShmConnExchangeInfo &info)
    {
        if (NN_UNLIKELY(mEventQueue != nullptr)) {
            info.qCapacity = mEventQueue->Capacity();
        }

        if (NN_LIKELY(mHandleEventQueue.Get() != nullptr)) {
            info.queueFd = mHandleEventQueue->Fd();
            return info.SetQueueName(mHandleEventQueue->FullPath());
        }

        info.mode = mOptions.mode;
        return false;
    }

    inline const UBSHcomNetWorkerIndex &Index() const
    {
        return mIndex;
    }

    inline void ReturnOpContextInfo(ShmOpContextInfo *ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            if (NN_LIKELY(ctx->channel != nullptr)) {
                ctx->channel->DecreaseRef();
            }
            mOpCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline void ReturnOpCompInfo(ShmOpCompInfo *ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            if (NN_LIKELY(ctx->channel != nullptr)) {
                ctx->channel->DecreaseRef();
            }
            mOpCompInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline void ReturnSglContextInfo(ShmSglOpContextInfo *&ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            mSglCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    HResult PostSend(ShmChannel *ch, const UBSHcomNetTransRequest &req, uint64_t offset, uint32_t immData,
        int32_t defaultTimeout);
    HResult PostSendRawSgl(ShmChannel *ch, const UBSHcomNetTransRequest &req, const UBSHcomNetTransSglRequest &sglReq,
        uint64_t offset, uint32_t immData, int32_t defaultTimeout);
    HResult PostRead(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostReadSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    HResult Validate();
    HResult CreateEventQueue();
    void RunInThread(int16_t cpuId);

    void DoEventPolling();
    void DoBusyPolling();

    HResult PostReadWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap,
        ShmOpContextInfo::ShmOpType type);

    HResult PostReadWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap,
        ShmOpContextInfo::ShmOpType type);

    uint64_t inline GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }
    static bool inline NeedRetry(HResult &result, ShmChannel *ch)
    {
        if (NN_UNLIKELY(ch->State().Compare(CH_BROKEN))) {
            result = SH_CH_BROKEN;
            return false;
        }

        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            return true;
        }

        return false;
    }

    HResult FillSglCtx(ShmSglOpContextInfo *sglCtx, const UBSHcomNetTransSglRequest &sglReq);
    HResult SendLocalEvent(uintptr_t ctx, ShmChannel *ch, ShmOpContextInfo::ShmOpType type);

private:
    static std::atomic<uint64_t> shmWorkerIndex;

private:
    std::string mName;
    std::mutex mMutex;
    UBSHcomNetWorkerIndex mIndex {};
    bool mInited = false;
    int32_t mDefaultTimeout = -1;

    ShmWorkerOptions mOptions {};

    /* variable for thread */
    std::thread mProgressThr;                       /* thread object of progress */
    bool mStarted = false;                          /* thread already started or not */
    std::atomic_bool mProgressThrStarted { false }; /* started flag */
    volatile bool mNeedToStop = false;              /* flag to be stopped */

    ShmOpCompInfoPool mOpCompInfoPool;     /* op completion pool */
    ShmOpContextInfoPool mOpCtxInfoPool;   /* op completion pool */
    ShmSglContextInfoPool mSglCtxInfoPool; /* sgl op context pool */

    ShmEventQueue *mEventQueue = nullptr;            /* event queue for polling with both event and busy mode */
    ShmNewReqHandler mNewRequestHandler = nullptr;   /* request process related */
    ShmPostedHandler mSendPostedHandler = nullptr;   /* send request posted process related */
    ShmOneSideHandler mOneSideDoneHandler = nullptr; /* one side done will call this */
    ShmIdleHandler mIdleHandler = nullptr;           /* no request will call this */

    ShmHandlePtr mHandleEventQueue; /* handle of event queue */

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

inline HResult ShmWorker::PostSend(ShmChannel *ch, const UBSHcomNetTransRequest &req, uint64_t offset, uint32_t immData,
    int32_t defaultTimeout = -1)
{
    /* upper caller need to make sure ch is not null */
    if (NN_UNLIKELY(req.upCtxSize > sizeof(ShmOpContextInfo::upCtx))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as upCtxSize > " <<
            sizeof(ShmOpContextInfo::upCtx));
        return SH_PARAM_INVALID;
    }

    if (NN_UNLIKELY(ch->State().Compare(CH_BROKEN))) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as ch status is broken");
        return SH_CH_BROKEN;
    }

    mDefaultTimeout = defaultTimeout;

    ShmEvent event(immData, req.size, offset, ch->Id(), ch->PeerChannelId(), ch->PeerChannelAddress(),
        ShmOpContextInfo::ShmOpType::SH_RECEIVE);
    auto result = ch->EQEventEnqueue(event);
    if (NN_UNLIKELY(result != SH_OK)) {
        if (result == ShmEventQueue::SHM_QUEUE_FULL) {
            NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as event queue is full");
            result = SH_RETRY_FULL;
        }
        return result;
    }

    /* get op completion ctx */
    auto ctx = mOpCompInfoPool.Get();
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to PostSend with ShmWorker " << mName << " as no opCtx left");
        return SH_OP_CTX_FULL;
    }

    bzero(ctx, sizeof(ShmOpCompInfo));
    if (immData == 0) {
        ctx->header = *(reinterpret_cast<UBSHcomNetTransHeader *>(req.lAddress));
    }
    ctx->header.immData = immData;
    ctx->channel = ch;
    ctx->request = req;
    ctx->opType = immData == 0 ? ShmOpContextInfo::ShmOpType::SH_SEND : ShmOpContextInfo::ShmOpType::SH_SEND_RAW;
    ch->IncreaseRef();
    ch->AddOpCompInfo(ctx);

    /* send local event for send completion callback */
    result = SendLocalEvent(reinterpret_cast<uintptr_t>(ctx), ch, ShmOpContextInfo::ShmOpType::SH_SEND);
    if (NN_UNLIKELY(result != SH_OK && result != SH_CH_BROKEN)) {
        /* if state is broken ch of ctx has already decreased\remove\return in keeper thread, ensure not deal twice */
        /* if state is ok ch of ctx has already decreased in worker thread */
        if (NN_UNLIKELY(ch->RemoveOpCompInfo(ctx) != SH_OK)) {
            return result;
        }
        ch->DecreaseRef();
        mOpCompInfoPool.Return(ctx);
    }

    return result;
}

inline HResult ShmWorker::PostRead(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap)
{
    return PostReadWrite(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_READ);
}

inline HResult ShmWorker::PostReadSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWriteSgl(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_SGL_READ);
}

inline HResult ShmWorker::PostWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap)
{
    return PostReadWrite(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_WRITE);
}

inline HResult ShmWorker::PostWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWriteSgl(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_SGL_WRITE);
}
}
}

#endif // OCK_HCOM_SHM_WORKER_H
