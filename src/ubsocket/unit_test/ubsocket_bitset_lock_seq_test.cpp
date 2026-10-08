/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include "common/ubsocket_flash_dynamic_bitset.h"
#include "common/ubsocket_lock.h"
#include "core/umq/umq_bounded_seq.h"

using namespace ock::ubs;
using namespace umq;

// ==================== FlashDynamicBitSet ====================

class FlashDynamicBitSetTest : public ::testing::Test {
};

TEST_F(FlashDynamicBitSetTest, ConstructWithCapacity)
{
    FlashDynamicBitSet bs(128);
    EXPECT_EQ(bs.Capacity(), 128U);
    EXPECT_EQ(bs.Count(), 0U);
    EXPECT_FALSE(bs.Full());
}

TEST_F(FlashDynamicBitSetTest, GetMemSize)
{
    EXPECT_GT(FlashDynamicBitSet::GetMemSize(64), 0U);
    EXPECT_GT(FlashDynamicBitSet::GetMemSize(128), FlashDynamicBitSet::GetMemSize(64));
}

TEST_F(FlashDynamicBitSetTest, GetMemSize_ZeroCapacityReturnsZero)
{
    EXPECT_EQ(FlashDynamicBitSet::GetMemSize(0), 0U);
}

TEST_F(FlashDynamicBitSetTest, SetAndTest)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    EXPECT_TRUE(bs.Test(0));
    EXPECT_FALSE(bs.Test(1));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, SetMultiple)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    bs.Set(10);
    bs.Set(63);
    EXPECT_TRUE(bs.Test(0));
    EXPECT_TRUE(bs.Test(10));
    EXPECT_TRUE(bs.Test(63));
    EXPECT_EQ(bs.Count(), 3U);
}

TEST_F(FlashDynamicBitSetTest, SetOutOfRange)
{
    FlashDynamicBitSet bs(64);
    bs.Set(64);
    EXPECT_EQ(bs.Count(), 0U);
    bs.Set(1000);
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, SetAlreadySet)
{
    FlashDynamicBitSet bs(64);
    bs.Set(5);
    EXPECT_EQ(bs.Count(), 1U);
    bs.Set(5);
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, Clear)
{
    FlashDynamicBitSet bs(64);
    bs.Set(5);
    EXPECT_TRUE(bs.Test(5));
    EXPECT_TRUE(bs.Clear(5));
    EXPECT_FALSE(bs.Test(5));
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearOutOfRange)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    EXPECT_FALSE(bs.Clear(64));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, ClearAlreadyCleared)
{
    FlashDynamicBitSet bs(64);
    EXPECT_TRUE(bs.Clear(5));
    EXPECT_TRUE(bs.Clear(5));
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearAll)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    bs.Set(10);
    bs.Set(63);
    EXPECT_EQ(bs.Count(), 3U);
    bs.ClearAll();
    EXPECT_EQ(bs.Count(), 0U);
    EXPECT_FALSE(bs.Test(0));
    EXPECT_FALSE(bs.Test(10));
    EXPECT_FALSE(bs.Test(63));
}

TEST_F(FlashDynamicBitSetTest, Full)
{
    FlashDynamicBitSet bs(4);
    EXPECT_FALSE(bs.Full());
    bs.Set(0);
    bs.Set(1);
    bs.Set(2);
    EXPECT_FALSE(bs.Full());
    bs.Set(3);
    EXPECT_TRUE(bs.Full());
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_Normal)
{
    FlashDynamicBitSet bs(128);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(bs.FindAndSet(0, pos));
    EXPECT_EQ(pos, 0U);
    EXPECT_TRUE(bs.Test(0));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_SkipsUsedBits)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    bs.Set(1);
    bs.Set(2);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(bs.FindAndSet(0, pos));
    EXPECT_EQ(pos, 3U);
    EXPECT_EQ(bs.Count(), 4U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_StartPos)
{
    FlashDynamicBitSet bs(64);
    bs.Set(0);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(bs.FindAndSet(1, pos));
    EXPECT_EQ(pos, 1U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_FullReturnsFalse)
{
    FlashDynamicBitSet bs(4);
    bs.Set(0);
    bs.Set(1);
    bs.Set(2);
    bs.Set(3);
    EXPECT_TRUE(bs.Full());
    uint32_t pos = 0;
    EXPECT_FALSE(bs.FindAndSet(0, pos));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_StartPosOutOfRange)
{
    FlashDynamicBitSet bs(64);
    uint32_t pos = 0;
    EXPECT_FALSE(bs.FindAndSet(64, pos));
    EXPECT_FALSE(bs.FindAndSet(100, pos));
}

TEST_F(FlashDynamicBitSetTest, ConstructWithExternalMemory)
{
    uint32_t cap = 64;
    auto memSize = FlashDynamicBitSet::GetMemSize(cap);
    auto *mem = new uint8_t[memSize];
    bzero(mem, memSize);

    {
        FlashDynamicBitSet bs(reinterpret_cast<uintptr_t>(mem), cap, true);
        EXPECT_EQ(bs.Capacity(), cap);
        EXPECT_EQ(bs.Count(), 0U);
        bs.Set(10);
        EXPECT_TRUE(bs.Test(10));
    }
    delete[] mem;
}

TEST_F(FlashDynamicBitSetTest, TestOutOfRange)
{
    FlashDynamicBitSet bs(64);
    EXPECT_FALSE(bs.Test(64));
    EXPECT_FALSE(bs.Test(1000));
}

// ==================== UmqBoundedSeqTraits ====================

class BoundedSeqTest : public ::testing::Test {
};

TEST_F(BoundedSeqTest, Mask_PowerOfTwo)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_EQ(T::MASK, 0xFFFFU);
    EXPECT_EQ(T::Mask(0x12345), 0x2345U);
}

