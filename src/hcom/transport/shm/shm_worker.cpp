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
std::atomic<uint64_t> ShmWorker::shmWorkerIndex(0);

ShmWorker::ShmWorker(const std::string &name, const UBSHcomNetWorkerIndex &index, const ShmWorkerOptions &options,
    const NetMemPoolFixedPtr &opMemPool, const NetMemPoolFixedPtr &opCtxMemPool, const NetMemPoolFixedPtr &sglOpMemPool)
    : mName(name + index.ToString()), mIndex(index), mOptions(options)
{
    if (mOpCompInfoPool.Initialize(opMemPool) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize op complete pool for worker " << mName);
    }

    if (mOpCtxInfoPool.Initialize(opCtxMemPool) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize op ctx pool for worker " << mName);
    }

    if (mSglCtxInfoPool.Initialize(sglOpMemPool) != NN_OK) {
        NN_LOG_ERROR("Failed to initialize sgl op ctx pool for worker " << mName);
    }

    OBJ_GC_INCREASE(ShmWorker);
}

HResult ShmWorker::Initialize()
{
    std::lock_guard<std::mutex> locker(mMutex);
    if (mInited) {
        return SH_OK;
    }

    /* validate */
    HResult result = SH_OK;
    if ((result = Validate()) != SH_OK) {
        NN_LOG_ERROR("Failed to validate in shm worker initialize");
        return result;
    }

    /* create event queue */
    if ((result = CreateEventQueue()) != SH_OK) {
        NN_LOG_ERROR("Failed to create event queue in shm worker initialize");
        return result;
    }

    mInited = true;
    return SH_OK;
}

void ShmWorker::UnInitialize()
{
    std::lock_guard<std::mutex> locker(mMutex);
    if (!mInited) {
        return;
    }

    if (mEventQueue != nullptr) {
        mEventQueue->DecreaseRef();
        mEventQueue = nullptr;
    }
}

HResult ShmWorker::Validate()
{
    // do later
    return SH_OK;
}

HResult ShmWorker::CreateEventQueue()
{
    if (mEventQueue != nullptr) {
        NN_LOG_ERROR("Event queue is already created in shm worker " << mName);
        return SH_ERROR;
    }

    /* get id and data size */
    auto id = shmWorkerIndex++;
    uint64_t dataSize = ShmEventQueue::MemSize(mOptions.eventQueueLength);

    /* create handle for event queue */
    HResult result = SH_OK;
    ShmHandlePtr tmpHandle = new (std::nothrow) ShmHandle(mName, SHM_F_EVENT_QUEUE_PREFIX, id, dataSize, true);
    if (NN_UNLIKELY(tmpHandle.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new shm handle for worker " << mName << ", probably out of memory");
        return SH_NEW_OBJECT_FAILED;
    }

    /* create and initialize event queue */
    ShmEventQueuePtr tmpQueue = new (std::nothrow) ShmEventQueue(mName, mOptions.eventQueueLength, tmpHandle);
    if (NN_UNLIKELY(tmpQueue.Get() == nullptr)) {
        NN_LOG_ERROR("Failed to new shm event queue for worker " << mName);
        return SH_NEW_OBJECT_FAILED;
    }

    if ((result = tmpQueue->Initialize()) != SH_OK) {
        return result;
    }

    /* assign member variables */
    mHandleEventQueue.Set(tmpHandle.Get());
    mEventQueue = tmpQueue.Get();
    mEventQueue->IncreaseRef();

    return SH_OK;
}

void SetThreadNameAndAffinity(const std::string& name, int16_t cpuId)
{
    pthread_setname_np(pthread_self(), name.c_str());

    if ((cpuId) != -1) {
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(cpuId, &cpuSet);
        if (pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) != 0) {
            NN_LOG_WARN("Unable to bind shm worker " << name << " << to cpu " << cpuId);
        }
    }
}

