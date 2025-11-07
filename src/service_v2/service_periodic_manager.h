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
#ifndef HCOM_SERVICE_V2_SERVICE_PERIODIC_MANAGER_H_
#define HCOM_SERVICE_V2_SERVICE_PERIODIC_MANAGER_H_
#include <thread>
#include <vector>
#include <queue>

#include "service_common.h"
#include "hcom_utils.h"
#include "service_callback.h"

namespace ock {
namespace hcom {

const int M_MAX_THREAD_NUM = 4;
const int M_MAX_BATCH_NUM = 16;

class HcomPeriodicManager {
public:
    HcomPeriodicManager(uint16_t threadCount, const std::string &name) : mThreadCount(threadCount), mName(name)
    {
        OBJ_GC_INCREASE(HcomPeriodicManager);
    }

    ~HcomPeriodicManager()
    {
        Stop();
        OBJ_GC_DECREASE(HcomPeriodicManager);
    }

#define VALIDATE(timer)                                                                                                \
    do {                                                                                                               \
        if (NN_UNLIKELY((timer) == nullptr)) {                                                                         \
            NN_LOG_ERROR("Failed to add timeout, because timer is null");                                              \
            return SER_INVALID_PARAM;                                                                                  \
        }                                                                                                              \
                                                                                                                       \
        if (NN_UNLIKELY(mNeedStop)) {                                                                                  \
            NN_LOG_ERROR("Failed to add timeout seq no " << (timer)->SeqNo() << " because stop service");              \
            return SER_STOP;                                                                                           \
        }                                                                                                              \
                                                                                                                       \
        if (NN_UNLIKELY((timer)->SeqNo() == 0 || (timer)->Callback() == 0)) {                                          \
            NN_LOG_ERROR("Add timeout invalid seq no " << (timer)->SeqNo() << " or callback " << (timer)->Callback()); \
            return SER_INVALID_PARAM;                                                                                  \
        }                                                                                                              \
    } while (false)

    /*
     * @brief Add the cb for timeout with seqNo
     */
    inline SerResult AddTimer(HcomServiceTimer *&timer)
    {
        VALIDATE(timer);
        uint32_t tId = timer->SeqNo() % mThreadCount;
        uint32_t index = mQueue[tId].NextIndex();

        AddLinkedList(timer);
        std::lock_guard<std::mutex> guard(mQueue[tId].lock[index]);
        mQueue[tId].queue[index].push(timer);
        return SER_OK;
    }

    SerResult Start();
    void Stop();

private:
    inline void AddLinkedList(HcomServiceTimer *timer)
    {
        if (NN_UNLIKELY(timer == nullptr || timer->mType != HcomAsyncCBType::CBS_IO)) {
            return;
        }

        if (NN_UNLIKELY(timer->mChannel == nullptr || timer->mChannel->GetTimerList() == 0)) {
            return;
        }

        auto header = reinterpret_cast<SerTimerListHeader *>(timer->mChannel->GetTimerList());
        header->AddTimerCtx(timer);
    }

    inline void RemoveLinkedList(HcomServiceTimer *timer)
    {
        if (NN_UNLIKELY(timer == nullptr || timer->mType != HcomAsyncCBType::CBS_IO)) {
            return;
        }

        if (NN_UNLIKELY(timer->mChannel == nullptr || timer->mChannel->GetTimerList() == 0)) {
            return;
        }

        auto header = reinterpret_cast<SerTimerListHeader *>(timer->mChannel->GetTimerList());
        header->RemoveTimerCtx(timer);
    }
    void StopInner();
    void RunInThread(int16_t tId);
    void ProcessTimeOut(uint16_t tId);
    void ProcessCleanUp(uint16_t tId);
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    static constexpr uint64_t gMaxTimeout = 500L;

    struct QueueManager {
        std::mutex lock[M_MAX_BATCH_NUM];
        std::priority_queue<HcomServiceTimer *, std::vector<HcomServiceTimer *>, HcomServiceTimerCompare>
            queue[M_MAX_BATCH_NUM];
        uint32_t nextIndex = 0;
        QueueManager() = default;

        inline uint32_t NextIndex()
        {
            return __sync_fetch_and_add(&nextIndex, 1) % M_MAX_BATCH_NUM;
        }
    };

private:
    QueueManager mQueue[M_MAX_THREAD_NUM];
    std::vector<HcomServiceTimer *> mHandleQueue[M_MAX_THREAD_NUM];

    std::thread mWorkingThreads[M_MAX_THREAD_NUM];
    std::atomic<int16_t> mStartedWorkingThreads = { 0 };
    uint16_t mThreadCount = 1;

    std::mutex mMutex;
    bool mStarted = false;
    bool mNeedStop = true;

    std::string mName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}
#endif // HCOM_SERVICE_V2_SERVICE_PERIODIC_MANAGER_H_