/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <gtest/gtest.h>
#include <thread>

#include "iobuf/allocator/ubsocket_alloc_fixed_size.h"

using namespace ock::ubs;
using namespace ock::ubs::allocator;

namespace {
/* 统计 callback 调用次数的 mock 回调，模拟 mlock 行为 */
struct CallbackCounter {
    std::atomic<uint32_t> add_count{0};
    std::atomic<uint32_t> remove_count{0};
    std::atomic<uint32_t> fail_after{0};

    Result operator()(uintptr_t address, uint64_t size, uint32_t flags)
    {
        if (flags == BlockExpandFlag::ADD) {
            if (fail_after.load() > 0 && add_count.load() >= fail_after.load()) {
                return UBS_ERROR;
            }
            if (mlock(reinterpret_cast<void *>(address), size) != 0) {
                return UBS_ERROR;
            }
            add_count.fetch_add(1);
        } else if (flags == BlockExpandFlag::REMOVE) {
            munlock(reinterpret_cast<void *>(address), size);
            remove_count.fetch_add(1);
        }
        return UBS_OK;
    }

    void Reset()
    {
        add_count.store(0);
        remove_count.store(0);
        fail_after.store(0);
    }
};

CallbackCounter g_callback_counter;

FixedAllocatorOptions MakeDefaultOptions()
{
    FixedAllocatorOptions opts;
    opts.pool_space_start_tb = 2;
    opts.pool_space_size_gb = 4;
    opts.block_size_mb = 8;
    opts.init_block_count = 2;
    opts.mini_block_size_kb = 8;
    opts.async_expand = false;
    opts.expanded_callback = [](uintptr_t a, uint64_t s, uint32_t f) {
        return g_callback_counter(a, s, f);
    };
    return opts;
}

/*
 * FixedAllocator::GetTC() 使用 static thread_local FixedThreadCache，
 * 其构造时传入的 gc_ 指针只在当前线程首次 GetTC() 调用时生效。
 *
 * 规避策略：每个用例在新线程中执行 Allocate/Free 业务逻辑，
 * 新线程会重新构造 tc，使用当前 FixedAllocator 的 gc_ 指针。
 * 线程退出时 tc 自动析构。
 *
 * 主线程仅做 Init/UnInit 和结果断言，不调用 Allocate/Free。
 */
template <typename F>
void RunInWorker(F &&body)
{
    std::thread t(std::forward<F>(body));
    t.join();
}
} /* namespace */

/* =========================================================================
 * 1. FixedAllocatorOptions 校验与初始化
 *    Init/UnInit 在主线程执行，不触发 Allocate/Free
 * ========================================================================= */
class AllocatorInitTest : public testing::Test {
protected:
    FixedAllocator alloc_;
    FixedAllocatorOptions opts_{MakeDefaultOptions()};
    void SetUp() override
    {
        g_callback_counter.Reset();
    }
    void TearDown() override
    {
        alloc_.UnInit();
    }
};

TEST_F(AllocatorInitTest, InitWithDefaultOptions)
{
    ASSERT_EQ(alloc_.Init(opts_), UBS_OK);
}

TEST_F(AllocatorInitTest, RepeatedInit)
{
    ASSERT_EQ(alloc_.Init(opts_), UBS_OK);
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
}

TEST_F(AllocatorInitTest, UnInitWithoutInit)
{
    alloc_.UnInit();
    SUCCEED();
}

TEST_F(AllocatorInitTest, NullCallbackFails)
{
    opts_.expanded_callback = nullptr;
    EXPECT_EQ(alloc_.Init(opts_), UBS_ERROR);
}

TEST_F(AllocatorInitTest, InvalidStartTbFallsBack)
{
    opts_.pool_space_start_tb = 200;
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
}

TEST_F(AllocatorInitTest, InvalidBlockSizeMbFallsBack)
{
    opts_.block_size_mb = 1;
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
    alloc_.UnInit();

    opts_ = MakeDefaultOptions();
    opts_.block_size_mb = 256;
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
}

TEST_F(AllocatorInitTest, InvalidMiniBlockSizeFallsBack)
{
    opts_.mini_block_size_kb = 1;
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
    alloc_.UnInit();

    opts_ = MakeDefaultOptions();
    opts_.mini_block_size_kb = 512;
    EXPECT_EQ(alloc_.Init(opts_), UBS_OK);
}

