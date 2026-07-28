#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "ubsocket_atomic_fd_set.h"

using namespace ock::ubs;

struct Tracked {
    int id;
    static inline int alive = 0;
    Tracked(int i = 0) noexcept : id{i}
    {
        alive++;
    }
    ~Tracked()
    {
        alive--;
    }
    Tracked(const Tracked &) = delete;
    Tracked &operator=(const Tracked &) = delete;
};

// ---------- AtomicFd basic tests ----------

TEST(AtomicFdTest, DefaultConstructIsEmpty)
{
    AtomicFd<Tracked> u;
    EXPECT_FALSE(static_cast<bool>(u));
}

TEST(AtomicFdTest, ConstructWithPlacement)
{
    alignas(uint64_t) uint64_t storage{};
    EXPECT_EQ(Tracked::alive, 0);
    {
        AtomicFd<Tracked> u{&storage, new Tracked(42)};
        EXPECT_TRUE(static_cast<bool>(u));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, ConstructWithNullPlacement)
{
    Tracked *obj = new Tracked(42);
    EXPECT_EQ(Tracked::alive, 1);
    AtomicFd<Tracked> u{nullptr, obj};
    EXPECT_FALSE(static_cast<bool>(u));
    delete obj;
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, MoveConstructorTransfersOwnership)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1{&storage, new Tracked(1)};
        EXPECT_TRUE(static_cast<bool>(u1));

        AtomicFd<Tracked> u2{std::move(u1)};
        EXPECT_TRUE(static_cast<bool>(u2));
        EXPECT_FALSE(static_cast<bool>(u1));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, CopyConstructorIncrementsRefCount)
{
    alignas(uint64_t) uint64_t storage{};
    EXPECT_EQ(Tracked::alive, 0);
    {
        AtomicFd<Tracked> u1{&storage, new Tracked(7)};
        EXPECT_EQ(Tracked::alive, 1);
        {
            AtomicFd<Tracked> u2{u1};
            EXPECT_TRUE(static_cast<bool>(u1));
            EXPECT_TRUE(static_cast<bool>(u2));
            EXPECT_EQ(Tracked::alive, 1);
        }
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, MoveAssignment)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1{&storage, new Tracked(1)};
        AtomicFd<Tracked> u2;
        u2 = std::move(u1);
        EXPECT_TRUE(static_cast<bool>(u2));
        EXPECT_FALSE(static_cast<bool>(u1));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, CopyAssignment)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1{&storage, new Tracked(5)};
        AtomicFd<Tracked> u2;
        EXPECT_FALSE(static_cast<bool>(u2));
        u2 = u1;
        EXPECT_TRUE(static_cast<bool>(u1));
        EXPECT_TRUE(static_cast<bool>(u2));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, GetObj)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1{&storage, new Tracked(3)};
        EXPECT_EQ(Tracked::alive, 1);
        {
            auto u2 = AtomicFd<Tracked>::GetObj(&storage);
            EXPECT_TRUE(static_cast<bool>(u2));
            EXPECT_EQ(Tracked::alive, 1);
        }
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, GetObjNullReturnsEmpty)
{
    auto u = AtomicFd<Tracked>::GetObj(nullptr);
    EXPECT_FALSE(static_cast<bool>(u));
}

// ---------- MakeAtomicFdOn tests ----------

TEST(MakeAtomicFdOnTest, Basic)
{
    alignas(uint64_t) uint64_t storage{};
    EXPECT_EQ(Tracked::alive, 0);
    {
        auto u = MakeAtomicFdOn<Tracked>(&storage, 42);
        EXPECT_TRUE(static_cast<bool>(u));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(MakeAtomicFdOnTest, NullPlacementReturnsEmpty)
{
    EXPECT_EQ(Tracked::alive, 0);
    auto u = MakeAtomicFdOn<Tracked>(nullptr, 42);
    EXPECT_FALSE(static_cast<bool>(u));
    EXPECT_EQ(Tracked::alive, 0);
}

// ---------- Thread safety tests ----------

TEST(AtomicFdThreadTest, ConcurrentCopy)
{
    alignas(uint64_t) uint64_t storage{};
    static constexpr int kThreads = 8;
    static constexpr int kCopies = 1000;

    {
        AtomicFd<Tracked> root{&storage, new Tracked(0)};
        EXPECT_EQ(Tracked::alive, 1);

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&] {
                for (int i = 0; i < kCopies; i++) {
                    AtomicFd<Tracked> copy{root};
                    (void)copy;
                }
            });
        }
        for (auto &th : threads)
            th.join();

        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdThreadTest, ConcurrentGetObj)
{
    alignas(uint64_t) uint64_t storage{};
    static constexpr int kThreads = 8;
    static constexpr int kGets = 1000;

    {
        AtomicFd<Tracked> root{&storage, new Tracked(0)};

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&] {
                for (int i = 0; i < kGets; i++) {
                    auto u = AtomicFd<Tracked>::GetObj(&storage);
                    (void)u;
                }
            });
        }
        for (auto &th : threads)
            th.join();

        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

