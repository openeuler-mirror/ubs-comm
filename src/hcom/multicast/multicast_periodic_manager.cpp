/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <new>

#include <sys/epoll.h>
#include <unistd.h>

#include "include/multicast_publisher.h"
#include "include/multicast_publisher_service.h"
#include "multicast_periodic_manager.h"

namespace ock {
namespace hcom {
namespace {
inline uint64_t MakeFreeHead(uint32_t tag, uint32_t index)
{
    return (static_cast<uint64_t>(tag) << 32) | index;
}

inline uint32_t FreeHeadIndex(uint64_t head)
{
    return static_cast<uint32_t>(head);
}

inline uint32_t FreeHeadTag(uint64_t head)
{
    return static_cast<uint32_t>(head >> 32);
}
} // namespace

#define BIND_CPU(cpuId, tmpThread) /* bind cpu */                                                \
    if ((cpuId) >= 0) {                                                                          \
        cpu_set_t cpuSet;                                                                        \
        CPU_ZERO(&cpuSet);                                                                       \
        CPU_SET(static_cast<uint32_t>(cpuId), &cpuSet);                                          \
        if (pthread_setaffinity_np((tmpThread).native_handle(), sizeof(cpuSet), &cpuSet) != 0) { \
            NN_LOG_WARN("Unable to bind periodic manager to cpu " << (cpuId));                   \
        }                                                                                        \
    }

SerResult MultiCastPeriodicManager::Start()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (mStarted) {
        return SER_OK;
    }

    if (mThreadCount > M_MAX_THREAD_NUM) {
        NN_LOG_ERROR("Invalid thread count " << mThreadCount);
        return SER_INVALID_PARAM;
    }

    SerResult result = InitializeIoContexts();
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }

    mStartedWorkingThreads.store(0, std::memory_order_relaxed);
    mWorkerStartFailed.store(false, std::memory_order_relaxed);
    mNeedStop.store(false, std::memory_order_release);
    /* create threads */
    for (uint16_t i = 0; i < mThreadCount; i++) {
        std::thread tmpThread(&MultiCastPeriodicManager::RunInThread, this, i);
        if (!tmpThread.native_handle()) {
            NN_LOG_ERROR("Create manager thread failed");
            StopInner();
            return SER_CREATE_TIMEOUT_THREAD_FAILED;
        }

        /* set thread name */
        if (pthread_setname_np(tmpThread.native_handle(), ("MultiPerMgr" + std::to_string(i)).c_str()) != 0) {
            NN_LOG_WARN("Unable to set thread name of periodic manager");
        }

        BIND_CPU(mCpuId < 0 ? mCpuId : mCpuId + i, tmpThread);

        mWorkingThreads[i] = std::move(tmpThread);
    }

    while (mStartedWorkingThreads.load() != mThreadCount && !mWorkerStartFailed.load(std::memory_order_acquire)) {
        usleep(NN_NO10);
    }
    if (NN_UNLIKELY(mWorkerStartFailed.load(std::memory_order_acquire))) {
        NN_LOG_ERROR("Start multicast periodic manager worker failed.");
        StopInner();
        return SER_CREATE_TIMEOUT_THREAD_FAILED;
    }

    mStarted = true;
    return SER_OK;
}

void MultiCastPeriodicManager::Stop()
{
    std::lock_guard<std::mutex> guard(mMutex);
    if (!mStarted) {
        return;
    }

    StopInner();
    mStarted = false;
}

void MultiCastPeriodicManager::StopInner()
{
    mNeedStop.store(true, std::memory_order_release);
    for (uint16_t i = 0; i < mThreadCount; i++) {
        if (mWorkingThreads[i].joinable()) {
            mWorkingThreads[i].join();
        }

        ProcessCleanUp(i);
    }
    UnInitializeIoContexts();
}