TEST_F(AllocatorInitTest, InitBlockCountTooLargeFails)
{
    opts_.pool_space_size_gb = 2;
    opts_.block_size_mb = 64;
    opts_.init_block_count = 64;
    EXPECT_EQ(alloc_.Init(opts_), UBS_ERROR);
}

TEST_F(AllocatorInitTest, ExpandCallbackFailureFails)
{
    g_callback_counter.fail_after.store(1);
    EXPECT_EQ(alloc_.Init(opts_), UBS_ERROR);
}

/* =========================================================================
 * 2-7. 功能测试：每个用例在新线程中执行业务逻辑
 * ========================================================================= */
class AllocatorFunctionalTest : public testing::Test {
protected:
    FixedAllocator alloc_;
    FixedAllocatorOptions opts_{MakeDefaultOptions()};
    static constexpr uint64_t MINI_BLOCK_SIZE = 8 * 1024ULL;

    void SetUp() override
    {
        g_callback_counter.Reset();
        ASSERT_EQ(alloc_.Init(opts_), UBS_OK);
    }
    void TearDown() override
    {
        alloc_.UnInit();
    }
};

/* 未 Init 时 Allocate 应返回 false */
TEST(AllocatorNoInitTest, AllocateWithoutInitReturnsFalse)
{
    FixedAllocator a;
    bool ok = true;
    uintptr_t addr = 0;
    RunInWorker([&]() { ok = a.Allocate(addr); });
    EXPECT_FALSE(ok);
}

TEST(AllocatorNoInitTest, FreeWithoutInitNoCrash)
{
    FixedAllocator a;
    RunInWorker([&]() { a.Free(0x1000); });
    SUCCEED();
}

/* ---- 2. 基础 Allocate/Free ---- */
TEST_F(AllocatorFunctionalTest, AllocateFreeReallocate)
{
    bool ok1 = false, ok2 = false;
    uintptr_t addr1 = 0, addr2 = 0;
    RunInWorker([&]() {
        ok1 = alloc_.Allocate(addr1);
        if (ok1) {
            alloc_.Free(addr1);
            ok2 = alloc_.Allocate(addr2);
            if (ok2) {
                alloc_.Free(addr2);
            }
        }
    });
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_NE(addr1, 0ULL);
    EXPECT_NE(addr2, 0ULL);
}

TEST_F(AllocatorFunctionalTest, FreeZeroIgnored)
{
    RunInWorker([&]() { alloc_.Free(0); });
    SUCCEED();
}