// ---------- AtomicFd additional tests ----------

TEST(AtomicFdTest, GetReturnsPointer)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u{&storage, new Tracked(99)};
        auto *p = u.Get();
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->id, 99);
    }
}

TEST(AtomicFdTest, GetReturnsNullWhenEmpty)
{
    AtomicFd<Tracked> u;
    EXPECT_EQ(u.Get(), nullptr);
}

TEST(AtomicFdTest, GetReturnsNonNullWhileRefCountPositive)
{
    alignas(uint64_t) uint64_t storage{};
    EXPECT_EQ(Tracked::alive, 0);
    {
        AtomicFd<Tracked> root{&storage, new Tracked(1)};
        EXPECT_EQ(Tracked::alive, 1);
        {
            AtomicFd<Tracked> copy{root};
            EXPECT_EQ(Tracked::alive, 1);
        }
        // copy destroyed, ref_count drops to 1, object still alive
        EXPECT_NE(root.Get(), nullptr);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, GetRefCountBasic)
{
    alignas(uint64_t) uint64_t storage{};
    EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 0);
    {
        AtomicFd<Tracked> u{&storage, new Tracked(7)};
        EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 1);
        {
            AtomicFd<Tracked> c1{u};
            EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 2);
            {
                AtomicFd<Tracked> c2{u};
                EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 3);
            }
            EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 2);
        }
        EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 1);
    }
    EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(&storage), 0);
}

TEST(AtomicFdTest, GetRefCountNullPlacement)
{
    EXPECT_EQ(AtomicFd<Tracked>::GetRefCount(nullptr), 0);
}

TEST(AtomicFdTest, CopyAssignmentReplaceExisting)
{
    alignas(uint64_t) uint64_t storage1{};
    alignas(uint64_t) uint64_t storage2{};
    {
        AtomicFd<Tracked> u1{&storage1, new Tracked(10)};
        {
            AtomicFd<Tracked> u2{&storage2, new Tracked(20)};
            EXPECT_EQ(Tracked::alive, 2);
            u2 = u1; // u2 releases old (Tracked(20)), acquires new reference
            EXPECT_EQ(Tracked::alive, 1);
            EXPECT_TRUE(static_cast<bool>(u1));
            EXPECT_TRUE(static_cast<bool>(u2));
        }
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

// ---------- AtomicFd additional edge-case tests ----------

TEST(AtomicFdTest, CopyAssignmentEmptySourceToNonEmpty)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1;
        AtomicFd<Tracked> u2{&storage, new Tracked(10)};
        EXPECT_EQ(Tracked::alive, 1);
        u2 = u1; // u2 releases old, source is empty
        EXPECT_FALSE(static_cast<bool>(u1));
        EXPECT_FALSE(static_cast<bool>(u2));
        EXPECT_EQ(Tracked::alive, 0);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, CopyAssignmentEmptySourceToEmpty)
{
    AtomicFd<Tracked> u1;
    AtomicFd<Tracked> u2;
    u2 = u1;
    EXPECT_FALSE(static_cast<bool>(u1));
    EXPECT_FALSE(static_cast<bool>(u2));
}

TEST(AtomicFdTest, CopyFromEmpty)
{
    AtomicFd<Tracked> u1;
    AtomicFd<Tracked> u2{u1};
    EXPECT_FALSE(static_cast<bool>(u1));
    EXPECT_FALSE(static_cast<bool>(u2));
}

TEST(AtomicFdTest, MoveFromEmpty)
{
    AtomicFd<Tracked> u1;
    AtomicFd<Tracked> u2{std::move(u1)};
    EXPECT_FALSE(static_cast<bool>(u1));
    EXPECT_FALSE(static_cast<bool>(u2));
}

TEST(AtomicFdTest, MoveAssignmentEmptyToEmpty)
{
    AtomicFd<Tracked> u1;
    AtomicFd<Tracked> u2;
    u2 = std::move(u1);
    EXPECT_FALSE(static_cast<bool>(u1));
    EXPECT_FALSE(static_cast<bool>(u2));
}

TEST(AtomicFdTest, MoveAssignmentEmptyToNonEmpty)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u1;
        AtomicFd<Tracked> u2{&storage, new Tracked(10)};
        EXPECT_EQ(Tracked::alive, 1);
        u2 = std::move(u1);
        EXPECT_FALSE(static_cast<bool>(u1));
        EXPECT_FALSE(static_cast<bool>(u2));
        EXPECT_EQ(Tracked::alive, 0); // old object was released
    }
}

