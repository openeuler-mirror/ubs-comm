/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_service_callback.h"
#include "multicast_periodic_manager.h"
#include "service_periodic_manager.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint16_t TEST_THREAD_COUNT = 1;
constexpr int TEST_NO_CPU_BIND = -1;
constexpr uint32_t TEST_IO_CAPACITY = 4;
constexpr uint32_t TEST_MAX_SUBSCRIBER_NUM = 4;
constexpr uint32_t TEST_ZERO_IO_CAPACITY = 0;
constexpr uint32_t TEST_OVERFLOW_IO_CAPACITY = 0x1000000U; // 0xFFFFFF + 1
constexpr uint32_t TEST_TIMER_SEQ_NO = 7;
constexpr uint32_t TEST_INVALID_SEQ_NO = 0;
constexpr int32_t TEST_TIMER_TIMEOUT_SEC = -1; // never timeout
constexpr int16_t TEST_NO_TIMEOUT = -1;
constexpr int16_t TEST_IMMEDIATE_TIMEOUT = 0;
constexpr uint16_t INVALID_THREAD_ID = M_MAX_THREAD_NUM + 1;
constexpr uint32_t INVALID_WHEEL_SEQ = 0xFFFFFFFEU;

class TestMultiCastCallback : public MultiCastCallback {
public:
    void Run(PublisherContext &context) override
    {
        (void)context;
        runCount++;
    }

    void SetTime(uint64_t time) override
    {
        startTime = time;
    }

    uint64_t GetTime() override
    {
        return startTime;
    }

    bool Permanent() const override
    {
        return false;
    }

    void Destroy() override
    {
        destroyed = true;
    }

    int runCount = 0;
    uint64_t startTime = 0;
    bool destroyed = false;
};
} // namespace

class TestMulticastPeriodicManager : public testing::Test {
public:
    TestMulticastPeriodicManager() = default;
    ~TestMulticastPeriodicManager() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestMulticastPeriodicManager::SetUp() {}

void TestMulticastPeriodicManager::TearDown() {}

TEST_F(TestMulticastPeriodicManager, TestAddTimerInvalidParams)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    // null timer
    MultiCastServiceTimer *nullTimer = nullptr;
    EXPECT_EQ(mgr.AddTimer(nullTimer), SER_INVALID_PARAM);

    // manager not started yet, adding timer is rejected
    MultiCastServiceTimer stopTimer(nullptr, nullptr, TEST_TIMER_TIMEOUT_SEC, 0, MultiCastSyncCBType::IO);
    stopTimer.SeqNo(TEST_TIMER_SEQ_NO);
    MultiCastServiceTimer *pStopTimer = &stopTimer;
    EXPECT_EQ(mgr.AddTimer(pStopTimer), SER_STOP);

    // enable the manager manually, then check timer content validation
    mgr.mNeedStop = false;
    MultiCastServiceTimer zeroSeqTimer(nullptr, nullptr, TEST_TIMER_TIMEOUT_SEC, 0, MultiCastSyncCBType::IO);
    zeroSeqTimer.SeqNo(TEST_INVALID_SEQ_NO);
    MultiCastServiceTimer *pZeroSeqTimer = &zeroSeqTimer;
    EXPECT_EQ(mgr.AddTimer(pZeroSeqTimer), SER_INVALID_PARAM);

    MultiCastServiceTimer noCbTimer(nullptr, nullptr, TEST_TIMER_TIMEOUT_SEC, 0, MultiCastSyncCBType::IO);
    noCbTimer.SeqNo(TEST_TIMER_SEQ_NO);
    MultiCastServiceTimer *pNoCbTimer = &noCbTimer;
    EXPECT_EQ(mgr.AddTimer(pNoCbTimer), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPeriodicManager, TestAddTimerAndTimeoutCleanup)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    mgr.mNeedStop = false;
    TestMultiCastCallback cb;
    auto *timer = new MultiCastServiceTimer(nullptr, nullptr, TEST_TIMER_TIMEOUT_SEC, reinterpret_cast<uintptr_t>(&cb),
                                            MultiCastSyncCBType::IO);
    timer->SeqNo(TEST_TIMER_SEQ_NO);
    EXPECT_EQ(mgr.AddTimer(timer), SER_OK);
    // never timeout and not finished, nothing is collected
    mgr.FillHandleQueue(0);
    EXPECT_TRUE(mgr.mHandleQueue[0].empty());
    // mark finished, the timer is detached and processed
    timer->MarkFinished();
    mgr.ProcessTimeOut(0);
    // ProcessTimer calls DecreaseRef, with no ctxStore/publisher the object survives
    delete timer;
}

TEST_F(TestMulticastPeriodicManager, TestProcessCleanUpInvalidThreadId)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    mgr.ProcessCleanUp(INVALID_THREAD_ID);
    mgr.mNeedStop = false;
}

TEST_F(TestMulticastPeriodicManager, TestRunInThreadInvalidId)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    mgr.RunInThread(-1);
    EXPECT_TRUE(mgr.mWorkerStartFailed.load());
    mgr.mWorkerStartFailed = false;
}

