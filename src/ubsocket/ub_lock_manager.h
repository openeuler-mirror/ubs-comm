/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-03-19
 * Note:
 * History: 2026-03-19
*/
#ifndef THREAD_LOCK_MANAGER_H
#define THREAD_LOCK_MANAGER_H

#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <functional>
#include <pthread.h>

class UbExclusiveLock {
public:
    virtual ~UbExclusiveLock() = default;
    
    virtual void lock() = 0;
    virtual void unlock() = 0;
    virtual bool try_lock() = 0;
};

class UbRWLock {
public:
    virtual ~UbRWLock() = default;
    
    virtual void rdlock() = 0;
    virtual void wrlock() = 0;
    virtual void unlock() = 0;
    virtual bool try_rdlock() = 0;
    virtual bool try_wrlock() = 0;
};

class DefaultExclusiveLock : public UbExclusiveLock {
public:
    void lock() override
    {
        mutex_.lock();
    }
    
    void unlock() override
    {
        mutex_.unlock();
    }
    
    bool try_lock() override
    {
        return mutex_.try_lock();
    }

private:
    std::mutex mutex_;
};

class DefaultRWLock : public UbRWLock {
public:
    DefaultRWLock()
    {
        pthread_rwlock_init(&rwlock_, nullptr);
    }
    
    ~DefaultRWLock() override
    {
        pthread_rwlock_destroy(&rwlock_);
    }
    
    void rdlock() override
    {
        pthread_rwlock_rdlock(&rwlock_);
    }
    
    void wrlock() override
    {
        pthread_rwlock_wrlock(&rwlock_);
    }
    
    void unlock() override
    {
        pthread_rwlock_unlock(&rwlock_);
    }
    
    bool try_rdlock() override
    {
        return pthread_rwlock_tryrdlock(&rwlock_) == 0;
    }
    
    bool try_wrlock() override
    {
        return pthread_rwlock_trywrlock(&rwlock_) == 0;
    }

private:
    pthread_rwlock_t rwlock_;
};

class UbLockManager {
public:
    using ExclusiveLockFactory = std::function<std::unique_ptr<UbExclusiveLock>()>;
    using RWLockFactory = std::function<std::unique_ptr<UbRWLock>()>;
    
    static UbLockManager& instance()
    {
        static UbLockManager registry;
        return registry;
    }
    
    void registerExclusiveLock(ExclusiveLockFactory factory)
    {
        exclusive_lock_factory = std::move(factory);
        exclusive_lock_registered = true;
    }
    
    void registerRWLock(RWLockFactory factory)
    {
        rwlock_factory = std::move(factory);
        rwlock_registered = true;
    }
    
    std::unique_ptr<UbExclusiveLock> createExclusiveLock()
    {
        if (exclusive_lock_registered) {
            return exclusive_lock_factory();
        }
        return std::make_unique<DefaultExclusiveLock>();
    }
    
    std::unique_ptr<UbRWLock> createRWLock()
    {
        if (rwlock_registered) {
            return rwlock_factory();
        }
        return std::make_unique<DefaultRWLock>();
    }
    
private:
    UbLockManager() = default;
    ExclusiveLockFactory exclusive_lock_factory;
    RWLockFactory rwlock_factory;
    bool exclusive_lock_registered = false;
    bool rwlock_registered = false;
};

class ScopedUbExclusiveLock {
public:
    explicit ScopedUbExclusiveLock(UbExclusiveLock& lock) : lock_(lock)
    {
        lock_.lock();
    }
    
    ~ScopedUbExclusiveLock()
    {
        lock_.unlock();
    }
    
    ScopedUbExclusiveLock(const ScopedUbExclusiveLock&) = delete;
    ScopedUbExclusiveLock& operator=(const ScopedUbExclusiveLock&) = delete;

private:
    UbExclusiveLock& lock_;
};

class ScopedUbReadLock {
public:
    explicit ScopedUbReadLock(UbRWLock &lock) : rwlock_(lock)
    {
        rwlock_.rdlock();
    }

    ~ScopedUbReadLock()
    {
        rwlock_.unlock();
    }

private:
    UbRWLock& rwlock_;
};

class ScopedUbWriteLock {
public:
    explicit ScopedUbWriteLock(UbRWLock &lock) : rwlock_(lock)
    {
        rwlock_.wrlock();
    }

    ~ScopedUbWriteLock()
    {
        rwlock_.unlock();
    }

private:
    UbRWLock& rwlock_;
};

template<typename UbLockType>
class UbLazyLock {
public:
    UbLazyLock() = default;
   
    UbLazyLock(const UbLazyLock&) = delete;
    UbLazyLock& operator=(const UbLazyLock&) = delete;
    
    UbLockType* operator->()
    {
        return getLock();
    }
    
    UbLockType& operator*()
    {
        return *getLock();
    }
    
    UbLockType& get()
    {
        return *getLock();
    }

private:
    UbLockType* getLock()
    {
        static UbLockType* lock = []() -> UbLockType* {
            auto& manager = UbLockManager::instance();
            if constexpr (std::is_same<UbLockType, UbExclusiveLock>::value) {
                return manager.createExclusiveLock().release();
            } else {
                return manager.createRWLock().release();
            }
        }();
        if (!lock_) {
            lock_ = lock;
        }
        return lock_;
    }
    
    UbLockType* lock_ = nullptr;
};

#endif // THREAD_LOCK_MANAGER_H