SerResult MultiCastPeriodicManager::InitializeIoContexts()
{
    if (NN_UNLIKELY(mIoCapacity == 0 || mIoCapacity > 0xFFFFFFU)) {
        NN_LOG_ERROR("Invalid multicast IO context capacity " << mIoCapacity << ".");
        return SER_INVALID_PARAM;
    }

    mIoContexts.reset(new (std::nothrow) MultiCastIoContext[mIoCapacity]);
    if (NN_UNLIKELY(mIoContexts == nullptr)) {
        NN_LOG_ERROR("Failed to allocate multicast IO contexts, count " << mIoCapacity << ".");
        return SER_NEW_OBJECT_FAILED;
    }

    for (uint16_t shardId = 0; shardId < mThreadCount; ++shardId) {
        IoShard &shard = mIoShards[shardId];
        shard.freeHead.store(MakeFreeHead(0, INVALID_CONTEXT_INDEX), std::memory_order_relaxed);
        shard.wheel.reset(new (std::nothrow) MultiCastIoContext *[IO_WHEEL_SIZE]());
        if (NN_UNLIKELY(shard.wheel == nullptr)) {
            NN_LOG_ERROR("Failed to allocate multicast timer wheel, shard " << shardId << ".");
            UnInitializeIoContexts();
            return SER_NEW_OBJECT_FAILED;
        }
        shard.lastTick = NetMonotonic::TimeMs() - 1;
    }

    try {
        for (uint32_t index = 0; index < mIoCapacity; ++index) {
            MultiCastIoContext &context = mIoContexts[index];
            context.Initialize(index, mMaxSubscriberNum);
            context.mShardId = static_cast<uint16_t>(index % mThreadCount);
            PushFreeContext(&context);
        }
    } catch (const std::bad_alloc &) {
        NN_LOG_ERROR("Failed to reserve multicast subscriber response contexts.");
        UnInitializeIoContexts();
        return SER_NEW_OBJECT_FAILED;
    }
    return SER_OK;
}

void MultiCastPeriodicManager::UnInitializeIoContexts()
{
    if (mIoContexts == nullptr) {
        return;
    }

    for (uint32_t index = 0; index < mIoCapacity; ++index) {
        MultiCastIoContext &context = mIoContexts[index];
        MultiCastIoState state = context.mState.load(std::memory_order_acquire);
        if (state == MultiCastIoState::FREE) {
            continue;
        }
        if (state == MultiCastIoState::PREPARING || state == MultiCastIoState::INIT) {
            MultiCastIoState expected = state;
            (void)context.mState.compare_exchange_strong(expected, MultiCastIoState::BROKEN, std::memory_order_acq_rel,
                                                         std::memory_order_acquire);
        }
        while (!TryCloseAccess(&context)) {
            usleep(NN_NO1);
        }
        if (context.mCallback != nullptr) {
            RunIoCallback(&context);
        }
        context.mPublisherCtx.Reset();
        if (context.mPublisher != nullptr) {
            context.mPublisher->DecreaseRef();
            context.mPublisher = nullptr;
        }
    }

    for (uint16_t shardId = 0; shardId < mThreadCount; ++shardId) {
        mIoShards[shardId].wheel.reset();
        mIoShards[shardId].addHead.store(nullptr, std::memory_order_relaxed);
        mIoShards[shardId].completeHead.store(nullptr, std::memory_order_relaxed);
    }
    mIoContexts.reset();
}

uint16_t MultiCastPeriodicManager::GetProducerShard()
{
    struct ThreadShard {
        const MultiCastPeriodicManager *manager = nullptr;
        uint16_t shardId = 0;
        uint16_t shardCount = 0;
    };
    static thread_local ThreadShard threadShard;
    if (threadShard.manager != this || threadShard.shardCount != mThreadCount) {
        threadShard.manager = this;
        threadShard.shardCount = mThreadCount;
        threadShard.shardId =
            static_cast<uint16_t>(mNextProducerShard.fetch_add(1, std::memory_order_relaxed) % mThreadCount);
    }
    return threadShard.shardId;
}

MultiCastIoContext *MultiCastPeriodicManager::PopFreeContext(uint16_t shardId)
{
    IoShard &shard = mIoShards[shardId];
    uint64_t head = shard.freeHead.load(std::memory_order_acquire);
    while (FreeHeadIndex(head) != INVALID_CONTEXT_INDEX) {
        MultiCastIoContext *context = &mIoContexts[FreeHeadIndex(head)];
        uint64_t next = MakeFreeHead(FreeHeadTag(head) + 1, context->mNextFree);
        if (shard.freeHead.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return context;
        }
    }
    return nullptr;
}

void MultiCastPeriodicManager::PushFreeContext(MultiCastIoContext *context)
{
    IoShard &shard = mIoShards[context->mShardId];
    uint64_t head = shard.freeHead.load(std::memory_order_relaxed);
    do {
        context->mNextFree = FreeHeadIndex(head);
    } while (!shard.freeHead.compare_exchange_weak(head, MakeFreeHead(FreeHeadTag(head) + 1, context->mIndex),
                                                   std::memory_order_release, std::memory_order_relaxed));
}

