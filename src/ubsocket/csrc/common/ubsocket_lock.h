/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_LOCK_H
#define UBS_COMM_UBSOCKET_LOCK_H

#include "include/ubsocket_def.h"
#include "ubsocket_defines.h"
#include "ubsocket_errno.h"

namespace ock {
namespace ubs {

class LockRegistry {
public:
    /**
     * Register default ops
     *
     * @return
     */
    static Result RegisterDefaultOps();

    /**
     * @brief Register external lock ops, if ops is invalid, register to default ops
     *
     * @param ops          [in] ops to be registered
     * @return 0 if successful
     */
    static Result RegisterLockOps(u_external_lock_ops_t *ops);

    /**
     * @brief Register external rw lock ops, if ops is invalid, register to default ops
     *
     * @param ops          [in] ops to be registered
     * @return 0 if successful
     */
    static Result RegisterRwLockOps(u_external_rw_lock_ops_t *ops);

    /**
     * @brief Register external sem ops, if ops is invalid, register to default ops
     *
     * @param ops          [in] ops to be registered
     * @return 0 if successful
     */
    static Result RegisterSemOps(u_external_semaphore_ops_t *ops);

public:
    static u_external_lock_ops_t LOCK_OPS;       /* global lock ops, registered from external or use default ops */
    static u_external_rw_lock_ops_t RW_LOCK_OPS; /* global rw lock ops, registered from external or use default ops */
    static u_external_semaphore_ops_t SEM_OPS;   /* global sem ops, registered from external or use default ops */
};

class Locker {
public:
    explicit Locker(u_mutex_t *lock) : lock_(lock)
    {
        LockRegistry::LOCK_OPS.lock(lock_);
    }

    ~Locker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            LockRegistry::LOCK_OPS.unlock(lock_);
            unlocked_ = true;
        }
    }

    Locker(const Locker &) = delete;
    Locker &operator=(const Locker &) = delete;

    Locker(Locker &&) = delete;
    Locker &operator=(Locker &&) = delete;

private:
    u_mutex_t *lock_ = nullptr;
    bool unlocked_ = false;
};

class ReadLocker {
public:
    explicit ReadLocker(u_rw_lock_t *lock) : rwlock_(lock)
    {
        LockRegistry::RW_LOCK_OPS.lock_read(rwlock_);
    }

    ~ReadLocker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            LockRegistry::RW_LOCK_OPS.unlock_rw(rwlock_);
            unlocked_ = true;
        }
    }

    ReadLocker(const ReadLocker &) = delete;
    ReadLocker &operator=(const ReadLocker &) = delete;

    ReadLocker(ReadLocker &&) = delete;
    ReadLocker &operator=(ReadLocker &&) = delete;

private:
    u_rw_lock_t *rwlock_ = nullptr;
    bool unlocked_ = false;
};

class WriteLocker {
public:
    explicit WriteLocker(u_rw_lock_t *lock) : rwlock_(lock)
    {
        LockRegistry::RW_LOCK_OPS.lock_write(rwlock_);
    }

    ~WriteLocker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            LockRegistry::RW_LOCK_OPS.unlock_rw(rwlock_);
            unlocked_ = true;
        }
    }

    WriteLocker(const WriteLocker &) = delete;
    WriteLocker &operator=(const WriteLocker &) = delete;

    WriteLocker(WriteLocker &&) = delete;
    WriteLocker &operator=(WriteLocker &&) = delete;

private:
    u_rw_lock_t *rwlock_ = nullptr;
    bool unlocked_ = false;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_LOCK_H
