/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "umq_tp_wait_queue.h"

#include <cstdint>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_tx_helper.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const int TEST_FD = 42;
static const uint64_t TEST_HANDLE = 12345;
static const uint32_t TEST_TP_POOL_SIZE = 800; /* UmqSetting::UMQ_TP_POOL_SIZE 默认值, WakeUp 早退边界 */
static const uint32_t TEST_RX_DEPTH = 8; /* 设小 UBS_RX_DEPTH,降低公开 Push 填满/清空环的成本(capacity_ = next_pow2(8) = 8) */
static const uint32_t TEST_RX_DEPTH_DEFAULT = 2048; /* GlobalSetting::UBS_RX_DEPTH 出厂默认值, TearDown 恢复 */
static const uint32_t TEST_WAKEUP_REQUEST = 5; /* 请求唤醒数 > 队列实际元素数 */
static const uint32_t TEST_WAKEUP_EXACT = 2;   /* 请求唤醒数 == 队列实际元素数 */

static int g_fcCallCount = 0;
static uint64_t g_fcHandle = 0;

static int MockPollUmqTxForFcReturn(uint64_t umq_handle)
{
    g_fcCallCount++;
    g_fcHandle = umq_handle;
    return 0;
}

/* 测试用 socket 对: umqSock 用于 jetty 状态断言, sockPtr 用于入队 */
struct TestSocketPair {
    UmqSocketPtr umqSock;
    SocketPtr sockPtr;
};

static TestSocketPair MakeTestSocket()
{
    TestSocketPair pair;
    pair.umqSock = MakeRef<UmqSocket>(TEST_FD);
    pair.sockPtr = RefConvert<UmqSocket, Socket>(pair.umqSock);
    return pair;
}
} // namespace

class UmqTpWaitQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_fcCallCount = 0;
        g_fcHandle = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        UmqSetting::UMQ_TP_POOL_SIZE = TEST_TP_POOL_SIZE;
        /* 必须在首次访问 Instance() 之前设置(GetCapacity 在构造函数内读取,单例 capacity_ 固化) */
        GlobalSetting::UBS_RX_DEPTH = TEST_RX_DEPTH;
        ResetWaitQueue();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalSetting::UBS_RX_DEPTH = TEST_RX_DEPTH_DEFAULT;
        errno = 0;
    }

    /* 单例进程内仅一次，测试间通过公开 Pop 清空环隔离状态（顺带析构残留元素） */
    void ResetWaitQueue()
    {
        auto &q = UmqTpWaitQueue::Instance();
        UmqTpWaitQueueElement element;
        while (q.Pop(element)) {
        }
    }

    /* 用公开 Push 填满环，令后续 Push 在容量检查处返回 false */
    void FillQueueToFull()
    {
        auto &q = UmqTpWaitQueue::Instance();
        while (q.Push(UmqTpWaitQueueElement{})) {
        }
    }
};

// ==================== Enqueue(uint64_t) ====================

TEST_F(UmqTpWaitQueueTest, EnqueueHandle_InvalidHandle_ReturnsError)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_EQ(q.Enqueue(static_cast<uint64_t>(0)), UBS_ERROR);
    EXPECT_TRUE(q.Empty());
}

TEST_F(UmqTpWaitQueueTest, EnqueueHandle_ValidHandle_PushesAndReturnsOk)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_EQ(q.Enqueue(TEST_HANDLE), UBS_OK);
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_FALSE(q.Empty());
}

TEST_F(UmqTpWaitQueueTest, EnqueueHandle_QueueFull_ReturnsError)
{
    auto &q = UmqTpWaitQueue::Instance();
    FillQueueToFull();
    EXPECT_EQ(q.Enqueue(TEST_HANDLE), UBS_ERROR);
}

// ==================== Enqueue(SocketPtr) ====================

TEST_F(UmqTpWaitQueueTest, EnqueueSocket_NullSocket_ReturnsError)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_EQ(q.Enqueue(SocketPtr{}), UBS_ERROR);
    EXPECT_TRUE(q.Empty());
}

TEST_F(UmqTpWaitQueueTest, EnqueueSocket_IdleSocket_PushesAndMarksWaiting)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();

    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
    EXPECT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);
    EXPECT_EQ(q.Size(), 1u);
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::WAITING);
}

TEST_F(UmqTpWaitQueueTest, EnqueueSocket_AlreadyWaiting_SkipsPush)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();

    EXPECT_TRUE(pair.umqSock->TryAcquireForWaiting()); /* IDLE -> WAITING */
    EXPECT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);        /* TryAcquire 失败 -> 跳过入队 */
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::WAITING);
    pair.umqSock->ResetToIdle();
}

TEST_F(UmqTpWaitQueueTest, EnqueueSocket_QueueFull_ResetsToIdleAndReturnsError)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();

    FillQueueToFull();
    EXPECT_EQ(q.Enqueue(pair.sockPtr), UBS_ERROR);
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
}

// ==================== TryWakeupOne ====================

TEST_F(UmqTpWaitQueueTest, TryWakeupOne_EmptyQueue_ReturnsOk)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.TryWakeupOne(), UBS_OK);
}

TEST_F(UmqTpWaitQueueTest, TryWakeupOne_SocketElement_WakesAndResetsIdle)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();
    ASSERT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);
    ASSERT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::WAITING);

    EXPECT_EQ(q.TryWakeupOne(), UBS_OK);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
    /* 真实路径 RxQueueEmpty()==true → NotifyWritable → writable-ready 版本号 +1 */
    EXPECT_EQ(pair.umqSock->Version(), 1u);
}

