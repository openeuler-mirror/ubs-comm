/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for ub_lock_ops module
 */

#include "ub_lock_ops.h"
#include "ub_lock_def.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <pthread.h>
#include <semaphore.h>
#include <cstring>

namespace {
constexpr int K_SEM_INIT_VAL_1 = 1;
constexpr int K_SEM_INIT_VAL_2 = 2;
}  // namespace

// Test fixture for external lock tests
class ExternalLockTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void ExternalLockTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void ExternalLockTest::TearDown()
{
    GlobalMockObject::verify();
}

// ============= External Lock Tests =============

TEST_F(ExternalLockTest, Create_Exclusive)
{
    u_external_mutex_t* mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    EXPECT_NE(mutex, nullptr);

    // Cleanup
    if (mutex != nullptr) {
        g_external_lock_ops.destroy(mutex);
    }
}

TEST_F(ExternalLockTest, Create_Recursive)
{
    u_external_mutex_t* mutex = g_external_lock_ops.create(LT_RECURSIVE);
    EXPECT_NE(mutex, nullptr);

    // Cleanup
    if (mutex != nullptr) {
        g_external_lock_ops.destroy(mutex);
    }
}

TEST_F(ExternalLockTest, Destroy_NullPtr)
{
    int ret = g_external_lock_ops.destroy(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(ExternalLockTest, Lock_NullPtr)
{
    int ret = g_external_lock_ops.lock(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(ExternalLockTest, Unlock_NullPtr)
{
    int ret = g_external_lock_ops.unlock(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(ExternalLockTest, TryLock_NullPtr)
{
    int ret = g_external_lock_ops.try_lock(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(ExternalLockTest, LockUnlock_Success)
{
    u_external_mutex_t* mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    ASSERT_NE(mutex, nullptr);

    int ret = g_external_lock_ops.lock(mutex);
    EXPECT_EQ(ret, 0);

    ret = g_external_lock_ops.unlock(mutex);
    EXPECT_EQ(ret, 0);

    g_external_lock_ops.destroy(mutex);
}

TEST_F(ExternalLockTest, TryLock_Success)
{
    u_external_mutex_t* mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    ASSERT_NE(mutex, nullptr);

    int ret = g_external_lock_ops.try_lock(mutex);
    EXPECT_EQ(ret, 0);

    ret = g_external_lock_ops.unlock(mutex);
    EXPECT_EQ(ret, 0);

    g_external_lock_ops.destroy(mutex);
}

TEST_F(ExternalLockTest, RecursiveLock_MultipleTimes)
{
    u_external_mutex_t* mutex = g_external_lock_ops.create(LT_RECURSIVE);
    ASSERT_NE(mutex, nullptr);

    // Lock multiple times with recursive mutex
    int ret = g_external_lock_ops.lock(mutex);
    EXPECT_EQ(ret, 0);

    ret = g_external_lock_ops.lock(mutex);
    EXPECT_EQ(ret, 0);

    // Unlock multiple times
    ret = g_external_lock_ops.unlock(mutex);
    EXPECT_EQ(ret, 0);

    ret = g_external_lock_ops.unlock(mutex);
    EXPECT_EQ(ret, 0);

    g_external_lock_ops.destroy(mutex);
}

// ============= RW Lock Tests =============

class RWLockTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void RWLockTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void RWLockTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(RWLockTest, Create_Success)
{
    u_rw_lock_t* rwlock = g_rw_lock_ops.create();
    EXPECT_NE(rwlock, nullptr);

    if (rwlock != nullptr) {
        g_rw_lock_ops.destroy(rwlock);
    }
}

TEST_F(RWLockTest, Destroy_NullPtr)
{
    int ret = g_rw_lock_ops.destroy(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, LockRead_NullPtr)
{
    int ret = g_rw_lock_ops.lock_read(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, LockWrite_NullPtr)
{
    int ret = g_rw_lock_ops.lock_write(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, UnlockRW_NullPtr)
{
    int ret = g_rw_lock_ops.unlock_rw(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, TryLockRead_NullPtr)
{
    int ret = g_rw_lock_ops.try_lock_read(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, TryLockWrite_NullPtr)
{
    int ret = g_rw_lock_ops.try_lock_write(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RWLockTest, LockReadUnlock_Success)
{
    u_rw_lock_t* rwlock = g_rw_lock_ops.create();
    ASSERT_NE(rwlock, nullptr);

    int ret = g_rw_lock_ops.lock_read(rwlock);
    EXPECT_EQ(ret, 0);

    ret = g_rw_lock_ops.unlock_rw(rwlock);
    EXPECT_EQ(ret, 0);

    g_rw_lock_ops.destroy(rwlock);
}

TEST_F(RWLockTest, LockWriteUnlock_Success)
{
    u_rw_lock_t* rwlock = g_rw_lock_ops.create();
    ASSERT_NE(rwlock, nullptr);

    int ret = g_rw_lock_ops.lock_write(rwlock);
    EXPECT_EQ(ret, 0);

    ret = g_rw_lock_ops.unlock_rw(rwlock);
    EXPECT_EQ(ret, 0);

    g_rw_lock_ops.destroy(rwlock);
}

TEST_F(RWLockTest, TryLockRead_Success)
{
    u_rw_lock_t* rwlock = g_rw_lock_ops.create();
    ASSERT_NE(rwlock, nullptr);

    int ret = g_rw_lock_ops.try_lock_read(rwlock);
    EXPECT_EQ(ret, 0);

    ret = g_rw_lock_ops.unlock_rw(rwlock);
    EXPECT_EQ(ret, 0);

    g_rw_lock_ops.destroy(rwlock);
}

TEST_F(RWLockTest, TryLockWrite_Success)
{
    u_rw_lock_t* rwlock = g_rw_lock_ops.create();
    ASSERT_NE(rwlock, nullptr);

    int ret = g_rw_lock_ops.try_lock_write(rwlock);
    EXPECT_EQ(ret, 0);

    ret = g_rw_lock_ops.unlock_rw(rwlock);
    EXPECT_EQ(ret, 0);

    g_rw_lock_ops.destroy(rwlock);
}

// ============= Semaphore Tests =============

class SemaphoreTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void SemaphoreTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void SemaphoreTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(SemaphoreTest, Create_Success)
{
    u_semaphore_t* sem = g_semaphore_ops.create();
    EXPECT_NE(sem, nullptr);

    if (sem != nullptr) {
        g_semaphore_ops.destroy(sem);
    }
}

TEST_F(SemaphoreTest, Destroy_NullPtr)
{
    int ret = g_semaphore_ops.destroy(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(SemaphoreTest, Init_NullPtr)
{
    int ret = g_semaphore_ops.init(nullptr, 0, 1);
    EXPECT_EQ(ret, -1);
}

TEST_F(SemaphoreTest, Wait_NullPtr)
{
    int ret = g_semaphore_ops.wait(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(SemaphoreTest, Post_NullPtr)
{
    int ret = g_semaphore_ops.post(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(SemaphoreTest, InitWaitPost_Success)
{
    u_semaphore_t* sem = g_semaphore_ops.create();
    ASSERT_NE(sem, nullptr);

    // Initialize with value 1
    int ret = g_semaphore_ops.init(sem, 0, 1);
    EXPECT_EQ(ret, 0);

    // Wait (should succeed immediately with value 1)
    ret = g_semaphore_ops.wait(sem);
    EXPECT_EQ(ret, 0);

    // Post (increment back to 1)
    ret = g_semaphore_ops.post(sem);
    EXPECT_EQ(ret, 0);

    g_semaphore_ops.destroy(sem);
}

TEST_F(SemaphoreTest, InitZero_WaitBlocks)
{
    u_semaphore_t* sem = g_semaphore_ops.create();
    ASSERT_NE(sem, nullptr);

    // Initialize with value 0
    int ret = g_semaphore_ops.init(sem, 0, 0);
    EXPECT_EQ(ret, 0);

    // Post first to make it non-blocking
    ret = g_semaphore_ops.post(sem);
    EXPECT_EQ(ret, 0);

    // Now wait should succeed
    ret = g_semaphore_ops.wait(sem);
    EXPECT_EQ(ret, 0);

    g_semaphore_ops.destroy(sem);
}

TEST_F(SemaphoreTest, MultiplePostWait)
{
    u_semaphore_t* sem = g_semaphore_ops.create();
    ASSERT_NE(sem, nullptr);

    int ret = g_semaphore_ops.init(sem, 0, 0);
    EXPECT_EQ(ret, 0);

    // Post twice
    ret = g_semaphore_ops.post(sem);
    EXPECT_EQ(ret, 0);

    ret = g_semaphore_ops.post(sem);
    EXPECT_EQ(ret, 0);

    // Wait twice
    ret = g_semaphore_ops.wait(sem);
    EXPECT_EQ(ret, 0);

    ret = g_semaphore_ops.wait(sem);
    EXPECT_EQ(ret, 0);

    g_semaphore_ops.destroy(sem);
}

// ============= Register Ops Tests =============

class RegisterOpsTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void RegisterOpsTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void RegisterOpsTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(RegisterOpsTest, RegisterExternalLockOps_NullOps)
{
    int ret = u_register_external_lock_ops(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterExternalLockOps_NullCreate)
{
    u_external_lock_ops_t ops = {
        .create = nullptr,
        .destroy = (int (*)(u_external_mutex_t*))1,
        .lock = (int (*)(u_external_mutex_t*))1,
        .unlock = (int (*)(u_external_mutex_t*))1,
        .try_lock = (int (*)(u_external_mutex_t*))1
    };
    int ret = u_register_external_lock_ops(&ops);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterExternalLockOps_NullDestroy)
{
    u_external_lock_ops_t ops = {
        .create = (u_external_mutex_t* (*)(u_external_mutex_type))1,
        .destroy = nullptr,
        .lock = (int (*)(u_external_mutex_t*))1,
        .unlock = (int (*)(u_external_mutex_t*))1,
        .try_lock = (int (*)(u_external_mutex_t*))1
    };
    int ret = u_register_external_lock_ops(&ops);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterRWLockOps_NullOps)
{
    int ret = u_register_rw_lock_ops(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterRWLockOps_NullCreate)
{
    u_rw_lock_ops_t ops = {
        .create = nullptr,
        .destroy = (int (*)(u_rw_lock_t*))1,
        .lock_read = (int (*)(u_rw_lock_t*))1,
        .lock_write = (int (*)(u_rw_lock_t*))1,
        .unlock_rw = (int (*)(u_rw_lock_t*))1,
        .try_lock_read = (int (*)(u_rw_lock_t*))1,
        .try_lock_write = (int (*)(u_rw_lock_t*))1
    };
    int ret = u_register_rw_lock_ops(&ops);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterSemaphoreOps_NullOps)
{
    int ret = u_register_semaphore_ops(nullptr);
    EXPECT_EQ(ret, -1);
}

TEST_F(RegisterOpsTest, RegisterSemaphoreOps_NullCreate)
{
    u_semaphore_ops_t ops = {
        .create = nullptr,
        .destroy = (int (*)(u_semaphore_t*))1,
        .init = (int (*)(u_semaphore_t*, int, unsigned int))1,
        .wait = (int (*)(u_semaphore_t*))1,
        .post = (int (*)(u_semaphore_t*))1
    };
    int ret = u_register_semaphore_ops(&ops);
    EXPECT_EQ(ret, -1);
}

// ============= UbExclusiveLock Tests =============

class UbExclusiveLockTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

void UbExclusiveLockTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void UbExclusiveLockTest::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(UbExclusiveLockTest, ConstructorDestructor)
{
    UbExclusiveLock lock;
    EXPECT_NE(lock.GetMutex(), nullptr);
}

TEST_F(UbExclusiveLockTest, ScopedLocker)
{
    UbExclusiveLock lock;
    u_external_mutex_t* mutex = lock.GetMutex();
    ASSERT_NE(mutex, nullptr);

    {
        ScopedUbExclusiveLocker locker(mutex);
        // Lock held in this scope
    }
    // Lock released after scope
}

TEST_F(UbExclusiveLockTest, ScopedLocker_NullPtr)
{
    // ScopedUbExclusiveLocker with nullptr should still call ops
    ScopedUbExclusiveLocker locker(nullptr);
}

// ============= ScopedUbReadLocker/ScopedUbWriteLocker Tests =============

class ScopedRWLockerTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
    u_rw_lock_t* rwlock_;
};

void ScopedRWLockerTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    rwlock_ = g_rw_lock_ops.create();
}

void ScopedRWLockerTest::TearDown()
{
    if (rwlock_ != nullptr) {
        g_rw_lock_ops.destroy(rwlock_);
    }
    GlobalMockObject::verify();
}

TEST_F(ScopedRWLockerTest, ScopedReadLocker)
{
    ASSERT_NE(rwlock_, nullptr);
    {
        ScopedUbReadLocker locker(rwlock_);
        // Read lock held
    }
    // Read lock released
}

TEST_F(ScopedRWLockerTest, ScopedWriteLocker)
{
    ASSERT_NE(rwlock_, nullptr);
    {
        ScopedUbWriteLocker locker(rwlock_);
        // Write lock held
    }
    // Write lock released
}

TEST_F(ScopedRWLockerTest, ScopedReadLocker_NullPtr)
{
    ScopedUbReadLocker locker(nullptr);
}

TEST_F(ScopedRWLockerTest, ScopedWriteLocker_NullPtr)
{
    ScopedUbWriteLocker locker(nullptr);
}

// ============= Additional External Lock Tests =============

TEST_F(ExternalLockTest, MultipleLocks)
{
    u_external_mutex_t* lock1 = g_external_lock_ops.create(LT_EXCLUSIVE);
    u_external_mutex_t* lock2 = g_external_lock_ops.create(LT_RECURSIVE);

    ASSERT_NE(lock1, nullptr);
    ASSERT_NE(lock2, nullptr);

    EXPECT_EQ(g_external_lock_ops.lock(lock1), 0);
    EXPECT_EQ(g_external_lock_ops.lock(lock2), 0);

    EXPECT_EQ(g_external_lock_ops.unlock(lock2), 0);
    EXPECT_EQ(g_external_lock_ops.unlock(lock1), 0);

    g_external_lock_ops.destroy(lock1);
    g_external_lock_ops.destroy(lock2);
}

TEST_F(ExternalLockTest, RecursiveLock)
{
    u_external_mutex_t* lock = g_external_lock_ops.create(LT_RECURSIVE);
    ASSERT_NE(lock, nullptr);

    // Lock multiple times
    EXPECT_EQ(g_external_lock_ops.lock(lock), 0);
    EXPECT_EQ(g_external_lock_ops.lock(lock), 0);
    EXPECT_EQ(g_external_lock_ops.lock(lock), 0);

    // Unlock same number of times
    EXPECT_EQ(g_external_lock_ops.unlock(lock), 0);
    EXPECT_EQ(g_external_lock_ops.unlock(lock), 0);
    EXPECT_EQ(g_external_lock_ops.unlock(lock), 0);

    g_external_lock_ops.destroy(lock);
}

TEST_F(ExternalLockTest, TryLock)
{
    u_external_mutex_t* lock = g_external_lock_ops.create(LT_EXCLUSIVE);
    ASSERT_NE(lock, nullptr);

    EXPECT_EQ(g_external_lock_ops.try_lock(lock), 0);
    EXPECT_EQ(g_external_lock_ops.unlock(lock), 0);

    g_external_lock_ops.destroy(lock);
}

// ============= Additional RW Lock Tests =============

TEST_F(RWLockTest, MultipleReadWriteLocks)
{
    u_rw_lock_t* lock1 = g_rw_lock_ops.create();
    u_rw_lock_t* lock2 = g_rw_lock_ops.create();

    ASSERT_NE(lock1, nullptr);
    ASSERT_NE(lock2, nullptr);

    EXPECT_EQ(g_rw_lock_ops.lock_read(lock1), 0);
    EXPECT_EQ(g_rw_lock_ops.lock_read(lock2), 0);

    EXPECT_EQ(g_rw_lock_ops.unlock_rw(lock2), 0);
    EXPECT_EQ(g_rw_lock_ops.unlock_rw(lock1), 0);

    g_rw_lock_ops.destroy(lock1);
    g_rw_lock_ops.destroy(lock2);
}

TEST_F(RWLockTest, TryLockRead)
{
    u_rw_lock_t* lock = g_rw_lock_ops.create();
    ASSERT_NE(lock, nullptr);

    EXPECT_EQ(g_rw_lock_ops.try_lock_read(lock), 0);
    EXPECT_EQ(g_rw_lock_ops.unlock_rw(lock), 0);

    g_rw_lock_ops.destroy(lock);
}

TEST_F(RWLockTest, TryLockWrite)
{
    u_rw_lock_t* lock = g_rw_lock_ops.create();
    ASSERT_NE(lock, nullptr);

    EXPECT_EQ(g_rw_lock_ops.try_lock_write(lock), 0);
    EXPECT_EQ(g_rw_lock_ops.unlock_rw(lock), 0);

    g_rw_lock_ops.destroy(lock);
}

// ============= Additional Semaphore Tests =============

TEST_F(SemaphoreTest, MultipleSemaphores)
{
    u_semaphore_t* sem1 = g_semaphore_ops.create();
    u_semaphore_t* sem2 = g_semaphore_ops.create();

    ASSERT_NE(sem1, nullptr);
    ASSERT_NE(sem2, nullptr);

    EXPECT_EQ(g_semaphore_ops.init(sem1, 0, 1), 0);
    EXPECT_EQ(g_semaphore_ops.init(sem2, 0, K_SEM_INIT_VAL_2), 0);

    EXPECT_EQ(g_semaphore_ops.wait(sem1), 0);
    EXPECT_EQ(g_semaphore_ops.wait(sem2), 0);
    EXPECT_EQ(g_semaphore_ops.wait(sem2), 0);

    g_semaphore_ops.destroy(sem1);
    g_semaphore_ops.destroy(sem2);
}

TEST_F(SemaphoreTest, PostBeforeWait)
{
    u_semaphore_t* sem = g_semaphore_ops.create();
    ASSERT_NE(sem, nullptr);

    EXPECT_EQ(g_semaphore_ops.init(sem, 0, 0), 0);

    // Post twice
    EXPECT_EQ(g_semaphore_ops.post(sem), 0);
    EXPECT_EQ(g_semaphore_ops.post(sem), 0);

    // Wait should succeed immediately
    EXPECT_EQ(g_semaphore_ops.wait(sem), 0);
    EXPECT_EQ(g_semaphore_ops.wait(sem), 0);

    g_semaphore_ops.destroy(sem);
}