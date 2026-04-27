/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-03-19
 * Note:
 * History: 2026-03-19
*/
#ifndef UB_LOCK_OPS_H
#define UB_LOCK_OPS_H

#include "ub_lock_def.h"

extern u_external_lock_ops_t g_external_lock_ops;
extern u_rw_lock_ops_t g_rw_lock_ops;
extern u_semaphore_ops_t g_semaphore_ops;

// Register external lock operations
int u_register_external_lock_ops(const u_external_lock_ops_t *ops);

// Register RW lock operations
int u_register_rw_lock_ops(const u_rw_lock_ops_t *ops);

// Register semaphore operations
int u_register_semaphore_ops(const u_semaphore_ops_t *ops);

class UbExclusiveLock {
public:
    UbExclusiveLock() {
        mutex_ = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    
    ~UbExclusiveLock() {
        g_external_lock_ops.destroy(mutex_);
    }

    u_external_mutex_t* GetMutex()
    {
        return mutex_;
    }

private:
    u_external_mutex_t* mutex_ = nullptr;
};

class ScopedUbExclusiveLocker {
public:
    explicit ScopedUbExclusiveLocker(u_external_mutex_t* lock) : lock_(lock)
    {
        g_external_lock_ops.lock(lock_);
    }

    ~ScopedUbExclusiveLocker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            g_external_lock_ops.unlock(lock_);
            unlocked_ = true;
        }
    }

    ScopedUbExclusiveLocker(const ScopedUbExclusiveLocker&) = delete;
    ScopedUbExclusiveLocker& operator=(const ScopedUbExclusiveLocker&) = delete;

    ScopedUbExclusiveLocker(ScopedUbExclusiveLocker&&) = delete;
    ScopedUbExclusiveLocker& operator=(ScopedUbExclusiveLocker&&) = delete;

private:
    u_external_mutex_t* lock_ = nullptr;
    bool unlocked_ = false;
};

class ScopedUbReadLocker {
public:
    explicit ScopedUbReadLocker(u_rw_lock_t* lock) : rwlock_(lock)
    {
        g_rw_lock_ops.lock_read(rwlock_);
    }

    ~ScopedUbReadLocker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            g_rw_lock_ops.unlock_rw(rwlock_);
            unlocked_ = true;
        }
    }

    ScopedUbReadLocker(const ScopedUbReadLocker&) = delete;
    ScopedUbReadLocker& operator=(const ScopedUbReadLocker&) = delete;

    ScopedUbReadLocker(ScopedUbReadLocker&&) = delete;
    ScopedUbReadLocker& operator=(ScopedUbReadLocker&&) = delete;

private:
    u_rw_lock_t* rwlock_ = nullptr;
    bool unlocked_ = false;
};

class ScopedUbWriteLocker {
public:
    explicit ScopedUbWriteLocker(u_rw_lock_t* lock) : rwlock_(lock)
    {
        g_rw_lock_ops.lock_write(rwlock_);
    }

    ~ScopedUbWriteLocker()
    {
        Unlock();
    }

    void Unlock()
    {
        if (!unlocked_) {
            g_rw_lock_ops.unlock_rw(rwlock_);
            unlocked_ = true;
        }
    }

    ScopedUbWriteLocker(const ScopedUbWriteLocker&) = delete;
    ScopedUbWriteLocker& operator=(const ScopedUbWriteLocker&) = delete;

    ScopedUbWriteLocker(ScopedUbWriteLocker&&) = delete;
    ScopedUbWriteLocker& operator=(ScopedUbWriteLocker&&) = delete;

private:
    u_rw_lock_t* rwlock_ = nullptr;
    bool unlocked_ = false;
};

#endif // UB_LOCK_OPS_H