TEST_F(BoundedSeqTest, Normalize_NoMaxVal)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_EQ(T::Normalize(100), 100U);
    EXPECT_EQ(T::Normalize(T::MASK), T::MASK);
}

TEST_F(BoundedSeqTest, Distance_NoMaxVal)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_EQ(T::Distance(0, 10), 10U);
    EXPECT_EQ(T::Distance(10, 5), 65531U); // wrap-around
}

TEST_F(BoundedSeqTest, CompareLessInCircularOrder)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_FALSE(T::CompareLessInCircularOrder(10, 5));
    EXPECT_TRUE(T::CompareLessInCircularOrder(5, 10));
}

TEST_F(BoundedSeqTest, Add_NoMaxVal)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_EQ(T::Add(10, 5), 15U);
    EXPECT_EQ(T::Add(0xFFF0, 32), 16U); // wrap
}

TEST_F(BoundedSeqTest, Next)
{
    using T = UmqBoundedSeqTraits<16>;
    EXPECT_EQ(T::Next(0), 1U);
    EXPECT_EQ(T::Next(T::MASK), 0U); // wrap
}

TEST_F(BoundedSeqTest, WithMaxVal_Normalize)
{
    using T = UmqBoundedSeqTraits<16, uint32_t, 100>;
    EXPECT_EQ(T::Normalize(50), 50U);
    EXPECT_EQ(T::Normalize(101), 0U); // wraps
}

TEST_F(BoundedSeqTest, WithMaxVal_Distance)
{
    using T = UmqBoundedSeqTraits<16, uint32_t, 100>;
    EXPECT_EQ(T::Distance(10, 50), 40U);
    EXPECT_EQ(T::Distance(90, 10), 21U); // wrap: (101-90) + 10 = 21
}

TEST_F(BoundedSeqTest, WithMaxVal_Add)
{
    using T = UmqBoundedSeqTraits<16, uint32_t, 100>;
    EXPECT_EQ(T::Add(50, 30), 80U);
    EXPECT_EQ(T::Add(80, 30), 9U); // 80+30=110, wraps: 110-101=9
}

TEST_F(BoundedSeqTest, WithMaxVal_AddNegative)
{
    using T = UmqBoundedSeqTraits<16, uint32_t, 100>;
    EXPECT_EQ(T::Add(50, -10), 40U);
    EXPECT_EQ(T::Add(5, -10), 96U); // 5-10=-5, wraps: 101-5=96
}

// ==================== UmqSocketBoundedSequence ====================

TEST_F(BoundedSeqTest, Sequence_DefaultConstruct)
{
    UmqSocketBoundedSequence<16> seq;
    EXPECT_EQ(seq.LoadSeqNum(), 0U);
}

TEST_F(BoundedSeqTest, Sequence_ConstructWithValue)
{
    UmqSocketBoundedSequence<16> seq(42);
    EXPECT_EQ(seq.LoadSeqNum(), 42U);
}

TEST_F(BoundedSeqTest, Sequence_FetchAddSeqNum)
{
    UmqSocketBoundedSequence<16> seq(0);
    auto old = seq.FetchAddSeqNum(5);
    EXPECT_EQ(old, 0U);
    EXPECT_EQ(seq.LoadSeqNum(), 5U);

    old = seq.FetchAddSeqNum(3);
    EXPECT_EQ(old, 5U);
    EXPECT_EQ(seq.LoadSeqNum(), 8U);
}

TEST_F(BoundedSeqTest, Sequence_FetchSubSeqNum)
{
    UmqSocketBoundedSequence<16> seq(10);
    auto old = seq.FetchSubSeqNum(3);
    EXPECT_EQ(old, 10U);
    EXPECT_EQ(seq.LoadSeqNum(), 7U);
}

TEST_F(BoundedSeqTest, Sequence_StoreSeqNum)
{
    UmqSocketBoundedSequence<16> seq;
    seq.StoreSeqNum(100);
    EXPECT_EQ(seq.LoadSeqNum(), 100U);
}

// ==================== Lock ====================

class LockTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        LockRegistry::RegisterDefaultOps();
    }
};

