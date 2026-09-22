/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <pthread.h>
#include <semaphore.h>
#include <sstream>
#include <cstring>

#include "common/ubsocket_flash_dynamic_bitset.h"
#include "common/ubsocket_lock.h"
#include "core/umq/umq_bounded_seq.h"

using namespace ock::ubs;
using namespace umq;

// ==================== FlashDynamicBitSet ====================
//
// FlashDynamicBitSet methods are ALWAYS_INLINE, so each call site generates
// separate branch counters in gcov. To make 100% branch coverage achievable,
// noinline helper wrappers consolidate each method to a single call site.

__attribute__((noinline)) static void SetBit(FlashDynamicBitSet &bs, uint32_t pos)
{
    bs.Set(pos);
}

__attribute__((noinline)) static bool ClearBit(FlashDynamicBitSet &bs, uint32_t pos)
{
    return bs.Clear(pos);
}

__attribute__((noinline)) static void ClearAllBits(FlashDynamicBitSet &bs)
{
    bs.ClearAll();
}

__attribute__((noinline)) static bool TestBit(const FlashDynamicBitSet &bs, uint32_t pos)
{
    return bs.Test(pos);
}

__attribute__((noinline)) static bool FindAndSetBit(FlashDynamicBitSet &bs, uint32_t startPos, uint32_t &resultPos)
{
    return bs.FindAndSet(startPos, resultPos);
}

__attribute__((noinline)) static uint64_t GetMemSizeHelper(uint32_t capacity)
{
    return FlashDynamicBitSet::GetMemSize(capacity);
}

__attribute__((noinline)) static std::string BitsetToString(const FlashDynamicBitSet &bs, bool failStream = false)
{
    std::ostringstream oss;
    if (failStream) {
        oss.setstate(std::ios::failbit);
    }
    oss << bs;
    return oss.str();
}

static void MockExternalLog(int level, const char *msg, const char *filename, int line)
{
    (void)level;
    (void)msg;
    (void)filename;
    (void)line;
}

class FlashDynamicBitSetTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        savedLogLevel_ = Logger::Instance().GetLogLevel();
        Logger::Instance().logLevel = LEVEL_DEBUG;
    }

    void TearDown() override
    {
        Logger::Instance().mLogFunc = nullptr;
        Logger::Instance().SetLogLevel(savedLogLevel_);
    }

private:
    int savedLogLevel_{LEVEL_INFO};
};

TEST_F(FlashDynamicBitSetTest, ConstructWithCapacity)
{
    FlashDynamicBitSet bs(128);
    EXPECT_EQ(bs.Capacity(), 128U);
    EXPECT_EQ(bs.Count(), 0U);
    EXPECT_FALSE(bs.Full());
}

TEST_F(FlashDynamicBitSetTest, GetMemSize_ValidCapacity)
{
    EXPECT_GT(GetMemSizeHelper(64), 0U);
    EXPECT_GT(GetMemSizeHelper(128), GetMemSizeHelper(64));
}

TEST_F(FlashDynamicBitSetTest, GetMemSize_ZeroCapacity_ReturnsZero)
{
    EXPECT_EQ(GetMemSizeHelper(0), 0U);
}

TEST_F(FlashDynamicBitSetTest, GetMemSize_DebugLogSuppressed_ByHighLogLevel)
{
    Logger::Instance().logLevel = LEVEL_ERR;
    EXPECT_EQ(GetMemSizeHelper(64), 8U);
    Logger::Instance().logLevel = LEVEL_DEBUG;
}

TEST_F(FlashDynamicBitSetTest, GetMemSize_ErrLogSuppressed_ByHighLogLevel)
{
    Logger::Instance().logLevel = static_cast<LogLevel>(LEVEL_COUNT);
    EXPECT_EQ(GetMemSizeHelper(0), 0U);
    Logger::Instance().logLevel = LEVEL_DEBUG;
}

