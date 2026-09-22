/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <sys/resource.h>
#include <algorithm>
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "common/ubsocket_set.h"
#include "common/ubsocket_lock.h"

using namespace ock::ubs;

// ==================== Test fixture types ====================

struct TestItem : public Referable {
    int value = 0;
    std::atomic<bool> deleted{false};
};

struct CountItem : public Referable {
    CountItem()
    {
        ++alive;
    }
    ~CountItem()
    {
        --alive;
    }
    static std::atomic<int> alive;
};
std::atomic<int> CountItem::alive{0};

struct ThreadItem : public Referable {
    int value = 0;
};

// ==================== ArraySet Tests ====================

class ArraySetTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        LockRegistry::RegisterDefaultOps();
    }

    void SetUp() override
    {
        ArraySet<TestItem>::GetInstance().ReleaseAll();
    }

    void TearDown() override
    {
        ArraySet<TestItem>::GetInstance().ReleaseAll();
    }
};

// --- Init ---

TEST_F(ArraySetTest, Init_SetsCapacity)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.ReleaseAll();
    // Reset capacity for testing (force re-init by clearing internal state
    // via ReleaseAll; but Init only works once unless capacity remains 0)
    // Since capacity persists, we just verify Init returns 0
    int ret = set.Init();
    EXPECT_EQ(ret, 0);
    EXPECT_GT(set.Capacity(), 0u);
}

TEST_F(ArraySetTest, Init_DoubleInitReturnsZero)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    int ret = set.Init();
    EXPECT_EQ(ret, 0);
}

TEST_F(ArraySetTest, Capacity_AfterInit)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    uint32_t cap = set.Capacity();
    EXPECT_GT(cap, 0u);
    /* issue #44: the ceiling follows RLIMIT_NOFILE up to 1M; 65536 was a hidden wall */
    struct rlimit rl {};
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &rl), 0);
    EXPECT_LE(cap, 1u << 20);
    EXPECT_EQ(cap, std::min(static_cast<uint32_t>(rl.rlim_cur), 1u << 20));
}

/* issue #44: capacity used to be min(rlimit, 65536); a process allowed 1M fds still could not register
 * fd >= 65536 and only found out at the first ubs_poll. Capacity must follow the rlimit up to 1M. */
struct FdCapacityProbe : public Referable {};

TEST_F(ArraySetTest, Capacity_FollowsRlimitAboveLegacy65536)
{
    struct rlimit saved {};
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &saved), 0);
    constexpr rlim_t WANT = 70000;
    if (saved.rlim_max != RLIM_INFINITY && saved.rlim_max < WANT) {
        GTEST_SKIP() << "hard nofile limit " << saved.rlim_max << " below " << WANT;
    }
    struct rlimit rl = saved;
    rl.rlim_cur = WANT;
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &rl), 0);

    auto &set = ArraySet<FdCapacityProbe>::GetInstance();
    const int init_ret = set.Init();
    (void)setrlimit(RLIMIT_NOFILE, &saved);
    ASSERT_EQ(init_ret, 0);
    EXPECT_EQ(set.Capacity(), static_cast<uint32_t>(WANT));

    auto *probe = new FdCapacityProbe();
    const int last_fd = static_cast<int>(WANT) - 1;
    set.OverrideItem(last_fd, probe);
    EXPECT_EQ(set.GetItem(last_fd).Get(), probe);
    EXPECT_EQ(set.GetItem(static_cast<int>(WANT)).Get(), nullptr);
    set.OverrideItem(last_fd, nullptr);
    set.DrainDeferredRelease();
}

// --- deferred release: notifier + counted drain (issue #49) ---

static std::atomic<int> g_deferredNotifyCnt{0};
static void *g_deferredNotifyCtx = nullptr;
static void CountDeferredNotify(void *ctx)
{
    g_deferredNotifyCtx = ctx;
    ++g_deferredNotifyCnt;
}

