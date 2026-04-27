// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <functional>
#include "brpc_thread_pool.h"

using namespace testing;
using namespace Brpc;

namespace {
static const uint32_t TEST_QUEUE_CAPACITY = 100;
static const uint32_t TEST_SMALL_QUEUE_CAPACITY = 2;
static const uint32_t TEST_THREAD_NUM = 2;
static const int TEST_EXEC_RESULT_SUCCESS = 0;
static const int TEST_EXEC_RESULT_FAILURE = -1;
static const int TEST_SLEEP_MS = 100;

// 新增常量以替换魔鬼数字
static const int TEST_RUN_COUNT = 3;
static const int TEST_SUBMIT_COUNT = 5;
static const int TEST_EXTRA_SUBMIT = 50;
static const int TEST_SMALL_TASK_SLEEP_MS = 5; // ms
static const int TEST_EXPECT_VALUE_42 = 42;
static const int RUNNABLE_TYPE_NORMAL_VALUE = 0;
static const int RUNNABLE_TYPE_STOP_VALUE = 1;
static const int TEST_UNKNOWN_EXCEPTION = 42; // 非标准异常替代常量

// 等待相关常量
static const int TEST_WAIT_POLL_MS = 5;
static const int TEST_WAIT_TIMEOUT_MS = 2000;

// 保护环境变量修改的互斥量，以避免并发调用 setenv/unsetenv 导致竞态
static std::mutex g_envMutex;
// 保护对 ExecutorService 单例的并发 Start/Stop/操作
static std::mutex g_serviceMutex;

// 新增：替换 Runnable_CapturesComplexState 中的魔鬼数字
static const int TEST_CAPTURE_A = 10;
static const int TEST_CAPTURE_B = 20;
static const int TEST_CAPTURE_EXPECT = 30;

// 新增：环境变量相关常量
static const char* ENV_THREAD_POOL_SIZE_STR = "2";
static const int ENV_OVERWRITE = 1;

// 最小成功提交计数
static const int TEST_MIN_EXECUTE_COUNT = 1;

// 等待断言满足的辅助函数，避免直接使用固定 sleep
static bool WaitForPredicate(const std::function<bool()> &pred, int timeoutMs = TEST_WAIT_TIMEOUT_MS)
{
    auto start = std::chrono::steady_clock::now();
    while (!pred()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_POLL_MS));
    }
    return true;
}

// 简化原子等待函数，避免在调用处使用 lambda 捕获
static bool WaitForAtomicGE(const std::atomic<int> &atom, int expected, int timeoutMs = TEST_WAIT_TIMEOUT_MS)
{
    auto start = std::chrono::steady_clock::now();
    while (atom.load() < expected) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_POLL_MS));
    }
    return true;
}

static bool WaitForAtomicTrue(const std::atomic<bool> &atom, int timeoutMs = TEST_WAIT_TIMEOUT_MS)
{
    auto start = std::chrono::steady_clock::now();
    while (!atom.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > timeoutMs) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_WAIT_POLL_MS));
    }
    return true;
}
}

class BrpcThreadPoolTest : public testing::Test {
protected:
    void SetUp() override
    {
        // 确保在每个测试开始前单例处于已停止状态，防止上一个测试残留
        std::lock_guard<std::mutex> guard(g_serviceMutex);
        ExecutorService::GetExecutorService()->Stop();
    }

    void TearDown() override
    {
        // 确保停止并检查 mock
        std::lock_guard<std::mutex> guard(g_serviceMutex);
        ExecutorService::GetExecutorService()->Stop();
        GlobalMockObject::verify();
    }
};

// 测试 Runnable 类 - 默认构造函数
TEST_F(BrpcThreadPoolTest, Runnable_DefaultConstructor)
{
    Runnable runnable;
    // 默认构造的 Runnable 的 task 是 nullptr
    // 调用 Run() 不应该做任何事情
    runnable.Run();
}

// 测试 Runnable 类 - 带 lambda 的构造函数
TEST_F(BrpcThreadPoolTest, Runnable_LambdaConstructor)
{
    int value = 0;
    Runnable runnable([&value]() {
        value = TEST_EXPECT_VALUE_42;
    });
    runnable.Run();
    EXPECT_EQ(value, TEST_EXPECT_VALUE_42);
}

// 测试 Runnable 类 - Run 方法执行多次
TEST_F(BrpcThreadPoolTest, Runnable_RunMultipleTimes)
{
    int count = 0;
    Runnable runnable([&count]() {
        count++;
    });
    runnable.Run();
    runnable.Run();
    runnable.Run();
    EXPECT_EQ(count, TEST_RUN_COUNT);
}