MultiCastIoContext *MultiCastPeriodicManager::AcquireIoContext(Publisher *publisher, const MultiCastCallback *callback,
                                                               int16_t timeout)
{
    uint16_t preferredShard = GetProducerShard();
    MultiCastIoContext *context = PopFreeContext(preferredShard);
    for (uint16_t offset = 1; context == nullptr && offset < mThreadCount; ++offset) {
        context = PopFreeContext(static_cast<uint16_t>((preferredShard + offset) % mThreadCount));
    }
    if (NN_UNLIKELY(context == nullptr)) {
        return nullptr;
    }

    context->mGeneration = (context->mGeneration + 1) & 0x3FU;
    HcomSeqNo seqNo(0);
    seqNo.SetValue(1, context->mGeneration, context->mIndex + 1);
    context->mSeqNo.store(seqNo.wholeSeq, std::memory_order_relaxed);
    context->mPublisher = publisher;
    context->mPublisher->IncreaseRef();
    context->mCallback = callback;
    context->mExpireTick = timeout < 0 ? 0 : NetMonotonic::TimeMs() + static_cast<uint64_t>(timeout) * 1000;
    context->mPublisherCtx.Reset();
    context->mSendReady.store(false, std::memory_order_relaxed);
    context->mAccess.store(1, std::memory_order_relaxed);
    context->mState.store(MultiCastIoState::PREPARING, std::memory_order_release);
    return context;
}

MultiCastIoContext *MultiCastPeriodicManager::GetIoContext(uint32_t seqNo)
{
    HcomSeqNo parsed(seqNo);
    if (NN_UNLIKELY(parsed.fromFlat == 0 || parsed.realSeq == 0 || parsed.realSeq > mIoCapacity)) {
        return nullptr;
    }

    parsed.isResp = 0;
    MultiCastIoContext *context = &mIoContexts[parsed.realSeq - 1];
    return context->TryAcquire(parsed.wholeSeq) ? context : nullptr;
}

void MultiCastPeriodicManager::SubmitIoContext(MultiCastIoContext *context)
{
    context->mState.store(MultiCastIoState::INIT, std::memory_order_release);
    PushIoAdd(context);
}

void MultiCastPeriodicManager::CompleteIoContext(MultiCastIoContext *context)
{
    PushIoComplete(context);
}

void MultiCastPeriodicManager::ReleaseIoContext(MultiCastIoContext *context)
{
    if (context == nullptr) {
        return;
    }
    DestroyCallback(context->mCallback);
    context->mCallback = nullptr;
    context->mPublisherCtx.Reset();
    if (context->mPublisher != nullptr) {
        context->mPublisher->DecreaseRef();
        context->mPublisher = nullptr;
    }
    context->mState.store(MultiCastIoState::FREE, std::memory_order_release);
    context->mAccess.store(ACCESS_CLOSED, std::memory_order_release);
    PushFreeContext(context);
}

void MultiCastPeriodicManager::PushIoAdd(MultiCastIoContext *context)
{
    IoShard &shard = mIoShards[context->mShardId];
    MultiCastIoContext *head = shard.addHead.load(std::memory_order_relaxed);
    do {
        context->mAddNext = head;
    } while (!shard.addHead.compare_exchange_weak(head, context, std::memory_order_release, std::memory_order_relaxed));
}

void MultiCastPeriodicManager::PushIoComplete(MultiCastIoContext *context)
{
    IoShard &shard = mIoShards[context->mShardId];
    MultiCastIoContext *head = shard.completeHead.load(std::memory_order_relaxed);
    do {
        context->mCompleteNext = head;
    } while (
        !shard.completeHead.compare_exchange_weak(head, context, std::memory_order_release, std::memory_order_relaxed));
}

void MultiCastPeriodicManager::ProcessCleanUp(uint16_t tId)
{
    if (NN_UNLIKELY(tId >= M_MAX_THREAD_NUM)) {
        NN_LOG_WARN("Thread id is invalid, id " << tId << ".");
        return;
    }

    mHandleQueue[tId].clear();
    for (uint32_t i = NN_NO0; i < M_MAX_BATCH_NUM; i++) {
        QueueManager &manager = mQueue[tId][i];
        manager.lock.Lock();
        while (manager.head != nullptr) {
            MultiCastServiceTimer *timer = manager.head;
            DetachTimer(manager, timer);
            mHandleQueue[tId].emplace_back(timer);
        }
        manager.lock.Unlock();
    }

    for (auto *timer : mHandleQueue[tId]) {
        ProcessTimer(timer);
    }
}

