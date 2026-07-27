/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "common/ubsocket_obj_statistics.h"

using namespace ock::ubs;

// ==================== ObjectStatistics Tests ====================

class ObjectStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto &stats = ObjectStatistics::Instance();
        for (int i = 0; i < OBJECT_COUNT; ++i) {
            stats.count_[i] = 0;
            stats.name_[i].clear();
        }
    }

    void TearDown() override
    {
        auto &stats = ObjectStatistics::Instance();
        for (int i = 0; i < OBJECT_COUNT; ++i) {
            stats.count_[i] = 0;
            stats.name_[i].clear();
        }
    }
};

// --- Singleton ---

TEST_F(ObjectStatisticsTest, Instance_ReturnsSameObject)
{
    ObjectStatistics &a = ObjectStatistics::Instance();
    ObjectStatistics &b = ObjectStatistics::Instance();
    EXPECT_EQ(&a, &b);
}

// --- Increase ---

TEST_F(ObjectStatisticsTest, Increase_IncrementsCount)
{
    auto &stats = ObjectStatistics::Instance();
    stats.Increase(UBS_SOCKET, "UBS_SOCKET");
    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), 1);

    stats.Increase(UBS_SOCKET, "UBS_SOCKET");
    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), 2);
}

TEST_F(ObjectStatisticsTest, Increase_SetsNameOnFirstUse)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_URMA_CONTEXT] = 0;
    stats.name_[UBS_URMA_CONTEXT].clear();

    stats.Increase(UBS_URMA_CONTEXT, "TestContext");
    EXPECT_EQ(stats.name_[UBS_URMA_CONTEXT], "TestContext");
}

TEST_F(ObjectStatisticsTest, Increase_DoesNotOverwriteName)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_URMA_JETTY] = 0;
    stats.name_[UBS_URMA_JETTY].clear();

    stats.Increase(UBS_URMA_JETTY, "FirstSet");
    EXPECT_EQ(stats.name_[UBS_URMA_JETTY], "FirstSet");

    // Second increase should not overwrite name (tmp > 0 after first)
    stats.Increase(UBS_URMA_JETTY, "ShouldNotSet");
    EXPECT_EQ(stats.name_[UBS_URMA_JETTY], "FirstSet");
}

// --- Decrease ---

TEST_F(ObjectStatisticsTest, Decrease_DecrementsCount)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_SOCKET] = 3;
    stats.Decrease(UBS_SOCKET);
    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), 2);
    stats.Decrease(UBS_SOCKET);
    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), 1);
}

TEST_F(ObjectStatisticsTest, Decrease_CanGoNegative)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_UMQ_SOCKET] = 1;
    stats.Decrease(UBS_UMQ_SOCKET);
    EXPECT_EQ(stats.count_[UBS_UMQ_SOCKET].load(), 0);
    stats.Decrease(UBS_UMQ_SOCKET);
    EXPECT_EQ(stats.count_[UBS_UMQ_SOCKET].load(), -1);
}

// --- DumpStr ---

TEST_F(ObjectStatisticsTest, DumpStr_ReturnsNonEmptyString)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_SOCKET] = 5;
    stats.name_[UBS_SOCKET] = "UBS_SOCKET";

    std::string dump = stats.DumpStr();
    EXPECT_FALSE(dump.empty());
}

// --- Multiple object types ---

TEST_F(ObjectStatisticsTest, MultipleTypes_IndependentCounts)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_SOCKET] = 0;
    stats.count_[UBS_URMA_CONTEXT] = 0;
    stats.count_[UBS_UMQ_SOCKET] = 0;

    stats.Increase(UBS_SOCKET, "UBS_SOCKET");
    stats.Increase(UBS_SOCKET, "UBS_SOCKET");
    stats.Increase(UBS_URMA_CONTEXT, "UBS_URMA_CONTEXT");
    stats.Increase(UBS_UMQ_SOCKET, "UBS_UMQ_SOCKET");

    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), 2);
    EXPECT_EQ(stats.count_[UBS_URMA_CONTEXT].load(), 1);
    EXPECT_EQ(stats.count_[UBS_UMQ_SOCKET].load(), 1);
}

// --- Thread safety ---

TEST_F(ObjectStatisticsTest, ThreadSafety_ConcurrentIncreaseDecrease)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_SOCKET] = 0;

    const int kOpsPerThread = 10000;
    std::vector<std::thread> threads;

    // 4 increment threads
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&stats]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                stats.Increase(UBS_SOCKET, "UBS_SOCKET");
            }
        });
    }

    // 2 decrement threads
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&stats]() {
            for (int i = 0; i < kOpsPerThread; ++i) {
                stats.Decrease(UBS_SOCKET);
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    // 4 * 10000 - 2 * 10000 = 20000
    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), kOpsPerThread * 2);
}

TEST_F(ObjectStatisticsTest, ThreadSafety_ConcurrentMultipleTypes)
{
    auto &stats = ObjectStatistics::Instance();
    stats.count_[UBS_SOCKET] = 0;
    stats.count_[UBS_URMA_JFS] = 0;

    const int kOps = 5000;
    std::vector<std::thread> threads;

    threads.emplace_back([&stats]() {
        for (int i = 0; i < kOps; ++i) {
            stats.Increase(UBS_SOCKET, "UBS_SOCKET");
        }
    });

    threads.emplace_back([&stats]() {
        for (int i = 0; i < kOps; ++i) {
            stats.Increase(UBS_URMA_JFS, "UBS_URMA_JFS");
        }
    });

    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(stats.count_[UBS_SOCKET].load(), kOps);
    EXPECT_EQ(stats.count_[UBS_URMA_JFS].load(), kOps);
}