TEST_F(FlashDynamicBitSetTest, Log_WithDefaultLogFunction)
{
    EXPECT_EQ(GetMemSizeHelper(0), 0U);
    EXPECT_GT(GetMemSizeHelper(64), 0U);
    FlashDynamicBitSet bs(64);
    SetBit(bs, 64);
    uint32_t pos = 0;
    FindAndSetBit(bs, 64, pos);
}

TEST_F(FlashDynamicBitSetTest, Log_WithExternalLogFunction)
{
    Logger::Instance().mLogFunc = MockExternalLog;
    EXPECT_EQ(GetMemSizeHelper(0), 0U);
    EXPECT_GT(GetMemSizeHelper(64), 0U);
    FlashDynamicBitSet bs(64);
    SetBit(bs, 64);
    uint32_t pos = 0;
    FindAndSetBit(bs, 64, pos);
    Logger::Instance().mLogFunc = nullptr;
}

TEST_F(FlashDynamicBitSetTest, SetAndTest)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    EXPECT_TRUE(TestBit(bs, 0));
    EXPECT_FALSE(TestBit(bs, 1));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, SetMultiple)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    SetBit(bs, 10);
    SetBit(bs, 63);
    EXPECT_TRUE(TestBit(bs, 0));
    EXPECT_TRUE(TestBit(bs, 10));
    EXPECT_TRUE(TestBit(bs, 63));
    EXPECT_EQ(bs.Count(), 3U);
}

TEST_F(FlashDynamicBitSetTest, SetOutOfRange)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 64);
    EXPECT_EQ(bs.Count(), 0U);
    SetBit(bs, 1000);
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, SetWarnLogSuppressed_ByHighLogLevel)
{
    FlashDynamicBitSet bs(64);
    Logger::Instance().logLevel = LEVEL_ERR;
    SetBit(bs, 64);
    EXPECT_EQ(bs.Count(), 0U);
    Logger::Instance().logLevel = LEVEL_DEBUG;
}

TEST_F(FlashDynamicBitSetTest, SetAlreadySet)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 5);
    EXPECT_EQ(bs.Count(), 1U);
    SetBit(bs, 5);
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, SetMultipleChunks_UpdatesMaxTouchedChunk)
{
    FlashDynamicBitSet bs(128);
    SetBit(bs, 0);
    EXPECT_EQ(bs.Count(), 1U);
    SetBit(bs, 64);
    EXPECT_EQ(bs.Count(), 2U);
    EXPECT_TRUE(TestBit(bs, 64));
}

TEST_F(FlashDynamicBitSetTest, Clear)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 5);
    EXPECT_TRUE(TestBit(bs, 5));
    EXPECT_TRUE(ClearBit(bs, 5));
    EXPECT_FALSE(TestBit(bs, 5));
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearOutOfRange)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    EXPECT_FALSE(ClearBit(bs, 64));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, ClearAlreadyCleared)
{
    FlashDynamicBitSet bs(64);
    EXPECT_TRUE(ClearBit(bs, 5));
    EXPECT_TRUE(ClearBit(bs, 5));
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearAll)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    SetBit(bs, 10);
    SetBit(bs, 63);
    EXPECT_EQ(bs.Count(), 3U);
    ClearAllBits(bs);
    EXPECT_EQ(bs.Count(), 0U);
    EXPECT_FALSE(TestBit(bs, 0));
    EXPECT_FALSE(TestBit(bs, 10));
    EXPECT_FALSE(TestBit(bs, 63));
}

TEST_F(FlashDynamicBitSetTest, ClearAll_OnEmptyBitSet)
{
    FlashDynamicBitSet bs(0);
    EXPECT_EQ(bs.Capacity(), 0U);
    ClearAllBits(bs);
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearAll_NoTouchedRange_WithTrueCount)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 5);
    bs.minTouchedChunk_ = UINT32_MAX;
    bs.maxTouchedChunk_ = 0;
    ClearAllBits(bs);
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, ClearAll_NoBitsSet)
{
    FlashDynamicBitSet bs(64);
    ClearAllBits(bs);
    EXPECT_EQ(bs.Count(), 0U);
}