void MultiCastPeriodicManager::FillHandleQueue(uint16_t tId)
{
    mHandleQueue[tId].clear();

    for (uint32_t i = NN_NO0; i < M_MAX_BATCH_NUM; i++) {
        QueueManager &manager = mQueue[tId][i];
        manager.lock.Lock();
        MultiCastServiceTimer *timer = manager.head;
        while (timer != nullptr) {
            MultiCastServiceTimer *next = timer->mPeriodicNext;
            if (timer->IsFinished() || timer->IsTimeOut()) {
                DetachTimer(manager, timer);
                mHandleQueue[tId].emplace_back(timer);
            }
            timer = next;
        }
        manager.lock.Unlock();
    }
}

void MultiCastPeriodicManager::ProcessTimeOut(uint16_t tId)
{
    if (tId >= M_MAX_THREAD_NUM) {
        NN_LOG_WARN("tId is invalid, tid:" << tId);
        return;
    }
    FillHandleQueue(tId);

    for (auto *timer : mHandleQueue[tId]) {
        ProcessTimer(timer);
    }
}

void MultiCastPeriodicManager::ProcessTimer(MultiCastServiceTimer *timer)
{
    if (timer->EraseSeqNoWithRet()) {
        timer->TimeoutDump();
        timer->MarkTimeout();
        auto *callback = reinterpret_cast<MultiCastCallback *>(timer->Callback());
        ProcessBrokenTimer(timer, callback);
    }

    timer->DecreaseRef();
}

void MultiCastPeriodicManager::ProcessBrokenTimer(MultiCastServiceTimer *timer, MultiCastCallback *callback)
{
    PublisherContext pubCtx;
    if (callback != nullptr) {
        timer->RunCallBack(pubCtx);
    }
    timer->DecreaseRef();
}

void MultiCastPeriodicManager::EnqueueTimer(MultiCastServiceTimer *timer)
{
    QueueManager &manager = mQueue[timer->mPeriodicThreadId][timer->mPeriodicQueueId];

    manager.lock.Lock();
    timer->mPeriodicPrev = nullptr;
    timer->mPeriodicNext = manager.head;
    if (manager.head != nullptr) {
        manager.head->mPeriodicPrev = timer;
    }
    manager.head = timer;
    timer->mInPeriodicQueue = true;
    manager.lock.Unlock();
}

void MultiCastPeriodicManager::DetachTimer(QueueManager &manager, MultiCastServiceTimer *timer)
{
    if (timer->mPeriodicPrev == nullptr) {
        manager.head = timer->mPeriodicNext;
    } else {
        timer->mPeriodicPrev->mPeriodicNext = timer->mPeriodicNext;
    }
    if (timer->mPeriodicNext != nullptr) {
        timer->mPeriodicNext->mPeriodicPrev = timer->mPeriodicPrev;
    }
    timer->mPeriodicPrev = nullptr;
    timer->mPeriodicNext = nullptr;
    timer->mInPeriodicQueue = false;
}

void MultiCastPeriodicManager::InsertWheel(MultiCastIoContext *context, uint64_t expireTick)
{
    IoShard &shard = mIoShards[context->mShardId];
    uint32_t slot = static_cast<uint32_t>(expireTick & IO_WHEEL_MASK);
    context->mWheelSlot = slot;
    context->mWheelPrev = nullptr;
    context->mWheelNext = shard.wheel[slot];
    if (shard.wheel[slot] != nullptr) {
        shard.wheel[slot]->mWheelPrev = context;
    }
    shard.wheel[slot] = context;
    context->mInWheel = true;
}

void MultiCastPeriodicManager::RemoveWheel(MultiCastIoContext *context)
{
    if (!context->mInWheel) {
        return;
    }

    IoShard &shard = mIoShards[context->mShardId];
    if (context->mWheelPrev == nullptr) {
        shard.wheel[context->mWheelSlot] = context->mWheelNext;
    } else {
        context->mWheelPrev->mWheelNext = context->mWheelNext;
    }
    if (context->mWheelNext != nullptr) {
        context->mWheelNext->mWheelPrev = context->mWheelPrev;
    }
    context->mWheelPrev = nullptr;
    context->mWheelNext = nullptr;
    context->mInWheel = false;
}