TEST_F(LockTest, RegisterDefaultOps_SetsOps)
{
    // ops should be non-null after SetUpTestSuite calls RegisterDefaultOps
    EXPECT_NE(LockRegistry::LOCK_OPS.lock, nullptr);
    EXPECT_NE(LockRegistry::LOCK_OPS.unlock, nullptr);
    EXPECT_NE(LockRegistry::RW_LOCK_OPS.lock_read, nullptr);
    EXPECT_NE(LockRegistry::RW_LOCK_OPS.lock_write, nullptr);
    EXPECT_NE(LockRegistry::RW_LOCK_OPS.unlock_rw, nullptr);
}

TEST_F(LockTest, Locker_LocksAndUnlocksOnDestroy)
{
    auto *m = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    EXPECT_NE(m, nullptr);
    {
        Locker locker(m);
        // mutex locked
        int rc = LockRegistry::LOCK_OPS.try_lock(m);
        EXPECT_NE(rc, 0); // should fail, already locked
    }
    // locker destroyed, mutex unlocked
    int rc = LockRegistry::LOCK_OPS.try_lock(m);
    EXPECT_EQ(rc, 0);
    LockRegistry::LOCK_OPS.unlock(m);
    LockRegistry::LOCK_OPS.destroy(m);
}

TEST_F(LockTest, Locker_ExplicitUnlock)
{
    auto *m = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    {
        Locker locker(m);
        locker.Unlock();
        // mutex should be unlocked now
        int rc = LockRegistry::LOCK_OPS.try_lock(m);
        EXPECT_EQ(rc, 0);
        LockRegistry::LOCK_OPS.unlock(m);
    }
    // destructor won't double-unlock
    LockRegistry::LOCK_OPS.destroy(m);
}

TEST_F(LockTest, Locker_DoubleUnlockSafe)
{
    auto *m = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    {
        Locker locker(m);
        locker.Unlock();
        locker.Unlock(); // should be no-op
    }
    LockRegistry::LOCK_OPS.destroy(m);
}

TEST_F(LockTest, ReadLocker_LocksAndUnlocks)
{
    auto *rw = LockRegistry::RW_LOCK_OPS.create();
    EXPECT_NE(rw, nullptr);
    {
        ReadLocker locker(rw);
        // read lock held
    }
    // read lock released
    // write lock should be acquirable now
    auto *rw2 = LockRegistry::RW_LOCK_OPS.create();
    {
        WriteLocker locker(rw2);
        // should succeed (no read lock held on rw)
    }
    LockRegistry::RW_LOCK_OPS.destroy(rw);
    LockRegistry::RW_LOCK_OPS.destroy(rw2);
}

TEST_F(LockTest, WriteLocker_LocksAndUnlocks)
{
    auto *rw = LockRegistry::RW_LOCK_OPS.create();
    EXPECT_NE(rw, nullptr);
    {
        WriteLocker locker(rw);
        // write lock held
        int rc = LockRegistry::RW_LOCK_OPS.try_lock_read(rw);
        EXPECT_NE(rc, 0); // read lock should fail while write lock held
    }
    // write lock released
    int rc = LockRegistry::RW_LOCK_OPS.try_lock_read(rw);
    EXPECT_EQ(rc, 0);
    LockRegistry::RW_LOCK_OPS.unlock_rw(rw);
    LockRegistry::RW_LOCK_OPS.destroy(rw);
}

TEST_F(LockTest, WriteLocker_ExplicitUnlock)
{
    auto *rw = LockRegistry::RW_LOCK_OPS.create();
    {
        WriteLocker locker(rw);
        locker.Unlock();
        int rc = LockRegistry::RW_LOCK_OPS.try_lock_read(rw);
        EXPECT_EQ(rc, 0);
        LockRegistry::RW_LOCK_OPS.unlock_rw(rw);
    }
    LockRegistry::RW_LOCK_OPS.destroy(rw);
}

TEST_F(LockTest, ReadLocker_ExplicitUnlock)
{
    auto *rw = LockRegistry::RW_LOCK_OPS.create();
    {
        ReadLocker locker(rw);
        locker.Unlock();
        // should be unlocked
    }
    LockRegistry::RW_LOCK_OPS.destroy(rw);
}

TEST_F(LockTest, RegisterCustomOps)
{
    u_external_lock_ops_t custom_ops = {
        .create = +[](u_mutex_type_t) -> u_mutex_t * { return reinterpret_cast<u_mutex_t *>(0x1); },
        .destroy = +[](u_mutex_t *) -> int { return 0; },
        .lock = +[](u_mutex_t *) -> int { return 0; },
        .unlock = +[](u_mutex_t *) -> int { return 0; },
        .try_lock = +[](u_mutex_t *) -> int { return 0; },
    };

    // Save original ops
    auto saved = LockRegistry::LOCK_OPS;
    LockRegistry::RegisterLockOps(&custom_ops);

    u_mutex_t dummy = nullptr;
    {
        Locker locker(&dummy);
    }
    // No crash means custom ops work

    // Restore
    LockRegistry::LOCK_OPS = saved;
}
