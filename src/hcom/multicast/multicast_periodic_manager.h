/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PERIODIC_MANAGER_H
#define HCOM_MULTICAST_PERIODIC_MANAGER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hcom_obj_statistics.h"
#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_publisher_service.h"
#include "multicast/include/multicast_service_callback.h"
#include "service_periodic_manager.h"

namespace ock {
namespace hcom {
class Publisher;
class MultiCastPeriodicManager;
using MultiCastPeriodicManagerPtr = NetRef<MultiCastPeriodicManager>;
class MultiCastPeriodicManager {
private:
    struct QueueManager;

public:
    MultiCastPeriodicManager(uint16_t threadCount, std::string name, int cpuId, uint32_t ioCapacity,
                             uint32_t maxSubscriberNum)
        : mThreadCount(threadCount),
          mCpuId(cpuId),
          mIoCapacity(ioCapacity),
          mMaxSubscriberNum(maxSubscriberNum),
          mName(std::move(name))
    {
        OBJ_GC_INCREASE(MultiCastPeriodicManager);
    }

    ~MultiCastPeriodicManager()
    {
        Stop();
        OBJ_GC_DECREASE(MultiCastPeriodicManager);
    }

    /*
     * @brief Add the cb for timeout with seqNo
     */
    inline SerResult AddTimer(MultiCastServiceTimer *&timer)
    {
        auto ret = AddTimerCheck(timer);
        if (ret != SER_OK) {
            return ret;
        }

        timer->IncreaseRef();
        PrepareTimerQueue(timer);
        EnqueueTimer(timer);
        return SER_OK;
    }

    MultiCastIoContext *AcquireIoContext(Publisher *publisher, const MultiCastCallback *callback, int16_t timeout);
    MultiCastIoContext *GetIoContext(uint32_t seqNo);
    void SubmitIoContext(MultiCastIoContext *context);
    void CompleteIoContext(MultiCastIoContext *context);
    void ReleaseIoContext(MultiCastIoContext *context);
    void ProcessIoInBroken(Publisher *publisher);
    SerResult Start();
    void Stop();

private:
    void StopInner();
    void RunInThread(int16_t tId);
    void ProcessTimeOut(uint16_t tId);
    void ProcessCleanUp(uint16_t tId);
    SerResult InitializeIoContexts();
    void UnInitializeIoContexts();
    void ProcessIoShard(uint16_t tId, uint64_t currentTick);
    void DrainIoAdd(uint16_t tId, uint64_t currentTick);
    void DrainIoComplete(uint16_t tId, uint64_t currentTick);
    void ProcessWheelSlot(uint16_t tId, uint64_t currentTick);
    void ProcessIoContext(MultiCastIoContext *context, uint64_t currentTick);
    void FinalizeIoContext(MultiCastIoContext *context);
    void InsertWheel(MultiCastIoContext *context, uint64_t expireTick);
    void RemoveWheel(MultiCastIoContext *context);
    bool TryCloseAccess(MultiCastIoContext *context);
    void RunIoCallback(MultiCastIoContext *context);
    void PushIoAdd(MultiCastIoContext *context);
    void PushIoComplete(MultiCastIoContext *context);
    MultiCastIoContext *PopFreeContext(uint16_t shardId);
    void PushFreeContext(MultiCastIoContext *context);
    uint16_t GetProducerShard();
    void ProcessTimer(MultiCastServiceTimer *timer);
    void ProcessBrokenTimer(MultiCastServiceTimer *timer, MultiCastCallback *callback);
    SerResult AddTimerCheck(MultiCastServiceTimer *&timer);
    void FillHandleQueue(uint16_t tId);
    inline void PrepareTimerQueue(MultiCastServiceTimer *timer)
    {
        timer->mPeriodicThreadId = static_cast<uint16_t>(timer->SeqNo() % mThreadCount);
        timer->mPeriodicQueueId = static_cast<uint16_t>((timer->SeqNo() / mThreadCount) % M_MAX_BATCH_NUM);
        timer->mPeriodicPrev = nullptr;
        timer->mPeriodicNext = nullptr;
        timer->mInPeriodicQueue = false;
    }
    void EnqueueTimer(MultiCastServiceTimer *timer);
    void DetachTimer(QueueManager &manager, MultiCastServiceTimer *timer);
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    static constexpr uint64_t gMaxTimeout = 500L;
    static constexpr uint32_t IO_WHEEL_SIZE = 65536;
    static constexpr uint32_t IO_WHEEL_MASK = IO_WHEEL_SIZE - 1;
    static constexpr uint64_t IO_NEVER_TIMEOUT_CHECK_MS = 1000;
    static constexpr uint32_t INVALID_CONTEXT_INDEX = UINT32_MAX;
    static constexpr uint32_t ACCESS_CLOSED = 1U << 31;

    struct QueueManager {
        NetSpinLock lock;
        MultiCastServiceTimer *head = nullptr;
        QueueManager() = default;
    };

    struct IoShard {
        alignas(MULTICAST_CACHE_LINE_SIZE) std::atomic<MultiCastIoContext *> addHead = {nullptr};
        alignas(MULTICAST_CACHE_LINE_SIZE) std::atomic<MultiCastIoContext *> completeHead = {nullptr};
        alignas(MULTICAST_CACHE_LINE_SIZE) std::atomic<uint64_t> freeHead = {0};
        alignas(MULTICAST_CACHE_LINE_SIZE) std::unique_ptr<MultiCastIoContext *[]> wheel;
        uint64_t lastTick = 0;
    };

private:
    QueueManager mQueue[M_MAX_THREAD_NUM][M_MAX_BATCH_NUM];
    std::vector<MultiCastServiceTimer *> mHandleQueue[M_MAX_THREAD_NUM];

    std::thread mWorkingThreads[M_MAX_THREAD_NUM];
    std::atomic<int16_t> mStartedWorkingThreads = {0};
    std::atomic<bool> mWorkerStartFailed = {false};
    std::atomic<uint32_t> mNextProducerShard = {0};

    std::mutex mMutex;
    bool mStarted = false;
    std::atomic<bool> mNeedStop = {true};

    uint16_t mThreadCount = 1;
    int mCpuId = -1;
    uint32_t mIoCapacity = 0;
    uint32_t mMaxSubscriberNum = 0;

    std::string mName;
    std::unique_ptr<MultiCastIoContext[]> mIoContexts;
    IoShard mIoShards[M_MAX_THREAD_NUM];

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
} // namespace hcom
} // namespace ock

#endif // HCOM_MULTICAST_PERIODIC_MANAGER_H