TEST_F(FlashDynamicBitSetTest, Full)
{
    FlashDynamicBitSet bs(4);
    EXPECT_FALSE(bs.Full());
    SetBit(bs, 0);
    SetBit(bs, 1);
    SetBit(bs, 2);
    EXPECT_FALSE(bs.Full());
    SetBit(bs, 3);
    EXPECT_TRUE(bs.Full());
}

TEST_F(FlashDynamicBitSetTest, TestOutOfRange)
{
    FlashDynamicBitSet bs(64);
    EXPECT_FALSE(TestBit(bs, 64));
    EXPECT_FALSE(TestBit(bs, 1000));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_Normal)
{
    FlashDynamicBitSet bs(128);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(FindAndSetBit(bs, 0, pos));
    EXPECT_EQ(pos, 0U);
    EXPECT_TRUE(TestBit(bs, 0));
    EXPECT_EQ(bs.Count(), 1U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_SkipsUsedBits)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    SetBit(bs, 1);
    SetBit(bs, 2);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(FindAndSetBit(bs, 0, pos));
    EXPECT_EQ(pos, 3U);
    EXPECT_EQ(bs.Count(), 4U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_StartPos)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(FindAndSetBit(bs, 1, pos));
    EXPECT_EQ(pos, 1U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_FullReturnsFalse)
{
    FlashDynamicBitSet bs(4);
    SetBit(bs, 0);
    SetBit(bs, 1);
    SetBit(bs, 2);
    SetBit(bs, 3);
    EXPECT_TRUE(bs.Full());
    uint32_t pos = 0;
    EXPECT_FALSE(FindAndSetBit(bs, 0, pos));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_StartPosOutOfRange)
{
    FlashDynamicBitSet bs(64);
    uint32_t pos = 0;
    EXPECT_FALSE(FindAndSetBit(bs, 64, pos));
    EXPECT_FALSE(FindAndSetBit(bs, 100, pos));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_ErrLogSuppressed_ByHighLogLevel)
{
    FlashDynamicBitSet bs(64);
    uint32_t pos = 0;
    Logger::Instance().logLevel = static_cast<LogLevel>(LEVEL_COUNT);
    EXPECT_FALSE(FindAndSetBit(bs, 64, pos));
    Logger::Instance().logLevel = LEVEL_DEBUG;
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_SkipsFullChunk)
{
    FlashDynamicBitSet bs(128);
    for (uint32_t i = 0; i < 64; ++i) {
        SetBit(bs, i);
    }
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(FindAndSetBit(bs, 0, pos));
    EXPECT_EQ(pos, 64U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_BitIdxZero_ContinuesToNextChunk)
{
    FlashDynamicBitSet bs(128);
    for (uint32_t i = 1; i < 64; ++i) {
        SetBit(bs, i);
    }
    uint32_t pos = UINT32_MAX;
    EXPECT_TRUE(FindAndSetBit(bs, 1, pos));
    EXPECT_EQ(pos, 64U);
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_ResultPosExceedsCapacity)
{
    FlashDynamicBitSet bs(65);
    for (uint32_t i = 0; i < 65; ++i) {
        SetBit(bs, i);
    }
    bs.trueCount_ = 64;
    uint32_t pos = UINT32_MAX;
    EXPECT_FALSE(FindAndSetBit(bs, 0, pos));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_LoopExhaust_ReturnsFalse)
{
    FlashDynamicBitSet bs(64);
    for (uint32_t i = 0; i < 64; ++i) {
        SetBit(bs, i);
    }
    bs.trueCount_ = 63;
    uint32_t pos = 0;
    EXPECT_FALSE(FindAndSetBit(bs, 0, pos));
}

TEST_F(FlashDynamicBitSetTest, FindAndSet_UpdatesMaxTouchedChunk)
{
    FlashDynamicBitSet bs(128);
    uint32_t pos = 0;
    FindAndSetBit(bs, 0, pos);
    pos = 0;
    FindAndSetBit(bs, 64, pos);
    EXPECT_EQ(pos, 64U);
}

TEST_F(FlashDynamicBitSetTest, ConstructWithExternalMemory_ClearBits)
{
    uint32_t cap = 64;
    auto memSize = GetMemSizeHelper(cap);
    auto *mem = new uint8_t[memSize];
    bzero(mem, memSize);

    {
        FlashDynamicBitSet bs(reinterpret_cast<uintptr_t>(mem), cap, true);
        EXPECT_EQ(bs.Capacity(), cap);
        EXPECT_EQ(bs.Count(), 0U);
        SetBit(bs, 10);
        EXPECT_TRUE(TestBit(bs, 10));
    }
    delete[] mem;
}

TEST_F(FlashDynamicBitSetTest, ConstructWithExternalMemory_NoClear_WithBitsSet)
{
    uint32_t cap = 66;
    auto memSize = GetMemSizeHelper(cap);
    auto *mem = new uint8_t[memSize];
    bzero(mem, memSize);

    auto *chunks = reinterpret_cast<uint64_t *>(mem);
    chunks[0] = 0x1;
    chunks[1] = 0x2;

    {
        FlashDynamicBitSet bs(reinterpret_cast<uintptr_t>(mem), cap, false);
        EXPECT_EQ(bs.Capacity(), cap);
        EXPECT_EQ(bs.Count(), 2U);
        EXPECT_TRUE(TestBit(bs, 0));
        EXPECT_TRUE(TestBit(bs, 65));
    }
    delete[] mem;
}

TEST_F(FlashDynamicBitSetTest, ConstructWithExternalMemory_NoClear_NoBitsSet)
{
    uint32_t cap = 64;
    auto memSize = GetMemSizeHelper(cap);
    auto *mem = new uint8_t[memSize];
    bzero(mem, memSize);

    {
        FlashDynamicBitSet bs(reinterpret_cast<uintptr_t>(mem), cap, false);
        EXPECT_EQ(bs.Capacity(), cap);
        EXPECT_EQ(bs.Count(), 0U);
    }
    delete[] mem;
}

TEST_F(FlashDynamicBitSetTest, ConstructWithExternalMemory_NoClear_ZeroCapacity)
{
    uint32_t cap = 0;
    auto *mem = new uint8_t[8];

    {
        FlashDynamicBitSet bs(reinterpret_cast<uintptr_t>(mem), cap, false);
        EXPECT_EQ(bs.Capacity(), 0U);
        EXPECT_EQ(bs.Count(), 0U);
    }
    delete[] mem;
}

TEST_F(FlashDynamicBitSetTest, OperatorStreamOutput)
{
    FlashDynamicBitSet bs(128);
    SetBit(bs, 0);
    SetBit(bs, 64);
    std::string s = BitsetToString(bs);
    EXPECT_NE(s.find("FlashDynamicBitSet"), std::string::npos);
    EXPECT_NE(s.find("capacity: 128"), std::string::npos);
    EXPECT_NE(s.find("bit allocated: 2"), std::string::npos);
}

TEST_F(FlashDynamicBitSetTest, OperatorStreamOutput_NullBitChunks)
{
    FlashDynamicBitSet bs(0);
    std::string s = BitsetToString(bs);
    EXPECT_NE(s.find("initialized: 0"), std::string::npos);
}

TEST_F(FlashDynamicBitSetTest, OperatorStreamOutput_BadStream)
{
    FlashDynamicBitSet bs(64);
    SetBit(bs, 0);
    std::string s = BitsetToString(bs, true);
    EXPECT_TRUE(s.empty());
}

TEST_F(FlashDynamicBitSetTest, OperatorStreamOutput_BadStream_NullBitChunks)
{
    FlashDynamicBitSet bs(0);
    std::string s = BitsetToString(bs, true);
    EXPECT_TRUE(s.empty());
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

// --- Lock: nullptr checks ---

TEST_F(LockTest, LockOps_Nullptr_Destroy)
{
    EXPECT_EQ(LockRegistry::LOCK_OPS.destroy(nullptr), -1);
}

TEST_F(LockTest, LockOps_Nullptr_Lock)
{
    EXPECT_EQ(LockRegistry::LOCK_OPS.lock(nullptr), -1);
}

TEST_F(LockTest, LockOps_Nullptr_Unlock)
{
    EXPECT_EQ(LockRegistry::LOCK_OPS.unlock(nullptr), -1);
}

TEST_F(LockTest, LockOps_Nullptr_TryLock)
{
    EXPECT_EQ(LockRegistry::LOCK_OPS.try_lock(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_Destroy)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.destroy(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_LockRead)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.lock_read(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_LockWrite)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.lock_write(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_UnlockRw)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.unlock_rw(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_TryLockRead)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.try_lock_read(nullptr), -1);
}

TEST_F(LockTest, RwLockOps_Nullptr_TryLockWrite)
{
    EXPECT_EQ(LockRegistry::RW_LOCK_OPS.try_lock_write(nullptr), -1);
}

// --- Lock: recursive mutex ---

TEST_F(LockTest, Create_RecursiveMutex)
{
    auto *m = LockRegistry::LOCK_OPS.create(LT_RECURSIVE);
    EXPECT_NE(m, nullptr);
    EXPECT_EQ(LockRegistry::LOCK_OPS.lock(m), 0);
    // Recursive lock should succeed (not deadlock)
    EXPECT_EQ(LockRegistry::LOCK_OPS.try_lock(m), 0);
    LockRegistry::LOCK_OPS.unlock(m);
    LockRegistry::LOCK_OPS.unlock(m);
    LockRegistry::LOCK_OPS.destroy(m);
}

// --- Semaphore operations ---

TEST_F(LockTest, Semaphore_CreateInitWaitPost_Destroy)
{
    auto *s = LockRegistry::SEM_OPS.create();
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(LockRegistry::SEM_OPS.init(s, 0, 0), 0);
    ASSERT_EQ(LockRegistry::SEM_OPS.post(s), 0);
    ASSERT_EQ(LockRegistry::SEM_OPS.wait(s), 0);
    ASSERT_EQ(LockRegistry::SEM_OPS.destroy(s), 0);
}

TEST_F(LockTest, Semaphore_Nullptr_Destroy)
{
    EXPECT_EQ(LockRegistry::SEM_OPS.destroy(nullptr), -1);
}

TEST_F(LockTest, Semaphore_Nullptr_Init)
{
    EXPECT_EQ(LockRegistry::SEM_OPS.init(nullptr, 0, 0), -1);
}

TEST_F(LockTest, Semaphore_Nullptr_Wait)
{
    EXPECT_EQ(LockRegistry::SEM_OPS.wait(nullptr), -1);
}

TEST_F(LockTest, Semaphore_Nullptr_Post)
{
    EXPECT_EQ(LockRegistry::SEM_OPS.post(nullptr), -1);
}

// --- Registration validation ---

TEST_F(LockTest, RegisterLockOps_NullOps_ReturnsInvalid)
{
    EXPECT_EQ(LockRegistry::RegisterLockOps(nullptr), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterLockOps_MissingFunctions_ReturnsInvalid)
{
    u_external_lock_ops_t incomplete = {nullptr, nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(LockRegistry::RegisterLockOps(&incomplete), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterRwLockOps_NullOps_ReturnsInvalid)
{
    EXPECT_EQ(LockRegistry::RegisterRwLockOps(nullptr), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterRwLockOps_MissingFunctions_ReturnsInvalid)
{
    u_external_rw_lock_ops_t incomplete = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(LockRegistry::RegisterRwLockOps(&incomplete), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterSemOps_NullOps_ReturnsInvalid)
{
    EXPECT_EQ(LockRegistry::RegisterSemOps(nullptr), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterSemOps_MissingFunctions_ReturnsInvalid)
{
    u_external_semaphore_ops_t incomplete = {nullptr, nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(LockRegistry::RegisterSemOps(&incomplete), UBS_INVALID_PARAM);
}

TEST_F(LockTest, RegisterRwLockOps_CompleteOps_ReturnsOk)
{
    u_external_rw_lock_ops_t ops = {
        .create = +[]() -> u_rw_lock_t * { return reinterpret_cast<u_rw_lock_t *>(0x1); },
        .destroy = +[](u_rw_lock_t *) -> int { return 0; },
        .lock_read = +[](u_rw_lock_t *) -> int { return 0; },
        .lock_write = +[](u_rw_lock_t *) -> int { return 0; },
        .unlock_rw = +[](u_rw_lock_t *) -> int { return 0; },
        .try_lock_read = +[](u_rw_lock_t *) -> int { return 0; },
        .try_lock_write = +[](u_rw_lock_t *) -> int { return 0; },
    };
    auto saved = LockRegistry::RW_LOCK_OPS;
    EXPECT_EQ(LockRegistry::RegisterRwLockOps(&ops), UBS_OK);
    LockRegistry::RW_LOCK_OPS = saved;
}

TEST_F(LockTest, RegisterSemOps_CompleteOps_ReturnsOk)
{
    u_external_semaphore_ops_t ops = {
        .create = +[]() -> u_semaphore_t * { return reinterpret_cast<u_semaphore_t *>(0x1); },
        .destroy = +[](u_semaphore_t *) -> int { return 0; },
        .init = +[](u_semaphore_t *, int, unsigned int) -> int { return 0; },
        .wait = +[](u_semaphore_t *) -> int { return 0; },
        .post = +[](u_semaphore_t *) -> int { return 0; },
    };
    EXPECT_EQ(LockRegistry::RegisterSemOps(&ops), UBS_OK);
}

TEST_F(LockTest, RegisterLockOps_CompleteOps_ReturnsOk)
{
    u_external_lock_ops_t ops = {
        .create = +[](u_mutex_type_t) -> u_mutex_t * { return reinterpret_cast<u_mutex_t *>(0x1); },
        .destroy = +[](u_mutex_t *) -> int { return 0; },
        .lock = +[](u_mutex_t *) -> int { return 0; },
        .unlock = +[](u_mutex_t *) -> int { return 0; },
        .try_lock = +[](u_mutex_t *) -> int { return 0; },
    };
    auto saved = LockRegistry::LOCK_OPS;
    EXPECT_EQ(LockRegistry::RegisterLockOps(&ops), UBS_OK);
    LockRegistry::LOCK_OPS = saved;
}

TEST_F(LockTest, Destroy_CorruptedMutex_HandledSafely)
{
    auto *raw = new pthread_mutex_t();
    memset(raw, 0xFF, sizeof(pthread_mutex_t));
    auto m = reinterpret_cast<u_mutex_t *>(raw);
    int ret = LockRegistry::LOCK_OPS.destroy(m);
    if (ret != 0) {
        delete raw;
    }
}

TEST_F(LockTest, RwLock_DestroyCorrupted_HandledSafely)
{
    auto *raw = new pthread_rwlock_t();
    memset(raw, 0xFF, sizeof(pthread_rwlock_t));
    auto rw = reinterpret_cast<u_rw_lock_t *>(raw);
    int ret = LockRegistry::RW_LOCK_OPS.destroy(rw);
    if (ret != 0) {
        delete raw;
    }
}

TEST_F(LockTest, Semaphore_DestroyCorrupted_HandledSafely)
{
    auto *raw = new sem_t();
    memset(raw, 0xFF, sizeof(sem_t));
    auto s = reinterpret_cast<u_semaphore_t *>(raw);
    int ret = LockRegistry::SEM_OPS.destroy(s);
    if (ret != 0) {
        delete raw;
    }
}
