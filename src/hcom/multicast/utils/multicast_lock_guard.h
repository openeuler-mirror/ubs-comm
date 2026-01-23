/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
 */
#ifndef OCK_HCOM_MULTICAST_LOCK_GUARD_H
#define OCK_HCOM_MULTICAST_LOCK_GUARD_H
#include <unistd.h>
#include "hcom.h"

namespace ock {
namespace hcom {

class RWLockGuard {
public:
    RWLockGuard(RWLockGuard &) = delete;
    RWLockGuard &operator = (RWLockGuard &) = delete;

    explicit RWLockGuard(NetReadWriteLock &lock) : mRwLock(lock) {}

    inline void LockRead()
    {
        mRwLock.LockRead();
    }

    inline void LockWrite()
    {
        mRwLock.LockWrite();
    }

    ~RWLockGuard()
    {
        mRwLock.UnLock();
    }

private:
    NetReadWriteLock &mRwLock;
};
}
}
#endif