/*
  * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
  * Description: Provide the utility for umq buffer, iov, etc
  * Author:
  * Create: 2026-05-08
  * Note:
  * History: 2026-05-08
 */
#ifndef UBSOCKET_UB_RING_BUFFER_H
#define UBSOCKET_UB_RING_BUFFER_H

#include <cstdint>
#include <new>
#include "ub_lock_ops.h"

template<typename T> class UbRingBuffer {
public:
    UbRingBuffer() {}

    ~UbRingBuffer()
    {
        UnInitialize();
    }

    int Initialize(uint32_t capacity)
    {
        if (mInited) {
            return 0;
        }
        if (capacity <= 0) {
            return -1;
        }
        mCapacity = capacity;

        if (mRingBuf != nullptr) {
            return 0;
        }

        mRingBuf = new (std::nothrow) T[mCapacity];
        if (mRingBuf == nullptr) {
            return -1;
        }
        mCount = 0;
        mHead = 0;
        mTail = 0;
        mInited = true;
        return 0;
    }

    uint32_t Capacity() const
    {
        return mCapacity;
    }

    void UnInitialize()
    {
        if (!mInited) {
            return;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mRingBuf == nullptr) {
            return;
        }
        delete[] mRingBuf;
        mRingBuf = nullptr;
        mInited = false;
    }

    bool PushBack(const T &item)
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mCapacity <= mCount) {
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
        return true;
    }

    bool PushFront(const T &item)
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mCapacity <= mCount) {
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

        return true;
    }

    bool PopFront(T &item)
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mCount == 0) {
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
        return true;
    }

    bool GetFront(T &item)
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mCount == 0) {
            return false;
        }
        item = mRingBuf[mHead];
        return true;
    }

    bool PopFrontN(T *items, uint32_t n)
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        if (mCount < n) {
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

        return true;
    }

    bool IsFull()
    {
        if (!mInited) {
            return false;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        return mCount >= mCapacity;
    }

    uint32_t Size()
    {
        if (!mInited) {
            return 0;
        }
        ScopedUbExclusiveLocker lock(mLock.GetMutex());
        return mCount;
    }

    UbRingBuffer(const UbRingBuffer &) = delete;
    UbRingBuffer(UbRingBuffer &&) = delete;
    UbRingBuffer &operator=(const UbRingBuffer &) = delete;
    UbRingBuffer &operator=(UbRingBuffer &&) = delete;

private:
    T *mRingBuf = nullptr;
    UbExclusiveLock mLock;
    uint32_t mCapacity = 0;
    uint32_t mCount = 0;
    uint32_t mHead = 0;
    uint32_t mTail = 0;
    bool mInited = false;
};


#endif // UBSOCKET_UB_RING_BUFFER_H