/* 摘表把最后一个引用停进延迟队列时必须叫醒监听者：这是失败链路（不经 close/Retire）
 * 能被释放的唯一信号。无监听者时入队照旧。 */
TEST_F(ArraySetTest, EnqueueDeferred_NotifiesRegisteredListener)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    ASSERT_EQ(set.Init(), 0);
    g_deferredNotifyCnt = 0;
    g_deferredNotifyCtx = nullptr;
    int token = 0;
    set.SetDeferredNotifier(&CountDeferredNotify, &token);

    auto *item = new TestItem();
    set.OverrideItem(3, item);
    EXPECT_EQ(g_deferredNotifyCnt, 0); /* 登记不入队 */
    set.OverrideItem(3, nullptr);      /* 摘除：旧引用入队 → 通知 */
    EXPECT_EQ(g_deferredNotifyCnt, 1);
    EXPECT_EQ(g_deferredNotifyCtx, &token);
    EXPECT_EQ(set.DeferredCount(), 1u);

    set.SetDeferredNotifier(nullptr, nullptr);
    auto *item2 = new TestItem();
    set.OverrideItem(4, item2);
    set.OverrideItem(4, nullptr);
    EXPECT_EQ(g_deferredNotifyCnt, 1); /* 已卸载：不再通知，但照常入队 */
    EXPECT_EQ(set.DeferredCount(), 2u);
    EXPECT_EQ(set.DrainDeferredRelease(), 2u);
    EXPECT_EQ(set.DeferredCount(), 0u);
}

TEST_F(ArraySetTest, DrainDeferredRelease_ReturnsCountAndDestroys)
{
    auto &set = ArraySet<CountItem>::GetInstance();
    ASSERT_EQ(set.Init(), 0);
    CountItem::alive = 0;
    for (int i = 10; i < 13; ++i) {
        set.OverrideItem(i, new CountItem());
    }
    EXPECT_EQ(CountItem::alive, 3);
    for (int i = 10; i < 13; ++i) {
        set.OverrideItem(i, nullptr); /* 返回的引用随即丢弃：对象只剩队列里那份 */
    }
    EXPECT_EQ(CountItem::alive, 3); /* 排空前不死 */
    EXPECT_EQ(set.DrainDeferredRelease(), 3u);
    EXPECT_EQ(CountItem::alive, 0);
    EXPECT_EQ(set.DrainDeferredRelease(), 0u);
}

// --- GetItem ---

TEST_F(ArraySetTest, GetItem_NegativeIndexReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    Ref<TestItem> ref = set.GetItem(-1);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, GetItem_OutOfRangeReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    Ref<TestItem> ref = set.GetItem(static_cast<int>(set.Capacity() + 100));
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, GetItem_ValidIndexReturnsItem)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item = new TestItem();
    item->value = 42;
    set.OverrideItem(0, item);
    // OverrideItem already increased refcount; array holds the reference.
    // Do NOT DecreaseRef on the raw pointer here.

    Ref<TestItem> ref = set.GetItem(0);
    ASSERT_NE(ref.Get(), nullptr);
    EXPECT_EQ(ref->value, 42);
}

// --- OverrideItem ---

TEST_F(ArraySetTest, OverrideItem_NegativeIndexReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item = new TestItem();
    Ref<TestItem> old = set.OverrideItem(-1, item);
    EXPECT_EQ(old.Get(), nullptr);
    delete item; // Not inserted, manually clean up
}

TEST_F(ArraySetTest, OverrideItem_OutOfRangeReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item = new TestItem();
    Ref<TestItem> old = set.OverrideItem(static_cast<int>(set.Capacity() + 100), item);
    EXPECT_EQ(old.Get(), nullptr);
    delete item;
}

