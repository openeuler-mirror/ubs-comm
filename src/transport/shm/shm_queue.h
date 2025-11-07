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
#ifndef OCK_HCOM_SHM_WRAPPER_H
#define OCK_HCOM_SHM_WRAPPER_H

#include "net_monotonic.h"
#include "shm_common.h"
#include "shm_handle.h"

namespace ock {
namespace hcom {
struct ShmQueueHeadTail {
    volatile uint32_t head = 0;
    volatile uint32_t tail = 0;
};

struct ShmQueueMeta {
    uint32_t capacity = 0; /* capacity of the queue */
    uint32_t mask = 0;     /* mask of queue */

    ShmQueueHeadTail prod {}; /* producer info */
    ShmQueueHeadTail cons {}; /* consumer info */

    sem_t sem {}; /* sem info */

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "capacity " << capacity << ", mask " << mask << ", prod: " << prod.head << "-" << prod.tail <<
            ", cons: " << cons.head << "-" << cons.tail;
        return oss.str();
    }
};

template <typename T> class ShmQueue {
public:
    static const HResult SHM_QUEUE_FULL = -1;
    static const HResult SHM_QUEUE_EMPTY = -2;
    static const HResult SHM_QUEUE_NOT_INIT = -3;

public:
    ShmQueue(const std::string &name, uint32_t capacity, const ShmHandlePtr &shmHandle)
        : mShmHandle(shmHandle.Get()), mName(name), mCapacity(capacity)
    {
        OBJ_GC_INCREASE(ShmQueue);
    }

    ~ShmQueue()
    {
        UnInitialize();
        OBJ_GC_DECREASE(ShmQueue);
    }

    static inline uint32_t MemSize(uint32_t capacity)
    {
        auto tmp = capacity;
        if (!POWER_OF_2(capacity)) {
            tmp = NN_NextPower2(capacity);
        }

        return sizeof(ShmQueueMeta) + sizeof(T) * tmp;
    }

    /*
     * @brief Initialize
     */
    HResult Initialize()
    {
        if (mInited) {
            return SH_OK;
        }

        if (mShmHandle.Get() == nullptr || mShmHandle->Initialize() != NN_OK || mCapacity == 0) {
            NN_LOG_ERROR("Failed to initialize shm queue " << mName);
            return SH_PARAM_INVALID;
        }

        /* check if capacity is power of 2 */
        if (!POWER_OF_2(mCapacity)) {
            mCapacity = NN_NextPower2(mCapacity);
        }

        if (mShmHandle->DataSize() != MemSize(mCapacity)) {
            NN_LOG_ERROR("Failed to initialize shm queue " << mName << " as size not matched, " <<
                mShmHandle->DataSize() << "!=" << MemSize(mCapacity));
            return SH_PARAM_INVALID;
        }

        /*
         * for example capacity is 4 [100], then mask is 3 [011]
         * for tail/head fast reverse
         */
        mMask = mCapacity - 1;

        mQueueMeta = reinterpret_cast<ShmQueueMeta *>(mShmHandle->ShmAddress());

        NN_LOG_TRACE_INFO("shm mem base info, sizeof(ShmQueueMeta) " << sizeof(ShmQueueMeta) << ", meta " <<
            mQueueMeta->ToString());

        if (mShmHandle->IsOwner()) {
            /* set meta */
            bzero(reinterpret_cast<void *>(mShmHandle->ShmAddress()), mShmHandle->DataSize());
            mQueueMeta->capacity = mCapacity;
            mQueueMeta->mask = mMask;
            mQueueMeta->prod.head = 0;
            mQueueMeta->prod.tail = 0;
            mQueueMeta->cons.head = 0;
            mQueueMeta->cons.tail = 0;

            auto result = sem_init(&mQueueMeta->sem, 1, 0);
            if (result != 0) {
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed initialize shm sem for queue " << mName << ", error "
                        << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                return result;
            }
        } else {
            /* get capacity and mask */
            mCapacity = mQueueMeta->capacity;
            mMask = mQueueMeta->mask;
        }

        mQueueData = reinterpret_cast<T *>(mShmHandle->ShmAddress() + sizeof(ShmQueueMeta));
        mMaxFailedTime = static_cast<uint32_t>(NetFunc::NN_GetLongEnv("HCOM_SHM_MAX_ENQUEUE_STUCK_TIME",
            NN_NO1, NN_NO7200, NN_NO10));
        mMaxEnqueueTimeout = static_cast<uint32_t>(NetFunc::NN_GetLongEnv("HCOM_SHM_ENQUEUE_TIMEOUT",
            NN_NO1, NN_NO7200, NN_NO20));
        NN_LOG_DEBUG("SHM: mMaxFailedTime " << mMaxFailedTime << ", mMaxEnqueueTimeout " << mMaxEnqueueTimeout);
        mInited = true;
        return SH_OK;
    }

    void UnInitialize()
    {
        if (!mInited) {
            return;
        }

        sem_destroy(&mQueueMeta->sem);
        mQueueMeta = nullptr;
        mQueueData = nullptr;
        mCapacity = 0;
        mMask = 0;
        mInited = false;
        mShmHandle.Set(nullptr);
    }

    inline HResult Enqueue(T &item)
    {
        if (NN_UNLIKELY(!mInited)) {
            return SHM_QUEUE_NOT_INIT;
        }

        uint32_t oldHead = 0;
        uint32_t newHead = 0;

        uint32_t remainCapacity = 0;
        /* request space, if no space return error */
        if (NN_UNLIKELY(RequestProduceSpace(oldHead, newHead, 1, remainCapacity) == 0)) {
            return SHM_QUEUE_FULL;
        }

        /* write item to the right place */
        EnqueueItem(oldHead, item);
        /* update tail */
        if (NN_UNLIKELY(!UpdateProdTail(true, oldHead, newHead))) {
            return SHM_QUEUE_FULL;
        }

        return SH_OK;
    }

    inline HResult Dequeue(T &item)
    {
        if (NN_UNLIKELY(!mInited)) {
            return SHM_QUEUE_NOT_INIT;
        }

        uint32_t oldHead = 0;
        uint32_t newHead = 0;

        uint32_t dequeueSize = 0;
        /* request 1 item for dequeue */
        if (RequestDequeue(oldHead, newHead, 1, dequeueSize) == 0) {
            return SHM_QUEUE_EMPTY;
        }

        /* if will consume the failed one, jump to the next. Since low probability to happen, handle it once */
        if (oldHead == mFailedProd) {
            NN_LOG_WARN("Skip the failed prod " << mFailedProd);
            /* update the tail */
            UpdateConsTail(false, oldHead, newHead);
            /* request 1 item for dequeue */
            if (RequestDequeue(oldHead, newHead, 1, dequeueSize) == 0) {
                return SHM_QUEUE_EMPTY;
            }
        }

        /* read the item from right place */
        DequeueItem(oldHead, item);

        /* update the tail */
        UpdateConsTail(false, oldHead, newHead);

        return SH_OK;
    }

    inline HResult EnqueueAndNotify(T &item)
    {
        auto result = Enqueue(item);
        if (NN_UNLIKELY(result != SH_OK)) {
            return result;
        }

        return sem_post(&mQueueMeta->sem);
    }

    inline void LocalStopAndNotify()
    {
        mStop = true;
        sem_post(&mQueueMeta->sem);
    }

    inline bool CompTime(const struct timespec &a, const struct timespec &b)
    {
        if (a.tv_sec != b.tv_sec) {
            return (a.tv_sec > b.tv_sec);
        }
        return (a.tv_nsec > b.tv_nsec);
    }

    inline void CheckAndMarkProducerState()
    {
        if (mQueueMeta->prod.head > mQueueMeta->prod.tail) {
            if (mTempProdIdx != mQueueMeta->prod.tail) {
                mTempProdIdx = mQueueMeta->prod.tail;
                struct timespec timeOutTime = MONOTONIC_TIME();
                timeOutTime.tv_sec += static_cast<time_t>(mMaxFailedTime);
                mFailedTime = timeOutTime;
                return;
            }
        } else {
            mTempProdIdx = UINT64_MAX;
            return;
        }

        struct timespec nowTime = MONOTONIC_TIME();
        if (mTempProdIdx != UINT64_MAX && CompTime(nowTime, mFailedTime)) {
            mFailedProd = mTempProdIdx;
            mTempProdIdx = UINT64_MAX;
            mQueueMeta->prod.tail++;
            NN_LOG_WARN("Dectected enqueue stuck, skip idx: " << mFailedProd);
        }
    }

    inline HResult DequeueOrWait(T &item, bool &stopping, int32_t timeoutInMs)
    {
        auto start = NetMonotonic::TimeMs();
        while (true) {
            /* stopping */
            if (NN_UNLIKELY(mStop)) {
                stopping = true;
                return SH_OK;
            }
            // check if any producer stuck in enqueue. If stuck, kick it out
            CheckAndMarkProducerState();

            auto pollTime = NetMonotonic::TimeMs() - start;
            if (timeoutInMs >= 0 && pollTime >= static_cast<uint32_t>(timeoutInMs)) {
                return SH_TIME_OUT;
            }

            struct timespec semTimeout {};
            if (timeoutInMs < 0) {
                // set 0 means never timeout
                semTimeout.tv_sec = 0;
                semTimeout.tv_nsec = 0;
            } else {
                clock_gettime(CLOCK_REALTIME, &semTimeout);
                semTimeout.tv_nsec +=
                    static_cast<long>(static_cast<long>(static_cast<uint32_t>(timeoutInMs)) * NN_NO1000000);
                if (semTimeout.tv_nsec >= static_cast<long>(NN_NO1000000000)) {
                    semTimeout.tv_sec += semTimeout.tv_nsec / NN_NO1000000000;
                    semTimeout.tv_nsec %= NN_NO1000000000;
                }
            }

            if (sem_timedwait(&mQueueMeta->sem, &semTimeout) != 0) {
                continue;
            }

            /* dequeue */
            if (NN_LIKELY(Dequeue(item) == SH_OK)) {
                return SH_OK;
            }
        }
    }

    inline uint32_t Capacity() const
    {
        return mCapacity;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "name: " << mName << ", capacity: " << mCapacity;
        return oss.str();
    }

private:
    inline uint32_t RequestProduceSpace(uint32_t &oHead, uint32_t &nHead, uint32_t reqSize, uint32_t &freeSpace)
    {
        const uint32_t capacity = mCapacity;
        bool successful = false;
        uint32_t tmpReqCount = reqSize;

        do {
            /* major 3 steps:
             * step 1: assign, global variable to local one
             * step 2: calculate target variables
             * --- a) check if there are enough spaces, return if no space left,
             * --- b) calculate target
             * step 3: commit using atomic operations, if failed try
             */
            reqSize = tmpReqCount;

            oHead = mQueueMeta->prod.head;

            /* read barrier avoid order */
            H_RMB();

            freeSpace = (capacity + mQueueMeta->cons.tail - oHead);

            /* no free space */
            if (H_UNLIKELY(reqSize > freeSpace)) {
                return 0;
            }

            nHead = oHead + reqSize;

            /* commit */
            successful = H_CAS(&mQueueMeta->prod.head, oHead, nHead);
        } while (H_UNLIKELY(!successful));

        return reqSize;
    }

    inline uint32_t RequestDequeue(uint32_t &oHead, uint32_t &nHead, uint32_t reqSize, uint32_t &dequeueCount)
    {
        bool successful = false;
        do {
            /* major 3 steps:
             * step 1: assign, global variable to local one
             * step 2: calculate target variables
             * --- a) check if there are enough items, if no just we have
             * --- b) calculate target
             * step 3: commit using atomic operations, if failed try
             */
            oHead = mQueueMeta->cons.head;

            /* read barrier avoid order */
            H_RMB();

            dequeueCount = mQueueMeta->prod.tail - oHead;
            if (dequeueCount == 0) {
                return 0;
            } else if (dequeueCount > reqSize) {
                dequeueCount = reqSize;
            }

            nHead = oHead + dequeueCount;

            successful = H_CAS(&mQueueMeta->cons.head, oHead, nHead);
        } while (H_UNLIKELY(!successful));

        return dequeueCount;
    }

    inline void EnqueueItem(uint32_t oHead, T &item)
    {
        mQueueData[oHead & mMask] = item;
    }

    inline void DequeueItem(uint32_t oHead, T &item)
    {
        item = mQueueData[oHead & mMask];
    }

    /*
     * @brief update produce tail
     */
    inline bool UpdateProdTail(bool enqueue, uint32_t oldTail, uint32_t newTail)
    {
        if (enqueue) {
            H_WMB();
        } else {
            H_RMB();
        }

        uint64_t endTimeSecond = NetMonotonic::TimeSec() + mMaxEnqueueTimeout;
        // if others is enqueue/dequeue in progress, wait
        uint32_t cmpTail = oldTail;
        while (H_UNLIKELY(!H_CAS(&mQueueMeta->prod.tail, cmpTail, newTail))) {
            cmpTail = oldTail;
            if (NetMonotonic::TimeSec() > endTimeSecond) {
                NN_LOG_ERROR("Update Prod tail failed, timeout.");
                return false;
            }
        }
        return true;
    }

    /*
     * @brief update consume tail
     */
    inline void UpdateConsTail(bool enqueue, uint32_t oldTail, uint32_t newTail)
    {
        if (enqueue) {
            H_WMB();
        } else {
            H_RMB();
        }

        /* if others is enqueue/dequeue in progress, wait */
        while (H_UNLIKELY(mQueueMeta->cons.tail != oldTail)) {
            H_Pause();
        }

        mQueueMeta->cons.tail = newTail;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    ShmQueueMeta *mQueueMeta = nullptr;
    T *mQueueData = nullptr;
    bool mStop = false;
    ShmHandlePtr mShmHandle = nullptr;

    std::string mName;
    bool mInited = false;
    uint32_t mCapacity = 0;
    uint32_t mMask = 0;

    uint32_t mMaxFailedTime = 10;
    uint32_t mMaxEnqueueTimeout = 20;
    uint64_t mTempProdIdx = UINT64_MAX;
    uint64_t mFailedProd = UINT64_MAX;
    struct timespec mFailedTime {};

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // OCK_HCOM_SHM_WRAPPER_H
