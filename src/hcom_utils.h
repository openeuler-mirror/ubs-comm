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
#ifndef OCK_HCOM_NET_UTIL_H_54434
#define OCK_HCOM_NET_UTIL_H_54434

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <random>
#include <malloc.h>
#include <semaphore.h>
#include <sys/time.h>
#include <linux/limits.h>

#include "hcom_err.h"
#include "hcom_def.h"

namespace ock {
namespace hcom {
inline timespec MONOTONIC_TIME()
{
    struct timespec now {
        0, 0
    };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now;
}

inline uint64_t MONOTONIC_TIME_INTERVAL_NS(const timespec &start, const timespec &end)
{
    return (end.tv_sec - start.tv_sec) * NN_NO1000000000 +
           (end.tv_nsec - start.tv_nsec);
}

inline uint64_t MONOTONIC_TIME_INTERVAL_US(const timespec &start, const timespec &end)
{
    return (end.tv_sec - start.tv_sec) * NN_NO1000000 +
           (end.tv_nsec - start.tv_nsec) / NN_NO1000;
}

inline uint64_t MONOTONIC_TIME_INTERVAL_SEC(const timespec &start, const timespec &end)
{
    return (end.tv_sec - start.tv_sec) +
           (end.tv_nsec - start.tv_nsec) / NN_NO1000000000;
}

inline uint64_t MONOTONIC_TIME_NS()
{
    struct timespec now {
        0, 0
    };
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_nsec + now.tv_sec * NN_NO1000000000;
}

inline uint64_t MONOTONIC_TIME_SECOND()
{
    return MONOTONIC_TIME_NS() / NN_NO1000000000;
}

inline int32_t TimeSecToMs(const int32_t &timeInSec)
{
    if (NN_UNLIKELY(timeInSec < 0)) {
        return -1;
    }
    if (NN_UNLIKELY(timeInSec > static_cast<int32_t>(NN_NO2000))) {
        return NN_NO2000 * NN_NO1000;
    }
    return timeInSec * static_cast<int32_t>(NN_NO1000);
}

/**
 * @brief Check whether the path is canonical, and canonical it.
 */
inline bool CanonicalPath(std::string &path)
{
    if (path.empty() || path.size() > PATH_MAX) {
        return false;
    }

    /* It will allocate memory to store path */
    char *realPath = realpath(path.c_str(), nullptr);
    if (realPath == nullptr) {
        return false;
    }

    path = realPath;
    free(realPath);
    realPath = nullptr;
    return true;
}
/* ****************************************************************************************** */
class NetReadWriteLock {
public:
    NetReadWriteLock()
    {
        pthread_rwlock_init(&mLock, nullptr);
    }
    ~NetReadWriteLock()
    {
        pthread_rwlock_destroy(&mLock);
    }

    NetReadWriteLock(const NetReadWriteLock &) = delete;
    NetReadWriteLock &operator=(const NetReadWriteLock &) = delete;
    NetReadWriteLock(NetReadWriteLock &&) = delete;
    NetReadWriteLock &operator=(NetReadWriteLock &&) = delete;

    inline void LockRead()
    {
        pthread_rwlock_rdlock(&mLock);
    }

    inline void LockWrite()
    {
        pthread_rwlock_wrlock(&mLock);
    }

    inline void UnLock()
    {
        pthread_rwlock_unlock(&mLock);
    }

private:
    pthread_rwlock_t mLock{};
};

/* ****************************************************************************************** */
class NetSpinLock {
public:
    NetSpinLock() = default;
    ~NetSpinLock() = default;

    NetSpinLock(const NetSpinLock &) = delete;
    NetSpinLock &operator=(const NetSpinLock &) = delete;
    NetSpinLock(NetSpinLock &&) = delete;
    NetSpinLock &operator=(NetSpinLock &&) = delete;

    inline bool TryLock()
    {
        return mFlag.test_and_set(std::memory_order_acquire);
    }

    inline void Lock()
    {
        while (mFlag.test_and_set(std::memory_order_acquire)) {
        }
    }