TEST_F(AllocatorFunctionalTest, ConsecutiveAllocDistinctAddresses)
{
    constexpr uint32_t N = 32;
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    RunInWorker([&]() {
        for (uint32_t i = 0; i < N; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        for (auto a : addrs) {
            alloc_.Free(a);
        }
    });
    EXPECT_TRUE(all_ok);
    std::set<uintptr_t> unique(addrs.begin(), addrs.end());
    EXPECT_EQ(unique.size(), N);
}

/* ---- 3. InRange 检查 ---- */
TEST_F(AllocatorFunctionalTest, InRangeForAllocatedAddr)
{
    bool in_range = false;
    uintptr_t a = 0;
    RunInWorker([&]() {
        if (alloc_.Allocate(a)) {
            in_range = alloc_.InRange(a, MINI_BLOCK_SIZE);
            alloc_.Free(a);
        }
    });
    EXPECT_TRUE(in_range);
}

TEST_F(AllocatorFunctionalTest, InRangeRejectsOutOfRange)
{
    bool result = true;
    RunInWorker([&]() { result = alloc_.InRange(0x1000, 64); });
    EXPECT_FALSE(result);
}

TEST_F(AllocatorFunctionalTest, InRangeRejectsZero)
{
    bool r1 = true, r2 = true;
    RunInWorker([&]() {
        r1 = alloc_.InRange(0, 64);
        r2 = alloc_.InRange(0x1000, 0);
    });
    EXPECT_FALSE(r1);
    EXPECT_FALSE(r2);
}

TEST_F(AllocatorFunctionalTest, BatchInRangeAllValid)
{
    constexpr uint16_t N = 4;
    uintptr_t addrs[N] = {0};
    uint64_t sizes[N] = {0};
    bool all_alloc = true;
    bool in_range = false;
    RunInWorker([&]() {
        for (uint16_t i = 0; i < N; i++) {
            if (!alloc_.Allocate(addrs[i])) {
                all_alloc = false;
                break;
            }
            sizes[i] = MINI_BLOCK_SIZE;
        }
        if (all_alloc) {
            in_range = alloc_.BatchInRange(N, addrs, sizes);
            for (uint16_t i = 0; i < N; i++) {
                alloc_.Free(addrs[i]);
            }
        }
    });
    EXPECT_TRUE(all_alloc);
    EXPECT_TRUE(in_range);
}

TEST_F(AllocatorFunctionalTest, BatchInRangeRejectsInvalid)
{
    constexpr uint16_t N = 2;
    uintptr_t addrs[N] = {0x1000, 0x2000};
    uint64_t sizes[N] = {64, 64};
    bool result = true;
    RunInWorker([&]() { result = alloc_.BatchInRange(N, addrs, sizes); });
    EXPECT_FALSE(result);
}

TEST_F(AllocatorFunctionalTest, BatchInRangeRejectsZeroArgs)
{
    bool r1 = true, r2 = true, r3 = true;
    RunInWorker([&]() {
        r1 = alloc_.BatchInRange(0, nullptr, nullptr);
        uintptr_t a = 0x1000;
        uint64_t s = 64;
        r2 = alloc_.BatchInRange(1, nullptr, &s);
        r3 = alloc_.BatchInRange(1, &a, nullptr);
    });
    EXPECT_FALSE(r1);
    EXPECT_FALSE(r2);
    EXPECT_FALSE(r3);
}

/* ---- 4. TC 与 GC 链表维护 ---- */
TEST_F(AllocatorFunctionalTest, AllocateBatchDepletesTCCache)
{
    constexpr uint32_t BATCH = FIX_GC_BATCH_ALLOC_SIZE;
    std::vector<uintptr_t> addrs;
    bool batch_ok = true;
    bool extra_ok = false;
    uintptr_t extra = 0;
    RunInWorker([&]() {
        for (uint32_t i = 0; i < BATCH; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                batch_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (batch_ok) {
            extra_ok = alloc_.Allocate(extra);
            for (auto a : addrs) {
                alloc_.Free(a);
            }
            if (extra_ok) {
                alloc_.Free(extra);
            }
        }
    });
    EXPECT_TRUE(batch_ok);
    EXPECT_TRUE(extra_ok);
    EXPECT_NE(extra, 0ULL);
}

TEST_F(AllocatorFunctionalTest, FreeThresholdTriggersFreeToGC)
{
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    bool reuse_ok = false;
    uintptr_t reuse = 0;
    RunInWorker([&]() {
        for (uint32_t i = 0; i < 36; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto a : addrs) {
                alloc_.Free(a);
            }
            reuse_ok = alloc_.Allocate(reuse);
            if (reuse_ok) {
                alloc_.Free(reuse);
            }
        }
    });
    EXPECT_TRUE(all_ok);
    EXPECT_TRUE(reuse_ok);
    EXPECT_NE(reuse, 0ULL);
}

TEST_F(AllocatorFunctionalTest, FreeBuildsNextNAndNextNMChain)
{
    std::vector<uintptr_t> addrs;
    std::vector<uintptr_t> reallocated;
    bool all_ok = true;
    RunInWorker([&]() {
        for (uint32_t i = 0; i < 16; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto it = addrs.rbegin(); it != addrs.rend(); ++it) {
                alloc_.Free(*it);
            }
            for (uint32_t i = 0; i < 16; i++) {
                uintptr_t a = 0;
                if (!alloc_.Allocate(a)) {
                    all_ok = false;
                    break;
                }
                reallocated.push_back(a);
            }
            for (auto a : reallocated) {
                alloc_.Free(a);
            }
        }
    });
    ASSERT_TRUE(all_ok);
    std::set<uintptr_t> expected(addrs.begin(), addrs.end());
    std::set<uintptr_t> actual(reallocated.begin(), reallocated.end());
    EXPECT_EQ(actual, expected);
}