bool MultiCastPeriodicManager::TryCloseAccess(MultiCastIoContext *context)
{
    uint32_t access = context->mAccess.load(std::memory_order_acquire);
    if (access == ACCESS_CLOSED) {
        return true;
    }
    if (access != 0) {
        return false;
    }
    return context->mAccess.compare_exchange_strong(access, ACCESS_CLOSED, std::memory_order_acq_rel,
                                                    std::memory_order_acquire);
}

void MultiCastPeriodicManager::RunIoCallback(MultiCastIoContext *context)
{
    MultiCastIoState state = context->mState.load(std::memory_order_acquire);
    SubscriberRspStatus status = state == MultiCastIoState::TIMEOUT ? SubscriberRspStatus::TIMEOUT :
                                                                      SubscriberRspStatus::BROKEN;
    for (auto &response : context->mPublisherCtx.subscriberRspList) {
        SubscriberRspStatus expected = SubscriberRspStatus::INIT;
        if (response.GetStatus() != SubscriberRspStatus::SUCCESS) {
            __atomic_compare_exchange_n(&response.mStatus, &expected, status, false, __ATOMIC_RELEASE,
                                        __ATOMIC_RELAXED);
        }
    }

    const MultiCastCallback *callback = context->mCallback;
    context->mCallback = nullptr;
    if (callback != nullptr) {
        const_cast<MultiCastCallback *>(callback)->Run(context->mPublisherCtx);
    }
}

void MultiCastPeriodicManager::FinalizeIoContext(MultiCastIoContext *context)
{
    if (!TryCloseAccess(context)) {
        InsertWheel(context, NetMonotonic::TimeMs() + 1);
        return;
    }

    MultiCastIoState state = context->mState.load(std::memory_order_acquire);
    if (state == MultiCastIoState::TIMEOUT || state == MultiCastIoState::BROKEN) {
        RunIoCallback(context);
    } else if (context->mCallback != nullptr) {
        DestroyCallback(context->mCallback);
        context->mCallback = nullptr;
    }

    context->mPublisherCtx.Reset();
    if (context->mPublisher != nullptr) {
        context->mPublisher->DecreaseRef();
        context->mPublisher = nullptr;
    }
    context->mState.store(MultiCastIoState::FREE, std::memory_order_release);
    PushFreeContext(context);
}

