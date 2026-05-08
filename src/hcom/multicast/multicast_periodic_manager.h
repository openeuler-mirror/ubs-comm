/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PERIODIC_MANAGER_H
#define HCOM_MULTICAST_PERIODIC_MANAGER_H

#include <thread>
#include <utility>
#include <vector>
#include <queue>
#include <list>

#include "multicast/include/multicast_publisher_service.h"
#include "service_periodic_manager.h"
#include "hcom_obj_statistics.h"
#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_service_callback.h"

namespace ock {
namespace hcom {
class Publisher;
class MultiCastPeriodicManager;
using MultiCastPeriodicManagerPtr = NetRef<MultiCastPeriodicManager>;
class MultiCastPeriodicManager {
public:
    MultiCastPeriodicManager(uint16_t threadCount, std::string name, int cpuId)
        : mThreadCount(threadCount), mName(std::move(name)), mCpuId(cpuId)
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
        uint32_t tId = timer->SeqNo() % mThreadCount;
        uint32_t index = mQueue[tId].NextIndex();

        AddTimerToList(timer);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[index]);
        mQueue[tId].queue[index].push_back(timer);
        return SER_OK;
    }

    SerResult Start();
    void Stop();

private:
    inline void AddTimerToList(MultiCastServiceTimer *timer)
    {
        if (!ValidateTimer(timer)) {
            return;
        }
        auto header = reinterpret_cast<MultiCastTimerListHeader *>(timer->mPublisher->mTimerList);
        header->AddTimerCtx(timer);
    }

    inline void RemoveTimerFromList(MultiCastServiceTimer *timer)
    {
        if (!ValidateTimer(timer)) {
            return;
        }
        auto header = reinterpret_cast<MultiCastTimerListHeader *>(timer->mPublisher->mTimerList);
        header->RemoveTimerCtx(timer);
    }
    void StopInner();
    void RunInThread(int16_t tId);
    void ProcessTimeOut(uint16_t tId);
    void ProcessCleanUp(uint16_t tId);
    void UpdateSubscriberRsp(PublisherContext *pubCtx) const;
    SerResult AddTimerCheck(MultiCastServiceTimer *&timer);
    bool ValidateTimer(MultiCastServiceTimer *&timer);
    void FillHandleQueue(uint16_t tId);
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    static constexpr uint64_t gMaxTimeout = 500L;
    struct QueueManager {
        std::mutex lock[M_MAX_BATCH_NUM];
        std::list<MultiCastServiceTimer *> queue[M_MAX_BATCH_NUM];
        uint32_t nextIndex = 0;
        QueueManager() = default;

        inline uint32_t NextIndex()
        {
            return __sync_fetch_and_add(&nextIndex, NN_NO1) % M_MAX_BATCH_NUM;
        }
    };

private:
    QueueManager mQueue[M_MAX_THREAD_NUM];
    std::vector<MultiCastServiceTimer *> mHandleQueue[M_MAX_THREAD_NUM];

    std::thread mWorkingThreads[M_MAX_THREAD_NUM];
    std::atomic<int16_t> mStartedWorkingThreads = { 0 };

    std::mutex mMutex;
    bool mStarted = false;
    bool mNeedStop = true;

    uint16_t mThreadCount = 1;
    int mCpuId = -1;

    std::string mName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // HCOM_MULTICAST_PERIODIC_MANAGER_H