TEST(AtomicFdTest, SelfCopyAssignment)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u{&storage, new Tracked(1)};
        u = u; // self-assignment must not crash
        EXPECT_TRUE(static_cast<bool>(u));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(AtomicFdTest, SelfMoveAssignment)
{
    alignas(uint64_t) uint64_t storage{};
    {
        AtomicFd<Tracked> u{&storage, new Tracked(1)};
        u = std::move(u); // self-move must not crash
        EXPECT_TRUE(static_cast<bool>(u));
        EXPECT_EQ(Tracked::alive, 1);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

// ---------- AtomicFdSet tests ----------

class AtomicFdSetTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto &set = AtomicFdSet<Tracked>::Instance();
        set.Uninitialize();
        EXPECT_EQ(Tracked::alive, 0);
    }

    void TearDown() override
    {
        auto &set = AtomicFdSet<Tracked>::Instance();
        set.Uninitialize();
        EXPECT_EQ(Tracked::alive, 0);
    }
};

TEST_F(AtomicFdSetTest, Singleton)
{
    auto &a = AtomicFdSet<Tracked>::Instance();
    auto &b = AtomicFdSet<Tracked>::Instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(AtomicFdSetTest, InitializeAndCapacity)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    EXPECT_EQ(set.Initialize(), 0);
    EXPECT_GT(set.Capacity(), 0);
    EXPECT_LE(set.Capacity(), 65536);
}

TEST_F(AtomicFdSetTest, DoubleInitialize)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    EXPECT_EQ(set.Initialize(), 0);
    EXPECT_EQ(set.Initialize(), 0); // second call should return 0 (already inited)
}

TEST_F(AtomicFdSetTest, InitialSizeIsZero)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    EXPECT_EQ(set.Size(), 0);
}

TEST_F(AtomicFdSetTest, GetItemOutOfRange)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    auto item = set.GetItem(-1);
    EXPECT_FALSE(static_cast<bool>(item));
    item = set.GetItem(static_cast<int>(set.Capacity()) + 1);
    EXPECT_FALSE(static_cast<bool>(item));
}

TEST_F(AtomicFdSetTest, ForEachOnEmptySet)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    int count = 0;
    set.ForEach([&count](int, Tracked *) { count++; });
    EXPECT_EQ(count, 0);
}

TEST_F(AtomicFdSetTest, UninitializeCleansUp)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    EXPECT_GT(set.Capacity(), 0);
    set.Uninitialize();
    // After uninit, re-init should work
    EXPECT_EQ(set.Initialize(), 0);
}

TEST_F(AtomicFdSetTest, CapacityStable)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    auto cap = set.Capacity();
    EXPECT_EQ(set.Capacity(), cap);
    set.Uninitialize();
    set.Initialize();
    EXPECT_EQ(set.Capacity(), cap);
}

TEST_F(AtomicFdSetTest, ForEachNonEmpty)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(0, new Tracked(10)));
    EXPECT_TRUE(set.SetItem(1, new Tracked(20)));

    int count = 0;
    int sum = 0;
    set.ForEach([&](int, Tracked *p) {
        count++;
        sum += p->id;
    });
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 30);
}

TEST_F(AtomicFdSetTest, SetItemAndGetItem)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(5, new Tracked(100)));
    EXPECT_EQ(Tracked::alive, 1);

    auto item = set.GetItem(5);
    EXPECT_TRUE(static_cast<bool>(item));
    ASSERT_NE(item.Get(), nullptr);
    EXPECT_EQ(item.Get()->id, 100);
    EXPECT_EQ(Tracked::alive, 1);
}