TEST_F(ArraySetTest, OverrideItem_InsertsAndReturnsOld)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item1 = new TestItem();
    item1->value = 10;
    TestItem *item2 = new TestItem();
    item2->value = 20;

    Ref<TestItem> old1 = set.OverrideItem(1, item1);
    EXPECT_EQ(old1.Get(), nullptr);

    Ref<TestItem> old2 = set.OverrideItem(1, item2);
    ASSERT_NE(old2.Get(), nullptr);
    EXPECT_EQ(old2->value, 10);
    // old2 Ref holds item1; released when old2 goes out of scope

    Ref<TestItem> current = set.GetItem(1);
    ASSERT_NE(current.Get(), nullptr);
    EXPECT_EQ(current->value, 20);
}

TEST_F(ArraySetTest, OverrideItem_NullItemRemovesExisting)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item = new TestItem();
    item->value = 99;
    set.OverrideItem(5, item);

    // Override with nullptr should remove
    Ref<TestItem> old = set.OverrideItem(5, nullptr);
    ASSERT_NE(old.Get(), nullptr);
    EXPECT_EQ(old->value, 99);
    // old Ref destructor will delete item

    Ref<TestItem> check = set.GetItem(5);
    EXPECT_EQ(check.Get(), nullptr);
}

// --- RemoveItem ---

TEST_F(ArraySetTest, RemoveItem_RemovesAndReturnsItem)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    TestItem *item = new TestItem();
    item->value = 77;
    set.OverrideItem(3, item);

    Ref<TestItem> removed = set.RemoveItem(3);
    ASSERT_NE(removed.Get(), nullptr);
    EXPECT_EQ(removed->value, 77);

    Ref<TestItem> check = set.GetItem(3);
    EXPECT_EQ(check.Get(), nullptr);
}

TEST_F(ArraySetTest, RemoveItem_NegativeIndexReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    Ref<TestItem> removed = set.RemoveItem(-1);
    EXPECT_EQ(removed.Get(), nullptr);
}

TEST_F(ArraySetTest, RemoveItem_OutOfRangeReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    Ref<TestItem> removed = set.RemoveItem(static_cast<int>(set.Capacity() + 100));
    EXPECT_EQ(removed.Get(), nullptr);
}

TEST_F(ArraySetTest, RemoveItem_NonExistentReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    Ref<TestItem> removed = set.RemoveItem(0);
    EXPECT_EQ(removed.Get(), nullptr);
}

// --- ReleaseAll ---

TEST_F(ArraySetTest, ReleaseAll_ClearsAllItems)
{
    auto &set = ArraySet<CountItem>::GetInstance();
    set.Init();
    CountItem::alive = 0;

    for (int i = 0; i < 10; ++i) {
        CountItem *item = new CountItem();
        set.OverrideItem(i, item);
    }
    EXPECT_EQ(set.Size(), 10u);

    set.ReleaseAll();
    EXPECT_EQ(set.Size(), 0u);
    EXPECT_EQ(CountItem::alive.load(), 0);
}

// --- Size ---

TEST_F(ArraySetTest, Size_EmptyReturnsZero)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    set.ReleaseAll();
    EXPECT_EQ(set.Size(), 0u);
}

TEST_F(ArraySetTest, Size_AfterInsertReturnsCorrectCount)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    set.ReleaseAll();

    TestItem *items[5];
    for (int i = 0; i < 5; ++i) {
        items[i] = new TestItem();
        items[i]->value = i;
        set.OverrideItem(i * 2, items[i]);
    }
    EXPECT_EQ(set.Size(), 5u);
    set.ReleaseAll();
}

// --- ForEach ---

TEST_F(ArraySetTest, ForEach_EmptySetCallsNothing)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    set.ReleaseAll();

    int callCount = 0;
    set.ForEach([&callCount](int, TestItem *) { callCount++; });
    EXPECT_EQ(callCount, 0);
}