TEST_F(TestMulticastPeriodicManager, TestStartInvalidThreadCount)
{
    MultiCastPeriodicManager mgr(M_MAX_THREAD_NUM + 1, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(mgr.Start(), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPeriodicManager, TestStartAndStop)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(mgr.Start(), SER_OK);
    // start twice is idempotent
    EXPECT_EQ(mgr.Start(), SER_OK);
    mgr.Stop();
    // stop on stopped manager is a no-op
    mgr.Stop();
}

TEST_F(TestMulticastPeriodicManager, TestInitializeIoContextsInvalidCapacity)
{
    MultiCastPeriodicManager zeroMgr(TEST_THREAD_COUNT, "ut-mgr-zero", TEST_NO_CPU_BIND, TEST_ZERO_IO_CAPACITY,
                                     TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(zeroMgr.InitializeIoContexts(), SER_INVALID_PARAM);

    MultiCastPeriodicManager overflowMgr(TEST_THREAD_COUNT, "ut-mgr-overflow", TEST_NO_CPU_BIND,
                                         TEST_OVERFLOW_IO_CAPACITY, TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(overflowMgr.InitializeIoContexts(), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPeriodicManager, TestAcquireSubmitGetReleaseIoContext)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    TestMultiCastCallback cb;

    MultiCastIoContext *ctx = mgr.AcquireIoContext(publisher, &cb, TEST_NO_TIMEOUT);
    ASSERT_NE(ctx, nullptr);
    EXPECT_NE(ctx->SeqNo(), 0);
    // state is PREPARING, GetIoContext rejects until submitted
    EXPECT_EQ(mgr.GetIoContext(ctx->SeqNo()), nullptr);
    EXPECT_EQ(mgr.GetIoContext(TEST_INVALID_SEQ_NO), nullptr);
    EXPECT_EQ(mgr.GetIoContext(INVALID_WHEEL_SEQ), nullptr);

    mgr.SubmitIoContext(ctx);
    EXPECT_EQ(mgr.GetIoContext(ctx->SeqNo()), ctx);

    // release puts the context back to the free list and destroys the callback
    mgr.ReleaseIoContext(ctx);
    EXPECT_EQ(mgr.GetIoContext(ctx->SeqNo()), nullptr);
    EXPECT_TRUE(cb.destroyed);
    MultiCastIoContext *again = mgr.AcquireIoContext(publisher, nullptr, TEST_NO_TIMEOUT);
    EXPECT_EQ(again, ctx);
    // drop the access hold, otherwise UnInitializeIoContexts spins forever on a PREPARING context
    mgr.ReleaseIoContext(again);
    mgr.UnInitializeIoContexts();
    publisher->DecreaseRef();
}

TEST_F(TestMulticastPeriodicManager, TestIoContextTimeoutFlow)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    TestMultiCastCallback cb;

    MultiCastIoContext *ctx = mgr.AcquireIoContext(publisher, &cb, TEST_IMMEDIATE_TIMEOUT);
    ASSERT_NE(ctx, nullptr);
    mgr.SubmitIoContext(ctx);
    // first round: send is not ready, the context is parked in the wheel
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs());
    EXPECT_TRUE(ctx->mInWheel);

    // send ready and access released, next wheel round times the context out
    ctx->mSendReady = true;
    ctx->ReleaseAccess();
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs() + 1);
    EXPECT_EQ(cb.runCount, 1);
    EXPECT_EQ(ctx->mState.load(), MultiCastIoState::FREE);

    // the context returns to the free list and can be acquired again
    MultiCastIoContext *again = mgr.AcquireIoContext(publisher, nullptr, TEST_NO_TIMEOUT);
    EXPECT_EQ(again, ctx);
    mgr.ReleaseIoContext(again);
    mgr.UnInitializeIoContexts();
    publisher->DecreaseRef();
}

TEST_F(TestMulticastPeriodicManager, TestIoContextBrokenFlow)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    TestMultiCastCallback cb;

    MultiCastIoContext *ctx = mgr.AcquireIoContext(publisher, &cb, TEST_NO_TIMEOUT);
    ASSERT_NE(ctx, nullptr);
    mgr.SubmitIoContext(ctx);
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs());
    ctx->ReleaseAccess();

    // simulate link broken, the in-flight context is marked broken and finalized
    mgr.ProcessIoInBroken(publisher);
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs() + 1);
    EXPECT_EQ(cb.runCount, 1);
    EXPECT_EQ(ctx->mState.load(), MultiCastIoState::FREE);
    mgr.UnInitializeIoContexts();
    publisher->DecreaseRef();
}

TEST_F(TestMulticastPeriodicManager, TestIoContextCompleteFlow)
{
    MultiCastPeriodicManager mgr(TEST_THREAD_COUNT, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY,
                                 TEST_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    TestMultiCastCallback cb;

    MultiCastIoContext *ctx = mgr.AcquireIoContext(publisher, &cb, TEST_NO_TIMEOUT);
    ASSERT_NE(ctx, nullptr);
    mgr.SubmitIoContext(ctx);
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs());

    // wire the periodic manager, otherwise TryCompleteIoContext dereferences a null periodic manager
    publisher->mPeriodicMgr = reinterpret_cast<uintptr_t>(&mgr);
    // all subscribers replied before send completes, try complete marks it finished
    ctx->mSendReady = true;
    publisher->TryCompleteIoContext(ctx);
    EXPECT_EQ(cb.runCount, 1);
    ctx->ReleaseAccess();
    // complete queue is drained by the shard loop and the context is finalized
    mgr.ProcessIoShard(0, NetMonotonic::TimeMs() + 1);
    EXPECT_EQ(ctx->mState.load(), MultiCastIoState::FREE);
    mgr.UnInitializeIoContexts();
    publisher->DecreaseRef();
}

TEST_F(TestMulticastPeriodicManager, TestGetProducerShardStickyPerThread)
{
    MultiCastPeriodicManager mgr(2, "ut-mgr", TEST_NO_CPU_BIND, TEST_IO_CAPACITY, TEST_MAX_SUBSCRIBER_NUM);
    uint16_t first = mgr.GetProducerShard();
    EXPECT_LT(first, 2);
    EXPECT_EQ(mgr.GetProducerShard(), first);
}
} // namespace hcom
} // namespace ock