TEST_F(AtomicFdSetTest, SetItemOutOfRange)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    auto *t1 = new Tracked(1);
    EXPECT_FALSE(set.SetItem(-1, t1));
    delete t1;

    auto *t2 = new Tracked(2);
    EXPECT_FALSE(set.SetItem(static_cast<int>(set.Capacity()) + 1, t2));
    delete t2;

    EXPECT_EQ(Tracked::alive, 0);
}

TEST_F(AtomicFdSetTest, SetItemOccupiedSlot)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(3, new Tracked(10)));
    auto *t2 = new Tracked(20);
    EXPECT_FALSE(set.SetItem(3, t2)); // slot already in use
    delete t2;                        // caller must free on failure
    EXPECT_EQ(Tracked::alive, 1);     // only Tracked(10) in the slot
}

TEST_F(AtomicFdSetTest, SizeAfterSetItem)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    EXPECT_EQ(set.Size(), 0);

    EXPECT_TRUE(set.SetItem(1, new Tracked(1)));
    EXPECT_EQ(set.Size(), 1);

    EXPECT_TRUE(set.SetItem(2, new Tracked(2)));
    EXPECT_EQ(set.Size(), 2);
}

TEST_F(AtomicFdSetTest, SetItemThenRemoveItem)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(3, new Tracked(77)));
    EXPECT_EQ(set.Size(), 1);

    {
        auto removed = set.RemoveItem(3);
        EXPECT_TRUE(static_cast<bool>(removed));
        ASSERT_NE(removed.Get(), nullptr);
        EXPECT_EQ(removed.Get()->id, 77);
        EXPECT_EQ(set.Size(), 1); // removed handle still holds a reference
    }
    EXPECT_EQ(set.Size(), 0);

    auto empty = set.GetItem(3);
    EXPECT_FALSE(static_cast<bool>(empty));
}

TEST_F(AtomicFdSetTest, RemoveItemOutOfRange)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    auto removed = set.RemoveItem(-1);
    EXPECT_FALSE(static_cast<bool>(removed));
    removed = set.RemoveItem(static_cast<int>(set.Capacity()) + 1);
    EXPECT_FALSE(static_cast<bool>(removed));
}

TEST_F(AtomicFdSetTest, RemoveItemEmptySlot)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();
    auto removed = set.RemoveItem(0);
    EXPECT_FALSE(static_cast<bool>(removed));
}

TEST_F(AtomicFdSetTest, ForEachAfterSetItem)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(0, new Tracked(10)));
    EXPECT_TRUE(set.SetItem(1, new Tracked(20)));
    EXPECT_TRUE(set.SetItem(2, new Tracked(30)));

    int count = 0;
    int sum = 0;
    set.ForEach([&](int, Tracked *p) {
        count++;
        sum += p->id;
    });
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 60);
}

TEST_F(AtomicFdSetTest, ForEachAfterRemove)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_TRUE(set.SetItem(0, new Tracked(10)));
    EXPECT_TRUE(set.SetItem(1, new Tracked(20)));
    EXPECT_TRUE(set.SetItem(2, new Tracked(30)));
    set.RemoveItem(1);

    int count = 0;
    int sum = 0;
    set.ForEach([&](int fd, Tracked *p) {
        count++;
        sum += p->id;
        EXPECT_NE(fd, 1); // slot 1 should be gone
    });
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 40);
}

TEST_F(AtomicFdSetTest, SizeAfterMultipleOps)
{
    auto &set = AtomicFdSet<Tracked>::Instance();
    set.Initialize();

    EXPECT_EQ(set.Size(), 0);
    EXPECT_TRUE(set.SetItem(0, new Tracked(1)));
    EXPECT_EQ(set.Size(), 1);
    EXPECT_TRUE(set.SetItem(5, new Tracked(2)));
    EXPECT_EQ(set.Size(), 2);
    set.RemoveItem(0);
    EXPECT_EQ(set.Size(), 1);
    set.RemoveItem(5);
    EXPECT_EQ(set.Size(), 0);
    set.RemoveItem(5); // double remove
    EXPECT_EQ(set.Size(), 0);
}