TEST_F(UmqTpWaitQueueTest, TryWakeupOne_HandleElement_CallsPollFcReturn)
{
    auto &q = UmqTpWaitQueue::Instance();
    MOCKER(&UmqTxHelper::PollUmqTxForFcReturn).stubs().will(invoke(MockPollUmqTxForFcReturn));
    ASSERT_EQ(q.Enqueue(TEST_HANDLE), UBS_OK);

    EXPECT_EQ(q.TryWakeupOne(), UBS_OK);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(g_fcCallCount, 1);
    EXPECT_EQ(g_fcHandle, TEST_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqTpWaitQueueTest, TryWakeupOne_UnsupportedType_ReturnsOk)
{
    auto &q = UmqTpWaitQueue::Instance();
    UmqTpWaitQueueElement element; /* type_ = TYPE_COUNT */
    ASSERT_TRUE(q.Push(std::move(element)));

    EXPECT_EQ(q.TryWakeupOne(), UBS_OK);
    EXPECT_TRUE(q.Empty());
}

TEST_F(UmqTpWaitQueueTest, TryWakeupOne_SocketElement_RxNotEmpty_NotifyReadableNoWritable)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();
    ASSERT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);

    MOCKER_CPP(&UmqSocket::RxQueueEmpty).stubs().will(returnValue(false));

    EXPECT_EQ(q.TryWakeupOne(), UBS_OK);
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
    /* NotifyReadable 分支不置 writable-ready(未注册 epoll 早退); 若误走 NotifyWritable, Version 会 +1 */
    EXPECT_EQ(pair.umqSock->Version(), 0u);
    GlobalMockObject::verify();
}

// ==================== WakeUp ====================

TEST_F(UmqTpWaitQueueTest, WakeUp_GreaterThanPoolSize_ReturnsZero)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_EQ(q.WakeUp(UmqSetting::UMQ_TP_POOL_SIZE + 1), 0u);
}

TEST_F(UmqTpWaitQueueTest, WakeUp_EqualToPoolSize_PopsOne)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();
    ASSERT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);

    /* wakeUpNum == UMQ_TP_POOL_SIZE 不触发早退(严格 > 才早退), 正常弹出 */
    EXPECT_EQ(q.WakeUp(TEST_TP_POOL_SIZE), 1u);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
}

TEST_F(UmqTpWaitQueueTest, WakeUp_EmptyQueue_ReturnsZero)
{
    auto &q = UmqTpWaitQueue::Instance();
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.WakeUp(TEST_WAKEUP_REQUEST), 0u);
}

TEST_F(UmqTpWaitQueueTest, WakeUp_MixedBatch_DispatchesSocketAndHandle)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();
    MOCKER(&UmqTxHelper::PollUmqTxForFcReturn).stubs().will(invoke(MockPollUmqTxForFcReturn));

    ASSERT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);
    ASSERT_EQ(q.Enqueue(TEST_HANDLE), UBS_OK);
    ASSERT_EQ(q.Size(), 2u);

    EXPECT_EQ(q.WakeUp(TEST_WAKEUP_REQUEST), 2u);
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
    /* socket 元素走 NotifyWritable → writable-ready 版本号 +1 */
    EXPECT_EQ(pair.umqSock->Version(), 1u);
    EXPECT_EQ(g_fcCallCount, 1);
    EXPECT_EQ(g_fcHandle, TEST_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqTpWaitQueueTest, WakeUp_UnsupportedType_ReturnsCount)
{
    auto &q = UmqTpWaitQueue::Instance();
    ASSERT_TRUE(q.Push(UmqTpWaitQueueElement{}));
    ASSERT_TRUE(q.Push(UmqTpWaitQueueElement{}));

    EXPECT_EQ(q.WakeUp(TEST_WAKEUP_EXACT), 2u);
    EXPECT_TRUE(q.Empty());
}

TEST_F(UmqTpWaitQueueTest, WakeUp_SocketElement_RxNotEmpty_NotifyReadableNoWritable)
{
    auto &q = UmqTpWaitQueue::Instance();
    TestSocketPair pair = MakeTestSocket();
    ASSERT_EQ(q.Enqueue(pair.sockPtr), UBS_OK);

    MOCKER_CPP(&UmqSocket::RxQueueEmpty).stubs().will(returnValue(false));

    EXPECT_EQ(q.WakeUp(1), 1u);
    EXPECT_EQ(pair.umqSock->GetJettyAllocState(), JettyAllocState::IDLE);
    /* NotifyReadable 分支不置 writable-ready(未注册 epoll 早退); 若误走 NotifyWritable, Version 会 +1 */
    EXPECT_EQ(pair.umqSock->Version(), 0u);
    GlobalMockObject::verify();
}

// ==================== Queue Capacity ====================

TEST_F(UmqTpWaitQueueTest, GetCapacity_DefaultRxDepth_NextPow2RoundUp)
{
    auto &q = UmqTpWaitQueue::Instance();
    /* 容量 = next_pow2(UBS_RX_DEPTH)(umq_tp_wait_queue.h GetCapacity) */
    uint64_t cap = GlobalSetting::UBS_RX_DEPTH;
    uint64_t expectCap = (cap <= 1) ? 1 : 1ULL << (64 - __builtin_clzll(cap - 1));

    /* 行为断言(边界值 + 相邻值): 第 expectCap 次 Push 成功, 第 expectCap+1 次失败 */
    for (uint64_t i = 0; i < expectCap; ++i) {
        ASSERT_TRUE(q.Push(UmqTpWaitQueueElement{}));
    }
    EXPECT_FALSE(q.Push(UmqTpWaitQueueElement{}));
    EXPECT_EQ(q.Size(), expectCap);
}