    inline void Unlock()
    {
        mFlag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

/* ****************************************************************************************** */

template<typename T> class NetRingBuffer {
public:
    explicit NetRingBuffer(uint32_t capacity) : mCapacity(capacity)
    {
    }

    ~NetRingBuffer()
    {
        UnInitialize();
    }

    inline uint32_t Capacity() const
    {
        return mCapacity;
    }

    NResult Initialize()
    {
        if (mCapacity == 0) {
            return NN_INVALID_PARAM;
        }

        if (mRingBuf != nullptr) {
            return NN_OK;
        }

        mRingBuf = new (std::nothrow) T[mCapacity];
        if (NN_UNLIKELY(mRingBuf == nullptr)) {
            return NN_NEW_OBJECT_FAILED;
        }
        mCount = 0;
        mHead = 0;
        mTail = 0;

        return NN_OK;
    }

    inline void UnInitialize()
    {
        if (mRingBuf == nullptr) {
            return;
        }

        delete[] mRingBuf;
        mRingBuf = nullptr;
    }

    inline bool PushBack(const T &item)
    {
        mLock.Lock();
        if (mCapacity <= mCount) {
            mLock.Unlock();
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        mRingBuf[mTail] = item;
        if (mTail != mCapacity - 1) {
            ++mTail;
        } else {
            mTail = 0;
        }
        ++mCount;
        mLock.Unlock();
        return true;
    }

    inline bool InterruptablePushBack(const T &item, bool &isInterrupted)
    {
        mLock.Lock();
        if (mCapacity <= mCount) {
            mLock.Unlock();
            return false;
        }

        if (mInterrupt) {
            isInterrupted = true;
            mLock.Unlock();
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        mRingBuf[mTail] = item;
        if (mTail != mCapacity - 1) {
            ++mTail;
        } else {
            mTail = 0;
        }
        ++mCount;
        mLock.Unlock();
        return true;
    }

    inline bool PushFront(const T &item)
    {
        mLock.Lock();
        if (mCapacity <= mCount) {
            mLock.Unlock();
            return false;
        }

        // move to tail
        if (mHead == 0) {
            mHead = mCapacity - 1;
        } else {
            mHead--;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        mRingBuf[mHead] = item;
        ++mCount;

        mLock.Unlock();
        return true;
    }

    inline bool PopFront(T &item)
    {
        mLock.Lock();
        if (mCount == 0) {
            mLock.Unlock();
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        item = mRingBuf[mHead];
        if (mHead != mCapacity - 1) {
            ++mHead;
        } else {
            mHead = 0;
        }
        --mCount;
        mLock.Unlock();
        return true;
    }

    inline bool GetFront(T &item)
    {
        mLock.Lock();
        if (mCount == 0) {
            mLock.Unlock();
            return false;
        }
        item = mRingBuf[mHead];
        mLock.Unlock();
        return true;
    }

    inline bool PopFrontN(T *items, uint32_t n)
    {
        mLock.Lock();
        if (mCount < n) {
            mLock.Unlock();
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        for (uint32_t i = 0; i < n; ++i) {
            items[i] = mRingBuf[mHead];
            if (mHead != mCapacity - 1) {
                ++mHead;
            } else {
                mHead = 0;
            }
        }

        mCount -= n;

        mLock.Unlock();
        return true;
    }

    inline bool IsFull()
    {
        mLock.Lock();
        auto full = mCount >= mCapacity;
        mLock.Unlock();
        return full;
    }

    inline uint32_t Size()
    {
        mLock.Lock();
        auto temp = mCount;
        mLock.Unlock();
        return temp;
    }

    inline void Interrupt()
    {
        mLock.Lock();
        mInterrupt = true;
        mLock.Unlock();
    }

    NetRingBuffer(const NetRingBuffer &) = delete;
    NetRingBuffer(NetRingBuffer &&) = delete;
    NetRingBuffer &operator=(const NetRingBuffer &) = delete;
    NetRingBuffer &operator=(NetRingBuffer &&) = delete;

private:
    T *mRingBuf = nullptr;
    NetSpinLock mLock;
    uint32_t mCapacity = 0;
    uint32_t mCount = 0;
    uint32_t mHead = 0;
    uint32_t mTail = 0;
    bool mInterrupt = false;
};

template<typename T> class NetBlockingQueue {
public:
    explicit NetBlockingQueue(uint32_t capacity) : mRingBuffer(capacity)
    {
    }
    ~NetBlockingQueue()
    {
        UnInitialize();
    }

    inline NResult Initialize()
    {
        if (sem_init(&mSem, 0, 0) != 0) {
            return NN_BLOCK_QUEUE_SEM_INIT_FAILED;
        }

        return mRingBuffer.Initialize();
    }

    inline void UnInitialize()
    {
        mRingBuffer.UnInitialize();
        sem_destroy(&mSem);
    }

    inline bool Enqueue(T &item)
    {
        auto result = mRingBuffer.PushBack(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    inline bool EnqueueFirst(T &item)
    {
        auto result = mRingBuffer.PushFront(item);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /* tip Dequeue and Interrupt cannot be used at the same time */
    inline bool Dequeue(T &item)
    {
        while (true) {
            auto result = mRingBuffer.PopFront(item);
            if (!result) {
                sem_wait(&mSem);
            } else {
                // result always true
                return result;
            }
        }
    }

    inline bool InterruptableEnqueue(const T &item, bool &isInterrupted)
    {
        auto result = mRingBuffer.InterruptablePushBack(item, isInterrupted);
        if (result) {
            sem_post(&mSem);
        }
        return result;
    }

    /* tip Dequeue and InterruptableDequeue cannot be
     * used at the same time */
    inline bool InterruptableDequeue(T &item, bool &isInterrupt)
    {
        isInterrupt = false;
        while (true) {
            auto result = mRingBuffer.PopFront(item);
            if (!result) {
                sem_wait(&mSem);
                if (NN_UNLIKELY(mInterrupt)) {
                    isInterrupt = true;
                    mInterrupt.store(false);
                    return false;
                }
            } else {
                // result always true
                return result;
            }
        }
    }

    inline uint32_t Size()
    {
        return mRingBuffer.Size();
    }

    /* tip Interrupt only be used for InterruptableDequeue */
    inline void Interrupt()
    {
        mInterrupt.store(true);
        sem_post(&mSem);
        mRingBuffer.Interrupt();
    }

private:
    NetRingBuffer<T> mRingBuffer;
    sem_t mSem{};
    std::atomic<bool> mInterrupt{false};
};

/* ****************************************************************************************** */
class NetUuid {
public:
    static inline uint64_t GenerateUuid()
    {
        // 高32位：时间戳（ns级）
        uint64_t timestamp = static_cast<uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());

        gLock.Lock();
        uint32_t seqNo = gSeqNo++;
        gLock.Unlock();

        return (timestamp << NN_NO32) | seqNo;
    }

    static uint64_t GenerateUuid(const std::string& ip);
private:
    static uint32_t gSeqNo;
    static NetSpinLock gLock;
};

/* ****************************************************************************************** */
// const variables
constexpr uint32_t PAGE_ALIGN_H = NN_NO4096;

// defines
#define POWER_OF_2(x) ((((x) - 1) & (x)) == 0)

#define H_LIKELY(e) (__builtin_expect(!!(e), 1) != 0)
#define H_UNLIKELY(e) (__builtin_expect(!!(e), 0) != 0)

#define H_CAS(ptr, o, n)                                           \
    __atomic_compare_exchange_n(ptr, &(o), n, 0, __ATOMIC_RELEASE, \
                                __ATOMIC_RELAXED)
#define H_WMB() __atomic_thread_fence(__ATOMIC_RELEASE)
#define H_RMB() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define H_MB() __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define H_ATOMIC_LOAD(n) __atomic_load_n(&(n), __ATOMIC_RELAXED)
#define H_ATOMIC_FAA(n, num) __atomic_fetch_add(&(n), (num), __ATOMIC_RELAXED)
#define H_ATOMIC_STORE(n, num) __atomic_store_n(&(n), (num), __ATOMIC_RELAXED)

inline void H_Pause()
{
#ifdef __x86_64__
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#endif
}

inline uint32_t NN_NextPower2(uint32_t value)
{
    if (value < NN_NO2) {
        return NN_NO2;
    }
    return 1UL << (NN_NO32 - __builtin_clz(value - 1));
}

}  // namespace hcom
}  // namespace ock

#endif  // OCK_HCOM_NET_UTIL_H_54434
