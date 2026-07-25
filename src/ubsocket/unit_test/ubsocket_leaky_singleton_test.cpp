/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "common/ubsocket_leaky_singleton.h"

using namespace ock::ubs;

// ==================== Test fixture types ====================

struct Counter {
    Counter()
    {
        ++instances;
    }
    int value = 0;
    static std::atomic<int> instances;
};
std::atomic<int> Counter::instances{0};

struct AnotherType {
    std::string name = "default";
};

// Fresh types for count-verification tests (singleton persists across tests,
// so each counting test needs its own type to get accurate creation counts)
struct CountTypeA {
    CountTypeA()
    {
        ++instances;
    }
    static std::atomic<int> instances;
};
std::atomic<int> CountTypeA::instances{0};

struct ThreadCountType {
    ThreadCountType()
    {
        ++instances;
    }
    static std::atomic<int> instances;
};
std::atomic<int> ThreadCountType::instances{0};

// ==================== LeakySingleton Tests ====================

class LeakySingletonTest : public ::testing::Test {
};

TEST_F(LeakySingletonTest, Instance_ReturnsSameObject)
{
    Counter &a = LeakySingleton<Counter>::Instance();
    Counter &b = LeakySingleton<Counter>::Instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(LeakySingletonTest, Instance_OnlyOneObjectCreated)
{
    CountTypeA::instances = 0;
    LeakySingleton<CountTypeA>::Instance();
    LeakySingleton<CountTypeA>::Instance();
    LeakySingleton<CountTypeA>::Instance();
    EXPECT_EQ(CountTypeA::instances.load(), 1);
}

TEST_F(LeakySingletonTest, Instance_DefaultState)
{
    Counter &c = LeakySingleton<Counter>::Instance();
    EXPECT_EQ(c.value, 0);
}

TEST_F(LeakySingletonTest, Instance_ModificationPersists)
{
    LeakySingleton<Counter>::Instance().value = 42;
    EXPECT_EQ(LeakySingleton<Counter>::Instance().value, 42);
}

TEST_F(LeakySingletonTest, DifferentTypes_IndependentInstances)
{
    Counter &c = LeakySingleton<Counter>::Instance();
    AnotherType &a = LeakySingleton<AnotherType>::Instance();
    EXPECT_NE(reinterpret_cast<void *>(&c), reinterpret_cast<void *>(&a));
    EXPECT_EQ(a.name, "default");
}

TEST_F(LeakySingletonTest, DifferentTypes_DontInterfere)
{
    LeakySingleton<Counter>::Instance().value = 100;
    LeakySingleton<AnotherType>::Instance().name = "hello";

    EXPECT_EQ(LeakySingleton<Counter>::Instance().value, 100);
    EXPECT_EQ(LeakySingleton<AnotherType>::Instance().name, "hello");
}

TEST_F(LeakySingletonTest, ThreadSafety_MultipleThreadsSameInstance)
{
    const int kNumThreads = 8;
    std::vector<std::thread> threads;
    std::vector<Counter *> results(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&results, i]() { results[i] = &LeakySingleton<Counter>::Instance(); });
    }
    for (auto &t : threads) {
        t.join();
    }

    Counter *first = results[0];
    EXPECT_NE(first, nullptr);
    for (int i = 1; i < kNumThreads; ++i) {
        EXPECT_EQ(results[i], first);
    }
}

TEST_F(LeakySingletonTest, ThreadSafety_StillOneInstanceCreated)
{
    ThreadCountType::instances = 0;
    const int kNumThreads = 16;
    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([]() { LeakySingleton<ThreadCountType>::Instance(); });
    }
    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(ThreadCountType::instances.load(), 1);
}

TEST_F(LeakySingletonTest, CopyConstructor_Deleted)
{
    EXPECT_FALSE(std::is_copy_constructible<LeakySingleton<Counter>>::value);
}

TEST_F(LeakySingletonTest, CopyAssignment_Deleted)
{
    EXPECT_FALSE(std::is_copy_assignable<LeakySingleton<Counter>>::value);
}

TEST_F(LeakySingletonTest, MoveConstructor_NotDeclared)
{
    // LeakySingleton has user-declared copy operations, so move is not
    // implicitly declared. Instance() is the only access path.
    EXPECT_FALSE(std::is_move_constructible<LeakySingleton<Counter>>::value);
}