/* ---- 5. DirectAllocate / DirectFree ---- */
TEST_F(AllocatorFunctionalTest, DirectAllocateAndFree)
{
    bool ok = false;
    bool in_range = false;
    uintptr_t a;
    RunInWorker([&]() {
        ok = alloc_.DirectAllocate(4096, a);
        if (ok) {
            in_range = alloc_.InRange(a, 4096);
            alloc_.DirectFree(a);
        }
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(in_range);
    EXPECT_NE(a, 0ULL);
}

TEST_F(AllocatorFunctionalTest, DirectAllocateZeroSizeFails)
{
    bool ok = true;
    uintptr_t a;
    RunInWorker([&]() { ok = alloc_.DirectAllocate(0, a); });
    EXPECT_FALSE(ok);
}

TEST_F(AllocatorFunctionalTest, DirectAllocateUnalignedSizeAligned)
{
    bool ok = false;
    uintptr_t a;
    RunInWorker([&]() {
        ok = alloc_.DirectAllocate(100, a);
        if (ok) {
            alloc_.DirectFree(a);
        }
    });
    EXPECT_TRUE(ok);
    EXPECT_NE(a, 0ULL);
    EXPECT_EQ(a % 4096, 0ULL);
}

TEST_F(AllocatorFunctionalTest, MultipleDirectAllocateFree)
{
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    bool reuse_ok = false;
    uintptr_t reuse;
    RunInWorker([&]() {
        for (int i = 0; i < 10; i++) {
            uintptr_t a = 100;
            if (!alloc_.DirectAllocate(8192, a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto a : addrs) {
                alloc_.DirectFree(a);
            }
            reuse_ok = alloc_.DirectAllocate(8192, reuse);
            if (reuse_ok) {
                alloc_.DirectFree(reuse);
            }
        }
    });
    EXPECT_TRUE(all_ok);
    EXPECT_TRUE(reuse_ok);
    EXPECT_NE(reuse, 0ULL);
}

TEST_F(AllocatorFunctionalTest, DirectAllocateLargeTriggersExpand)
{
    bool ok = false;
    uintptr_t a;
    RunInWorker([&]() {
        ok = alloc_.DirectAllocate(8 * 1024 * 1024, a);
        if (ok) {
            alloc_.DirectFree(a);
        }
    });
    EXPECT_TRUE(ok);
    EXPECT_NE(a, 0ULL);
}

TEST_F(AllocatorFunctionalTest, DirectFreeInvalidAddressNoCrash)
{
    RunInWorker([&]() { alloc_.DirectFree(0x1234); });
    SUCCEED();
}

TEST_F(AllocatorFunctionalTest, DirectFreeMergesAdjacentBlocks)
{
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    bool merged_ok = false;
    uintptr_t merged;
    RunInWorker([&]() {
        for (int i = 0; i < 4; i++) {
            uintptr_t a = 100;
            if (!alloc_.DirectAllocate(4096, a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto a : addrs) {
                alloc_.DirectFree(a);
            }
            merged_ok = alloc_.DirectAllocate(16 * 1024, merged);
            if (merged_ok) {
                alloc_.DirectFree(merged);
            }
        }
    });
    EXPECT_TRUE(all_ok);
    EXPECT_TRUE(merged_ok);
    EXPECT_NE(merged, 0ULL);
}

/* ---- 6. 多线程并发 ---- */
TEST_F(AllocatorFunctionalTest, MultiThreadAllocateFree)
{
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = 50;
    std::vector<std::vector<uintptr_t>> results(THREADS);
    bool success = true;
    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < PER_THREAD; i++) {
                uintptr_t a = 0;
                if (alloc_.Allocate(a)) {
                    results[t].push_back(a);
                    *reinterpret_cast<volatile uint8_t *>(a + sizeof(FixedMiniBlock)) = static_cast<uint8_t>(t);
                } else {
                    success = false;
                }
            }
            for (auto a : results[t]) {
                alloc_.Free(a);
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    EXPECT_TRUE(success);
}

/* ---- 7. MiniBlock 链表结构验证 ---- */
TEST_F(AllocatorFunctionalTest, InitialChainContiguousAddresses)
{
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    RunInWorker([&]() {
        for (uint32_t i = 0; i < 32; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto a : addrs) {
                alloc_.Free(a);
            }
        }
    });
    ASSERT_TRUE(all_ok);
    bool ascending = true;
    for (size_t i = 1; i < addrs.size(); i++) {
        if (addrs[i] <= addrs[i - 1]) {
            ascending = false;
            break;
        }
    }
    EXPECT_TRUE(ascending);
}

TEST_F(AllocatorFunctionalTest, NextNJumpsFourSteps)
{
    std::vector<uintptr_t> batch1;
    bool all_ok = true;
    RunInWorker([&]() {
        for (int i = 0; i < 16; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            batch1.push_back(a);
        }
        if (all_ok) {
            for (auto a : batch1) {
                alloc_.Free(a);
            }
        }
    });
    ASSERT_TRUE(all_ok);
    uintptr_t stride = MINI_BLOCK_SIZE;
    for (size_t i = 1; i < batch1.size(); i++) {
        EXPECT_EQ(batch1[i] - batch1[i - 1], stride);
    }
}

TEST_F(AllocatorFunctionalTest, ReverseFreeThenAllocateIsLIFO)
{
    std::vector<uintptr_t> addrs;
    std::vector<uintptr_t> reversed;
    bool all_ok = true;
    RunInWorker([&]() {
        for (int i = 0; i < 8; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (auto a : addrs) {
                alloc_.Free(a);
            }
            for (int i = 0; i < 8; i++) {
                uintptr_t a = 0;
                if (!alloc_.Allocate(a)) {
                    all_ok = false;
                    break;
                }
                reversed.push_back(a);
            }
            if (all_ok) {
                for (auto a : reversed) {
                    alloc_.Free(a);
                }
            }
        }
    });
    ASSERT_TRUE(all_ok);
    bool is_reverse = true;
    for (size_t i = 0; i < addrs.size(); i++) {
        if (reversed[i] != addrs[addrs.size() - 1 - i]) {
            is_reverse = false;
            break;
        }
    }
    EXPECT_TRUE(is_reverse);
}

TEST_F(AllocatorFunctionalTest, FreeToGCSeversNextNMChain)
{
    std::vector<uintptr_t> addrs;
    bool all_ok = true;
    bool extra_ok = false;
    uintptr_t extra = 0;
    RunInWorker([&]() {
        for (int i = 0; i < 40; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                all_ok = false;
                break;
            }
            addrs.push_back(a);
        }
        if (all_ok) {
            for (int i = 0; i < 20; i++) {
                alloc_.Free(addrs[i]);
            }
            extra_ok = alloc_.Allocate(extra);
            if (extra_ok) {
                alloc_.Free(extra);
            }
            for (int i = 20; i < 40; i++) {
                alloc_.Free(addrs[i]);
            }
        }
    });
    EXPECT_TRUE(all_ok);
    EXPECT_TRUE(extra_ok);
    EXPECT_NE(extra, 0ULL);
}

TEST_F(AllocatorFunctionalTest, AllocateFromGCAppendsToTail)
{
    std::vector<uintptr_t> first_batch;
    uintptr_t first_of_new = 0;
    bool batch_ok = true;
    bool new_ok = false;
    RunInWorker([&]() {
        for (int i = 0; i < 16; i++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                batch_ok = false;
                break;
            }
            first_batch.push_back(a);
        }
        if (batch_ok) {
            new_ok = alloc_.Allocate(first_of_new);
            for (auto a : first_batch) {
                alloc_.Free(a);
            }
            if (new_ok) {
                alloc_.Free(first_of_new);
            }
        }
    });
    EXPECT_TRUE(batch_ok);
    EXPECT_TRUE(new_ok);
    EXPECT_NE(first_of_new, 0ULL);
    EXPECT_EQ(std::find(first_batch.begin(), first_batch.end(), first_of_new), first_batch.end());
}

TEST_F(AllocatorFunctionalTest, StressAllocateFreeCycle)
{
    bool success = true;
    RunInWorker([&]() {
        constexpr uint32_t ITER = 1000;
        std::vector<uintptr_t> addrs;
        addrs.reserve(64);
        for (uint32_t iter = 0; iter < ITER; iter++) {
            uintptr_t a = 0;
            if (!alloc_.Allocate(a)) {
                success = false;
                break;
            }
            addrs.push_back(a);
            if (addrs.size() == 64) {
                for (auto x : addrs) {
                    alloc_.Free(x);
                }
                addrs.clear();
            }
        }
        for (auto x : addrs) {
            alloc_.Free(x);
        }
    });
    EXPECT_TRUE(success);
}

TEST_F(AllocatorFunctionalTest, BatchAllocateFreeStub)
{
    bool alloc_ok = true;
    RunInWorker([&]() {
        uintptr_t *addrs = nullptr;
        alloc_ok = alloc_.BatchAllocate(8, addrs);
        uintptr_t tmp[8] = {0};
        alloc_.BatchFree(8, tmp);
        alloc_.BatchFree(0, nullptr);
    });
    EXPECT_FALSE(alloc_ok);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