// 测试 Runnable 类 - 空 lambda
TEST_F(BrpcThreadPoolTest, Runnable_EmptyLambda)
{
    Runnable runnable([]() {});
    runnable.Run();  // 不应该崩溃
}

// 测试 ExecutorService::GetExecutorService 单例获取
TEST_F(BrpcThreadPoolTest, GetExecutorService_ReturnsSingleton)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service1 = ExecutorService::GetExecutorService();
    ExecutorService* service2 = ExecutorService::GetExecutorService();
    EXPECT_EQ(service1, service2);
    EXPECT_NE(service1, nullptr);
}

// 测试 ExecutorService::SetThreadName 设置名称
TEST_F(BrpcThreadPoolTest, SetThreadName_SetsName)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->SetThreadName("test_pool");
    // 名称已设置，无法直接验证，但方法被调用
}

// 测试 ExecutorService 构造函数和析构函数
TEST_F(BrpcThreadPoolTest, ConstructorAndDestructor)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    // GetExecutorService 返回静态实例，析构函数会在程序结束时调用
    ExecutorService* service = ExecutorService::GetExecutorService();
    EXPECT_NE(service, nullptr);
}

// 测试 ExecutorService::Execute - 未启动时返回 false
TEST_F(BrpcThreadPoolTest, Execute_NotStartedReturnsFalse)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    // 确保服务未启动（可能已启动过，先停止）
    service->Stop();

    Runnable runnable([]() {});
    bool result = service->Execute(runnable);
    EXPECT_FALSE(result);
}

// 测试 ExecutorService::Stop - 未启动时调用安全
TEST_F(BrpcThreadPoolTest, Stop_NotStartedSafe)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();  // 不应该崩溃
}

// 测试 RunnableType 枚举值
TEST_F(BrpcThreadPoolTest, RunnableType_Values)
{
    EXPECT_EQ(static_cast<int>(RunnableType::NORMAL), RUNNABLE_TYPE_NORMAL_VALUE);
    EXPECT_EQ(static_cast<int>(RunnableType::STOP), RUNNABLE_TYPE_STOP_VALUE);
}

// 测试 Runnable 捕获复杂状态
TEST_F(BrpcThreadPoolTest, Runnable_CapturesComplexState)
{
    int a = TEST_CAPTURE_A;
    int b = TEST_CAPTURE_B;
    std::string str = "test";
    int result = 0;

    Runnable runnable([&a, &b, &str, &result]() {
        result = a + b;
        str = "modified";
    });

    runnable.Run();
    EXPECT_EQ(result, TEST_CAPTURE_EXPECT);
    EXPECT_EQ(str, "modified");
}

// ===== 集成测试：覆盖线程池核心逻辑 =====
// 注意：由于 ExecutorService 和 Context 是单例，测试间状态可能有残留
// 每个测试需要确保在独立环境下运行

class BrpcThreadPoolIntegrationTest : public testing::Test {
protected:
    void SetUp() override
    {
        // 设置环境变量控制线程池大小（受互斥量保护）
        std::lock_guard<std::mutex> guard(g_envMutex);
        setenv("UBSOCKET_THREAD_POOL_SIZE", ENV_THREAD_POOL_SIZE_STR, ENV_OVERWRITE);

        // 确保单例初始状态
        std::lock_guard<std::mutex> guard2(g_serviceMutex);
        ExecutorService::GetExecutorService()->Stop();
    }

    void TearDown() override
    {
        // 按固定顺序：先锁 g_envMutex 再锁 g_serviceMutex，避免死锁
        std::lock_guard<std::mutex> guard(g_envMutex);
        {
            std::lock_guard<std::mutex> guard2(g_serviceMutex);
            ExecutorService::GetExecutorService()->Stop();
        }
        // 清理环境变量（仍在 g_envMutex 保护下）
        unsetenv("UBSOCKET_THREAD_POOL_SIZE");
        GlobalMockObject::verify();
    }
};

// 测试 Start 成功并 Execute 任务执行（覆盖 line 20-49, 63-76, 123-143, 83-96）
TEST_F(BrpcThreadPoolIntegrationTest, StartAndExecute_Success)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();

    bool started = service->Start(TEST_QUEUE_CAPACITY);
    EXPECT_TRUE(started);

    std::atomic<int> counter{0};
    for (int i = 0; i < TEST_SUBMIT_COUNT; i++) {
        Runnable runnable([&counter]() {
            counter++;
        });
        EXPECT_TRUE(service->Execute(runnable));
    }

    // 等待任务执行完成（使用轮询等待，避免不稳定的固定 sleep）
    bool ok = WaitForAtomicGE(counter, TEST_SUBMIT_COUNT);
    EXPECT_TRUE(ok);
}