void ShmWorker::RunInThread(int16_t cpuId)
{
    SetThreadNameAndAffinity("shmWorker" + mIndex.ToString(), cpuId);

    if (mOptions.threadPriority != 0) {
        if (NN_UNLIKELY(setpriority(PRIO_PROCESS, 0, mOptions.threadPriority) != 0)) {
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_WARN("Unable to set worker thread priority in shm worker " << mName << ", errno:" << errno <<
                " error:" << NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    mProgressThrStarted.store(true);

    if (mOptions.mode == SHM_EVENT_POLLING) {
        DoEventPolling();
    } else if (mOptions.mode == SHM_BUSY_POLLING) {
        DoBusyPolling();
    }

    NN_LOG_INFO("Shm worker " << mName << " progress thread exiting");
}

#define HANDLE_OP(event)                                                                                             \
    do {                                                                                                             \
        if ((event).opType == ShmOpContextInfo::ShmOpType::SH_SEND ||                                                \
            (event).opType == ShmOpContextInfo::ShmOpType::SH_SEND_RAW_SGL) {                                        \
            if (NN_UNLIKELY((event).shmChannel == nullptr)) {                                                        \
                NN_LOG_WARN("Got invalid event " << (event).opType << ", in worker " << mName << " as ch is null");  \
                /* if state is broken ch has already decreased\remove\return in keeper thread */                     \
                continue;                                                                                            \
            }                                                                                                        \
            if (NN_UNLIKELY((event).shmChannel->State().Compare(CH_BROKEN))) {                                       \
                (event).shmChannel->DecreaseRef();                                                                   \
                NN_LOG_WARN("Got invalid event " << (event).opType << " in worker " << mName << " as ch is broken"); \
                /* if state is broken ch has already decreased\remove\return in keeper thread */                     \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            auto compEvent = reinterpret_cast<ShmOpCompInfo *>((event).peerChannelAddress);                          \
            if (NN_UNLIKELY(compEvent == nullptr)) {                                                                 \
                NN_LOG_WARN("Got invalid event " << (event).opType << " as ctx is null");                            \
                (event).shmChannel->DecreaseRef();                                                                   \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            /* if state is broken in this line, ch has already decreased\remove\return in keeper thread, res is 0 */ \
            if (NN_UNLIKELY(compEvent->channel->RemoveOpCompInfo(compEvent) != SH_OK)) {                             \
                NN_LOG_WARN("Got invalid event " << (event).opType << " as ctx may be removed by keeper thread");    \
                (event).shmChannel->DecreaseRef();                                                                   \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            /* call upper completion function */                                                                     \
            /* decrease channel ref and return comp info in upper call */                                            \
            mSendPostedHandler(*compEvent);                                                                          \
            (event).shmChannel->DecreaseRef();                                                                       \
        } else if ((event).opType == ShmOpContextInfo::ShmOpType::SH_RECEIVE) {                                      \
            auto *ch = reinterpret_cast<ShmChannel *>((event).peerChannelAddress);                                   \
            /* if reset by peer ch ref decrease to 0, ch will be 0 */                                                \
            if (NN_UNLIKELY(ch == nullptr)) {                                                                        \
                NN_LOG_WARN("Got invalid event " << (event).opType << " in worker " << mName << ", dropped it");     \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            if (NN_UNLIKELY(!ch->State().Compare(CH_NEW))) {                                                         \
                NN_LOG_WARN("Got invalid event " << (event).opType << " in worker " << mName << " as ch is broken"); \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            uintptr_t address = 0;                                                                                   \
            if (NN_UNLIKELY((ch->GetPeerDataAddressByOffset((event).dataOffset, address)) != SH_OK)) {               \
                NN_LOG_WARN("Got invalid event in worker " << mName << " as get data address failed, dropped it");   \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            ShmOpContextInfo ctx(ch, address, (event).dataSize,                                                      \
                static_cast<ShmOpContextInfo::ShmOpType>((event).opType),                                            \
                ShmOpContextInfo::ShmErrorType::SH_NO_ERROR);                                                        \
            mNewRequestHandler(ctx, (event).immData);                                                                \
        } else if ((event).opType == ShmOpContextInfo::ShmOpType::SH_READ ||                                         \
            (event).opType == ShmOpContextInfo::ShmOpType::SH_WRITE ||                                               \
            (event).opType == ShmOpContextInfo::ShmOpType::SH_SGL_READ ||                                            \
            (event).opType == ShmOpContextInfo::ShmOpType::SH_SGL_WRITE) {                                           \
            if (NN_UNLIKELY((event).shmChannel == nullptr)) {                                                        \
                NN_LOG_WARN("Got invalid event " << (event).opType << " in worker " << mName << " as ch is null");   \
                /* if state is broken ch has already decreased\remove\return in keeper thread */                     \
                continue;                                                                                            \
            }                                                                                                        \
            if (NN_UNLIKELY((event).shmChannel->State().Compare(CH_BROKEN))) {                                       \
                (event).shmChannel->DecreaseRef();                                                                   \
                NN_LOG_WARN("Got invalid event " << (event).opType << " in worker " << mName << " as ch is broken"); \
                /* if state is broken ch has already decreased\remove\return in keeper thread */                     \
                continue;                                                                                            \
            }                                                                                                        \
                                                                                                                     \
            auto ctx = reinterpret_cast<ShmOpContextInfo *>((event).peerChannelAddress);                             \
            if (NN_UNLIKELY(ctx == nullptr)) {                                                                       \
                NN_LOG_WARN("Got invalid event " << (event).opType << " as ctx is null, ch may be broken");          \
                (event).shmChannel->DecreaseRef();                                                                   \
                continue;                                                                                            \
            }                                                                                                        \
            /* if state is broken in this line, ch has already decreased\remove\return in keeper thread, res is 0 */ \
            if (NN_UNLIKELY((event).shmChannel->RemoveOpCtxInfo(ctx) != SH_OK)) {                                    \
                NN_LOG_WARN("Got invalid event " << (event).opType << " as ctx may be removed by keeper thread");    \
                (event).shmChannel->DecreaseRef();                                                                   \
                continue;                                                                                            \
            }                                                                                                        \
            /* call upper completion function */                                                                     \
            /* decrease channel ref and return comp info in upper call */                                            \
            mOneSideDoneHandler(ctx);                                                                                \
            (event).shmChannel->DecreaseRef();                                                                       \
        }                                                                                                            \
    } while (0)

void ShmWorker::DoEventPolling()
{
    ShmEvent event {};
    bool stopping = false;

    HResult result;
    while (!mNeedToStop) {
        if (NN_UNLIKELY((result = mEventQueue->DequeueOrWait(event, stopping, mOptions.pollingTimeoutMs)) != SH_OK)) {
            /* timeout need invoke idle */
            if (mIdleHandler != nullptr) {
                mIdleHandler(mIndex);
            }
            continue;
        }

        if (NN_UNLIKELY(stopping)) {
            NN_LOG_INFO("Get stop sign in shm worker " << mName << ", stopping");
            break;
        }

        TRACE_DELAY_BEGIN(SHM_WORKER_EVENT_POLLING);
        HANDLE_OP(event);
        TRACE_DELAY_END(SHM_WORKER_EVENT_POLLING, 0);

        NN_LOG_TRACE_INFO("got event " << event.ToString() << ", result " << result);
    }
}

void ShmWorker::DoBusyPolling()
{
    ShmEvent event {};

    while (!mNeedToStop) {
        auto result = mEventQueue->Dequeue(event);
        if (result == ShmEventQueue::SHM_QUEUE_EMPTY) {
            // check if any producer stuck in enqueue. If stuck, kick it out
            mEventQueue->CheckAndMarkProducerState();
            /* if there is no coming request, call up idle function */
            if (mIdleHandler != nullptr) {
                mIdleHandler(mIndex);
            }
            continue;
        }

        TRACE_DELAY_BEGIN(SHM_WORKER_BUSY_POLLING);
        HANDLE_OP(event);
        TRACE_DELAY_END(SHM_WORKER_BUSY_POLLING, 0);

        NN_LOG_TRACE_INFO("got event " << event.ToString() << ", result " << result);
    }
}

HResult ShmWorker::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mInited) {
        NN_LOG_ERROR("Failed to start shm worker " << mName << " as it is not initialized");
        return SH_ERROR;
    }

    if (mStarted) {
        NN_LOG_WARN("Unable to start shm worker " << mName << " as it is already started");
        return SH_OK;
    }

    /* validate handler */
    if (mNewRequestHandler == nullptr) {
        NN_LOG_ERROR("Failed to start shm worker " << mName << " as new request handler is null");
        return SH_PARAM_INVALID;
    }

    if (mSendPostedHandler == nullptr) {
        NN_LOG_ERROR("Failed to start shm worker " << mName << " as request posted handler is null");
        return SH_PARAM_INVALID;
    }

    if (mOneSideDoneHandler == nullptr) {
        NN_LOG_ERROR("Failed to start shm worker " << mName << " as one side done handler is null");
        return SH_PARAM_INVALID;
    }

    mNeedToStop = false;
    std::thread tmpThread(&ShmWorker::RunInThread, this, mOptions.cpuId);
    mProgressThr = std::move(tmpThread);

    while (!mProgressThrStarted.load()) {
        usleep(NN_NO10);
    }

    mProgressThrStarted = false;

    mStarted = true;
    return SH_OK;
}

void ShmWorker::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    mNeedToStop = true;

    if (mOptions.mode == SHM_EVENT_POLLING) {
        mEventQueue->LocalStopAndNotify();
    }

    if (mProgressThr.joinable()) {
        mProgressThr.join();
    }

    mStarted = false;
}
}
}