TEST_F(ArraySetTest, ForEach_VisitsAllItems)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    set.Init();
    set.ReleaseAll();

    for (int i = 0; i < 3; ++i) {
        TestItem *item = new TestItem();
        item->value = i * 10;
        set.OverrideItem(i, item);
    }

    int sum = 0;
    int count = 0;
    set.ForEach([&sum, &count](int fd, TestItem *item) {
        sum += item->value;
        count++;
    });
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 30);
    set.ReleaseAll();
}

// --- Thread safety ---

TEST_F(ArraySetTest, ThreadSafety_ConcurrentGetItem)
{
    auto &set = ArraySet<ThreadItem>::GetInstance();
    set.Init();
    set.ReleaseAll();

    ThreadItem *item = new ThreadItem();
    item->value = 100;
    set.OverrideItem(0, item);

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&set, &errors]() {
            for (int i = 0; i < 1000; ++i) {
                Ref<ThreadItem> ref = set.GetItem(0);
                if (ref.Get() != nullptr && ref->value != 100) {
                    errors++;
                }
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    EXPECT_EQ(errors.load(), 0);
    set.ReleaseAll();
}

TEST_F(ArraySetTest, ThreadSafety_ConcurrentOverrideAndGet)
{
    auto &set = ArraySet<ThreadItem>::GetInstance();
    set.Init();
    set.ReleaseAll();

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;

    // Writer thread
    threads.emplace_back([&set]() {
        for (int i = 0; i < 100; ++i) {
            ThreadItem *item = new ThreadItem();
            item->value = i;
            set.OverrideItem(0, item);
        }
    });

    // Reader threads
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&set, &errors]() {
            for (int i = 0; i < 1000; ++i) {
                Ref<ThreadItem> ref = set.GetItem(0);
                // Just verify we don't crash and values are reasonable
                if (ref.Get() != nullptr && ref->value < 0) {
                    errors++;
                }
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }
    EXPECT_EQ(errors.load(), 0);
    set.ReleaseAll();
}

// ==================== ArraySet: edge cases ====================

TEST_F(ArraySetTest, GetItem_OutOfRange_ReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto ref = set.GetItem(-1);
    EXPECT_EQ(ref.Get(), nullptr);
    ref = set.GetItem(99999);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, OverrideItem_OutOfRange_ReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto ref = set.OverrideItem(-1, nullptr);
    EXPECT_EQ(ref.Get(), nullptr);
    ref = set.OverrideItem(99999, nullptr);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, RemoveItem_OutOfRange_ReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto ref = set.RemoveItem(-1);
    EXPECT_EQ(ref.Get(), nullptr);
    ref = set.RemoveItem(99999);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, OverrideItem_NullNewItem_NoOp)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto ref = set.OverrideItem(0, nullptr);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, RemoveItem_EmptySlot_ReturnsNullRef)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto ref = set.RemoveItem(0);
    EXPECT_EQ(ref.Get(), nullptr);
}

TEST_F(ArraySetTest, DrainDeferredRelease_NoMutex_NoCrash)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto *saved = set.deferred_mtx_;
    set.deferred_mtx_ = nullptr;
    set.DrainDeferredRelease();
    set.deferred_mtx_ = saved;
}

TEST_F(ArraySetTest, EnqueueDeferred_NoMutex_NoCrash)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    auto *saved = set.deferred_mtx_;
    set.deferred_mtx_ = nullptr;
    set.EnqueueDeferred(nullptr);
    set.deferred_mtx_ = saved;
}

TEST_F(ArraySetTest, Size_ZeroWhenEmpty)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    EXPECT_EQ(set.Size(), 0u);
}

TEST_F(ArraySetTest, Capacity_NonZero)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    EXPECT_GT(set.Capacity(), 0u);
}

TEST_F(ArraySetTest, ForEach_EmptySet_NoCalls)
{
    auto &set = ArraySet<TestItem>::GetInstance();
    uint32_t count = 0;
    set.ForEach([&count](int fd, TestItem *p) { count++; });
    EXPECT_EQ(count, 0u);
}
