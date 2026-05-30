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
#include "core/ubsocket_core_types.h"
namespace ock {
namespace ubs {
typedef void *u_mutex_t;
template <typename T>
class UbsocketRingBuffer {
public:
    UbsocketRingBuffer() {}

    ~UbsocketRingBuffer()
    {
        UnInitialize();
    }

    int Initialize(uint32_t capacity)
    {
        if (mInited_) {
            return 0;
        }
        if (capacity <= 0) {
            return -1;
        }
        mCapacity_ = capacity;

        if (mRingBuf != nullptr) {
            return 0;
        }

        mRingBuf = new (std::nothrow) T[mCapacity_];
        if (mRingBuf == nullptr) {
            return -1;
        }
        mCount_ = 0;
        mHead_ = 0;
        mTail_ = 0;
        mInited_ = true;
        mLock = ock::ubs::LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        return 0;
    }

    uint32_t Capacity() const
    {
        return mCapacity_;
    }

    void UnInitialize()
    {
        if (!mInited_) {
            return;
        }
        ock::ubs::Locker sLock(mLock);
        if (mRingBuf == nullptr) {
            return;
        }
        delete[] mRingBuf;
        mRingBuf = nullptr;
        mInited_ = false;
    }

    bool PushBack(const T &item)
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        if (mCapacity_ <= mCount_) {
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        mRingBuf[mTail_] = item;
        if (mTail_ != mCapacity_ - 1) {
            ++mTail_;
        } else {
            mTail_ = 0;
        }
        ++mCount_;
        return true;
    }

    bool PushFront(const T &item)
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        if (mCapacity_ <= mCount_) {
            return false;
        }

        // move to tail
        if (mHead_ == 0) {
            mHead_ = mCapacity_ - 1;
        } else {
            mHead_--;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        mRingBuf[mHead_] = item;
        ++mCount_;

        return true;
    }

    bool PopFront(T &item)
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        if (mCount_ == 0) {
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        item = mRingBuf[mHead_];
        if (mHead_ != mCapacity_ - 1) {
            ++mHead_;
        } else {
            mHead_ = 0;
        }
        --mCount_;
        return true;
    }

    bool GetFront(T &item)
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        if (mCount_ == 0) {
            return false;
        }
        item = mRingBuf[mHead_];
        return true;
    }

    bool PopFrontN(T *items, uint32_t n)
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        if (mCount_ < n) {
            return false;
        }

        // mRinBuf will not be null after init, this func is performance-sensitive, there is no need to check null
        for (uint32_t i = 0; i < n; ++i) {
            items[i] = mRingBuf[mHead_];
            if (mHead_ != mCapacity_ - 1) {
                ++mHead_;
            } else {
                mHead_ = 0;
            }
        }

        mCount_ -= n;

        return true;
    }

    bool IsFull()
    {
        if (!mInited_) {
            return false;
        }
        ock::ubs::Locker sLock(mLock);
        return mCount_ >= mCapacity_;
    }

    uint32_t Size()
    {
        if (!mInited_) {
            return 0;
        }
        ock::ubs::Locker sLock(mLock);
        return mCount_;
    }

    UbsocketRingBuffer(const UbsocketRingBuffer &) = delete;
    UbsocketRingBuffer(UbsocketRingBuffer &&) = delete;
    UbsocketRingBuffer &operator=(const UbsocketRingBuffer &) = delete;
    UbsocketRingBuffer &operator=(UbsocketRingBuffer &&) = delete;

private:
    T *mRingBuf = nullptr;
    u_mutex_t *mLock;
    uint32_t mCapacity_ = 0;
    uint32_t mCount_ = 0;
    uint32_t mHead_ = 0;
    uint32_t mTail_ = 0;
    bool mInited_ = false;
};
} // namespace ubs
} // namespace ock

#endif // UBSOCKET_UB_RING_BUFFER_H