void MultiCastPeriodicManager::ProcessIoContext(MultiCastIoContext *context, uint64_t currentTick)
{
    MultiCastIoState state = context->mState.load(std::memory_order_acquire);
    if (state == MultiCastIoState::INIT) {
        if (!context->mSendReady.load(std::memory_order_acquire)) {
            InsertWheel(context, currentTick + 1);
            return;
        }
        if (context->mExpireTick == 0) {
            InsertWheel(context, currentTick + IO_NEVER_TIMEOUT_CHECK_MS);
            return;
        }
        if (currentTick < context->mExpireTick) {
            uint64_t nextTick = context->mExpireTick;
            if (context->mExpireTick - currentTick >= IO_WHEEL_SIZE) {
                nextTick = currentTick + IO_WHEEL_SIZE - 1;
            }
            InsertWheel(context, nextTick);
            return;
        }
        if (!TryCloseAccess(context)) {
            InsertWheel(context, currentTick + 1);
            return;
        }

        MultiCastIoState expected = MultiCastIoState::INIT;
        if (!context->mState.compare_exchange_strong(expected, MultiCastIoState::TIMEOUT, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
            state = expected;
        } else {
            state = MultiCastIoState::TIMEOUT;
            NN_LOG_WARN("Multicast request timeout, seq no:" << context->SeqNo() << ", subscriber count:"
                                                             << context->mPublisherCtx.subscriberRspList.size() << ".");
        }
    }

    if (state != MultiCastIoState::FREE) {
        FinalizeIoContext(context);
    }
}

void MultiCastPeriodicManager::DrainIoAdd(uint16_t tId, uint64_t currentTick)
{
    IoShard &shard = mIoShards[tId];
    MultiCastIoContext *list = shard.addHead.exchange(nullptr, std::memory_order_acquire);
    MultiCastIoContext *reversed = nullptr;
    while (list != nullptr) {
        MultiCastIoContext *next = list->mAddNext;
        list->mAddNext = reversed;
        reversed = list;
        list = next;
    }

    while (reversed != nullptr) {
        MultiCastIoContext *next = reversed->mAddNext;
        reversed->mAddNext = nullptr;
        ProcessIoContext(reversed, currentTick);
        reversed = next;
    }
}

void MultiCastPeriodicManager::DrainIoComplete(uint16_t tId, uint64_t currentTick)
{
    IoShard &shard = mIoShards[tId];
    MultiCastIoContext *list = shard.completeHead.exchange(nullptr, std::memory_order_acquire);
    while (list != nullptr) {
        MultiCastIoContext *next = list->mCompleteNext;
        list->mCompleteNext = nullptr;
        if (list->mInWheel) {
            RemoveWheel(list);
            ProcessIoContext(list, currentTick);
        }
        list = next;
    }
}

void MultiCastPeriodicManager::ProcessWheelSlot(uint16_t tId, uint64_t currentTick)
{
    IoShard &shard = mIoShards[tId];
    uint32_t slot = static_cast<uint32_t>(currentTick & IO_WHEEL_MASK);
    MultiCastIoContext *context = shard.wheel[slot];
    shard.wheel[slot] = nullptr;

    while (context != nullptr) {
        MultiCastIoContext *next = context->mWheelNext;
        context->mWheelPrev = nullptr;
        context->mWheelNext = nullptr;
        context->mInWheel = false;
        ProcessIoContext(context, currentTick);
        context = next;
    }
}

void MultiCastPeriodicManager::ProcessIoShard(uint16_t tId, uint64_t currentTick)
{
    DrainIoAdd(tId, currentTick);
    DrainIoComplete(tId, currentTick);
    IoShard &shard = mIoShards[tId];
    while (shard.lastTick < currentTick) {
        ProcessWheelSlot(tId, ++shard.lastTick);
    }
}

void MultiCastPeriodicManager::ProcessIoInBroken(Publisher *publisher)
{
    for (uint32_t index = 0; index < mIoCapacity; ++index) {
        MultiCastIoContext *context = &mIoContexts[index];
        MultiCastIoState state = context->mState.load(std::memory_order_acquire);
        if (state != MultiCastIoState::INIT || !context->TryAcquire(context->SeqNo())) {
            continue;
        }
        if (context->mPublisher != publisher) {
            context->ReleaseAccess();
            continue;
        }
        MultiCastIoState expected = MultiCastIoState::INIT;
        if (context->mState.compare_exchange_strong(expected, MultiCastIoState::BROKEN, std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            PushIoComplete(context);
        }
        context->ReleaseAccess();
    }
}

SerResult MultiCastPeriodicManager::AddTimerCheck(MultiCastServiceTimer *&timer)
{
    if (NN_UNLIKELY(timer == nullptr)) {
        NN_LOG_ERROR("Failed to add timeout, timer is null");
        return SER_INVALID_PARAM;
    }

    if (NN_UNLIKELY(mNeedStop)) {
        NN_LOG_ERROR("Failed to add timeout seq no " << timer->SeqNo() << " by stop service");
        return SER_STOP;
    }

    if (NN_UNLIKELY(timer->SeqNo() == 0 || timer->Callback() == 0)) {
        NN_LOG_ERROR("Add timeout invalid seq no " << timer->SeqNo() << " or callback " << timer->Callback());
        return SER_INVALID_PARAM;
    }
    return SER_OK;
}

void MultiCastPeriodicManager::RunInThread(int16_t tId)
{
    mHandleQueue[tId].reserve(NN_NO8192);

    if (tId >= mThreadCount) {
        NN_LOG_WARN("Invalid thread id " << tId << " to run periodic manager.");
        mWorkerStartFailed.store(true, std::memory_order_release);
        return;
    }

    int eFd = epoll_create(1);
    if (eFd < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("MultiCastPeriodicManager manager failed to create epoll by "
                     << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        mWorkerStartFailed.store(true, std::memory_order_release);
        return;
    }

    mStartedWorkingThreads.fetch_add(1, std::memory_order_release);
    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread start");
    uint64_t lastLegacyCheck = NetMonotonic::TimeMs();
    while (!mNeedStop.load(std::memory_order_acquire)) {
        uint64_t currentTick = NetMonotonic::TimeMs();
        ProcessIoShard(static_cast<uint16_t>(tId), currentTick);
        if (currentTick - lastLegacyCheck >= gMaxTimeout) {
            ProcessTimeOut(static_cast<uint16_t>(tId));
            lastLegacyCheck = currentTick;
        }
        struct epoll_event ev {
        };
        epoll_wait(eFd, &ev, NN_NO1, NN_NO1);
    }

    NetFunc::NN_SafeCloseFd(eFd);
    NN_LOG_INFO("PeriodicManager for timeout [name: " << mName << ", index: " << tId << "] working thread exit");
}
} // namespace hcom
} // namespace ock
