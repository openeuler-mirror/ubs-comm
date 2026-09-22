/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_thread_pool.h"

using namespace ock::ubs;

// ==================== ExecutorService Tests ====================

// NOTE: ExecutorService is a process-lifetime singleton whose Stop()
// sets a permanent stopped_ flag that is never reset. Therefore all
// tests that require Start()→Stop() must be grouped into a single
// test case (the first one that calls Start).

class ExecutorServiceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        orig_pool_size_ = GlobalSetting::UBS_THREAD_POOL_SIZE;
        GlobalSetting::UBS_THREAD_POOL_SIZE = 2;
    }

    void TearDown() override
    {
        GlobalSetting::UBS_THREAD_POOL_SIZE = orig_pool_size_;
    }

    uint32_t orig_pool_size_ = 0;
};

// --- Lifecycle tests (must be the first to call Start) ---

TEST_F(ExecutorServiceTest, FullLifecycle_StartExecuteStop)
{
    auto *svc = ExecutorService::GetExecutorService();

    // Start
    EXPECT_TRUE(svc->Start());
    EXPECT_TRUE(svc->Start()); // double-start is idempotent

    // Execute a Runnable
    std::atomic<bool> ran{false};
    svc->Execute(Runnable([&ran]() { ran = true; }));
    for (int i = 0; i < 100 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(ran.load());

    // Execute a std::function
    std::atomic<int> value{0};
    svc->Execute([&value]() { value = 42; });
    for (int i = 0; i < 100 && value.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(value.load(), 42);

    // Execute multiple tasks
    const int kTaskCount = 10;
    std::atomic<int> counter{0};
    for (int i = 0; i < kTaskCount; ++i) {
        svc->Execute([&counter]() { counter.fetch_add(1); });
    }
    for (int i = 0; i < 200 && counter.load() < kTaskCount; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(counter.load(), kTaskCount);

    // Stop
    svc->Stop();

    // After stop, Execute should return false
    Runnable task([]() {});
    EXPECT_FALSE(svc->Execute(task));
}

TEST_F(ExecutorServiceTest, StartWithZeroPoolSize_ReturnsFalse)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 0;
    auto *svc = ExecutorService::GetExecutorService();
    EXPECT_FALSE(svc->Start());
}

TEST_F(ExecutorServiceTest, Execute_WhenNotStartedReturnsFalse)
{
    auto *svc = ExecutorService::GetExecutorService();
    Runnable task([]() {});
    EXPECT_FALSE(svc->Execute(task));
}

TEST_F(ExecutorServiceTest, Stop_WhenNotStartedDoesNotCrash)
{
    auto *svc = ExecutorService::GetExecutorService();
    svc->Stop();
    SUCCEED();
}

// --- Runnable types ---

TEST_F(ExecutorServiceTest, Runnable_DefaultConstructor)
{
    Runnable r;
    r.Run();
    SUCCEED();
}

TEST_F(ExecutorServiceTest, Runnable_ExplicitTask)
{
    std::atomic<bool> ran{false};
    Runnable r([&ran]() { ran = true; });
    r.Run();
    EXPECT_TRUE(ran.load());
}

TEST_F(ExecutorServiceTest, Runnable_NullTaskDoesNotCrash)
{
    Runnable r(nullptr);
    r.Run();
    SUCCEED();
}

// ==================== ExecutorService: edge cases ====================

TEST_F(ExecutorServiceTest, Execute_NotStarted_ReturnsFalse)
{
    ExecutorService svc;
    EXPECT_FALSE(svc.Execute([]() {}));
}

TEST_F(ExecutorServiceTest, Start_AlreadyStarted_ReturnsTrue)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
    ExecutorService svc;
    ASSERT_TRUE(svc.Start(64));
    EXPECT_TRUE(svc.Start(64)); // already started, should return true
    svc.Stop();
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
}

TEST_F(ExecutorServiceTest, Start_ZeroThreadNum_ReturnsFalse)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 0;
    ExecutorService svc;
    EXPECT_FALSE(svc.Start(64));
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
}

TEST_F(ExecutorServiceTest, Destructor_NotStopped_CallsStop)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
    {
        ExecutorService svc;
        ASSERT_TRUE(svc.Start(64));
        // Destructor should call Stop() since stopped_ is false
    }
    // If we reach here, no crash/deadlock
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
}

TEST_F(ExecutorServiceTest, DoRunnable_ThrowingTask_NoCrash)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
    ExecutorService svc;
    ASSERT_TRUE(svc.Start(64));
    std::atomic<bool> done{false};
    // Task that throws runtime_error
    svc.Execute([&done]() {
        throw std::runtime_error("test error");
    });
    // Task that throws unknown exception
    svc.Execute([&done]() {
        throw 42;
    });
    // Normal task to verify pool still works
    svc.Execute([&done]() {
        done.store(true);
    });
    // Wait for normal task
    for (int i = 0; i < 1000 && !done.load(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(done.load());
    svc.Stop();
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
}

TEST_F(ExecutorServiceTest, Stop_NotStarted_NoOp)
{
    ExecutorService svc;
    svc.Stop(); // should not crash
}

TEST_F(ExecutorServiceTest, Stop_AlreadyStopped_NoOp)
{
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
    ExecutorService svc;
    ASSERT_TRUE(svc.Start(64));
    svc.Stop();
    svc.Stop(); // already stopped, should be no-op
    GlobalSetting::UBS_THREAD_POOL_SIZE = 1;
}