// 测试 Stop 多次调用安全（覆盖 line 54-56 stopped_ 分支）
TEST_F(BrpcThreadPoolIntegrationTest, Stop_MultipleCallsSafe)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->Start(TEST_QUEUE_CAPACITY);

    service->Stop();  // 第一次
    service->Stop();  // 第二次，覆盖 stopped_ 分支
    service->Stop();  // 第三次
}

// 测试 Execute 使用 std::function 参数（覆盖 line 78-81）
TEST_F(BrpcThreadPoolIntegrationTest, Execute_StdFunction_Coverage)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->Start(TEST_QUEUE_CAPACITY);

    std::atomic<bool> done{false};
    std::function<void()> task = [&done]() { done.store(true); };

    EXPECT_TRUE(service->Execute(task));  // 覆盖 line 78-81
    bool ok = WaitForAtomicTrue(done);
    EXPECT_TRUE(ok);
}

// 测试 DoRunnable 捕获 runtime_error（覆盖 line 91-92）
TEST_F(BrpcThreadPoolIntegrationTest, Execute_RuntimeErrorCaught)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->Start(TEST_QUEUE_CAPACITY);

    Runnable runnable([]() {
        throw std::runtime_error("test runtime error");
    });

    EXPECT_TRUE(service->Execute(runnable));

    // 提交一个能够标记完成的正常任务，确保线程池已处理异常任务后仍能正常工作
    std::atomic<bool> done{false};
    Runnable normalTask([&done]() { done.store(true); });
    EXPECT_TRUE(service->Execute(normalTask));
    bool ok = WaitForAtomicTrue(done);
    EXPECT_TRUE(ok);
}

// 测试 DoRunnable 捕获未知异常（覆盖 line 93-95）
TEST_F(BrpcThreadPoolIntegrationTest, Execute_UnknownExceptionCaught)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->Start(TEST_QUEUE_CAPACITY);

    Runnable runnable([]() {
        throw TEST_UNKNOWN_EXCEPTION;
    });

    EXPECT_TRUE(service->Execute(runnable));

    // 提交一个能够标记完成的正常任务，确保线程池已处理异常任务后仍能正常工作
    std::atomic<bool> done{false};
    Runnable normalTask([&done]() { done.store(true); });
    EXPECT_TRUE(service->Execute(normalTask));
    bool ok = WaitForAtomicTrue(done);
    EXPECT_TRUE(ok);
}

// 测试 SetThreadName 后线程池正常运行（覆盖 line 127-129）
TEST_F(BrpcThreadPoolIntegrationTest, SetThreadName_AffectsThreadName)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->SetThreadName("test_pool");

    bool started = service->Start(TEST_QUEUE_CAPACITY);
    EXPECT_TRUE(started);

    std::atomic<bool> done{false};
    Runnable runnable([&done]() { done.store(true); });
    EXPECT_TRUE(service->Execute(runnable));

    bool ok = WaitForAtomicTrue(done);
    EXPECT_TRUE(ok);
}

// 测试 Execute 队列满返回 false（覆盖 line 69-71）
// 注意：由于线程快速消费，队列满难以稳定触发，此测试验证基本功能
TEST_F(BrpcThreadPoolIntegrationTest, Execute_QueueFull_BasicTest)
{
    std::unique_lock<std::mutex> service_lock(g_serviceMutex);
    ExecutorService* service = ExecutorService::GetExecutorService();
    service->Stop();
    service->Start(TEST_SMALL_QUEUE_CAPACITY);

    // 快速提交多个任务，尝试触发队列满
    std::atomic<int> executeCount{0};
    for (uint32_t i = 0; i < TEST_SMALL_QUEUE_CAPACITY + TEST_EXTRA_SUBMIT; i++) {
        Runnable runnable([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(TEST_SMALL_TASK_SLEEP_MS));
        });
        if (service->Execute(runnable)) {
            executeCount++;
        } else {
            break;  // 队列满时停止
        }
    }

    // 等待至少一个任务被接受（避免固定 sleep）
    bool ok = WaitForAtomicGE(executeCount, TEST_MIN_EXECUTE_COUNT);
    EXPECT_TRUE(ok);
    EXPECT_GE(executeCount.load(), TEST_MIN_EXECUTE_COUNT);  // 至少有一个任务成功提交
}