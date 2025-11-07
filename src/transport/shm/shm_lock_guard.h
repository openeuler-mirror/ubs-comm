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
#ifndef OCK_HCOM_SHM_LOCK_GUARD_H
#define OCK_HCOM_SHM_LOCK_GUARD_H
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