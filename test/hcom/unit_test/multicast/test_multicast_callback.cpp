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
#include <vector>

#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_service_callback.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint32_t TEST_SUB_ID_A = 11;
constexpr uint32_t TEST_SUB_ID_B = 12;
constexpr uint32_t TEST_SUB_ID_C = 13;
constexpr uint64_t TEST_START_TIME = 1234;
constexpr int32_t NEVER_TIMEOUT = -1;
constexpr int32_t SHORT_TIMEOUT_SEC = 1;
constexpr uint64_t TEST_EXPIRED_TIMEOUT_SEC = 1;
constexpr uint32_t TEST_POOL_ROUND = 5;

void CountRun(int *counter, PublisherContext &)
{
    (*counter)++;
}
} // namespace

class TestCallback : public MultiCastCallback {
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
        return permanent;
    }

    void Destroy() override
    {
        destroyed = true;
    }

    int runCount = 0;
    uint64_t startTime = 0;
    bool permanent = false;
    bool destroyed = false;
};

class TestMulticastCallback : public testing::Test {
public:
    TestMulticastCallback() = default;
    ~TestMulticastCallback() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestMulticastCallback::SetUp() {}

void TestMulticastCallback::TearDown() {}

TEST_F(TestMulticastCallback, TestCallbackPoolAcquireRelease)
{
    MultiCastCallbackPool &pool = MultiCastCallbackPool::Instance();
    EXPECT_TRUE(pool.Initialize());
    void *slot = pool.Acquire();
    ASSERT_NE(slot, nullptr);
    pool.Release(slot);
    for (uint32_t round = 0; round < TEST_POOL_ROUND; round++) {
        void *retry = pool.Acquire();
        EXPECT_EQ(retry, slot);
        pool.Release(retry);
    }
}

TEST_F(TestMulticastCallback, TestPooledCallbackLifeCycle)
{
    int runCount = 0;
    MultiCastCallback *cb = NewMultiCastCallback(&CountRun, &runCount, std::placeholders::_1);
    ASSERT_NE(cb, nullptr);
    EXPECT_FALSE(cb->Permanent());
    cb->SetTime(TEST_START_TIME);
    EXPECT_EQ(cb->GetTime(), TEST_START_TIME);
    PublisherContext ctx;
    // pooled callback is self-deleting: Run destroys it and returns the slot to the pool
    cb->Run(ctx);
    EXPECT_EQ(runCount, 1);
    // the slot must be reusable afterwards
    void *slot = MultiCastCallbackPool::Instance().Acquire();
    EXPECT_NE(slot, nullptr);
    MultiCastCallbackPool::Instance().Release(slot);
}

TEST_F(TestMulticastCallback, TestPermanentCallbackNotDestroyed)
{
    int runCount = 0;
    // permanent callback constructed directly: service_common.h defines another
    // NewPermanentCallback template in the same namespace, which makes the
    // multicast helper ambiguous with a plain function argument
    auto closure = std::bind(&CountRun, &runCount, std::placeholders::_1);
    auto *cb = new MultiCastClosureCallback<decltype(closure)>(std::move(closure), false, false);
    ASSERT_NE(cb, nullptr);
    EXPECT_TRUE(cb->Permanent());
    DestroyCallback(cb);
    // permanent callback survives DestroyCallback and can still run
    PublisherContext ctx;
    cb->Run(ctx);
    EXPECT_EQ(runCount, 1);
    cb->SetTime(TEST_START_TIME);
    EXPECT_EQ(cb->GetTime(), TEST_START_TIME);
    delete cb;
}

TEST_F(TestMulticastCallback, TestDestroyCallbackNullAndPermanent)
{
    DestroyCallback(nullptr);
    TestCallback cb;
    cb.permanent = true;
    DestroyCallback(&cb);
    EXPECT_FALSE(cb.destroyed);
}

TEST_F(TestMulticastCallback, TestServiceTimerTimeoutState)
{
    MultiCastServiceTimer neverTimeoutTimer(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    EXPECT_EQ(neverTimeoutTimer.Timeout(), 0);
    EXPECT_FALSE(neverTimeoutTimer.IsTimeOut());

    MultiCastServiceTimer futureTimer(nullptr, nullptr, SHORT_TIMEOUT_SEC, 0, MultiCastSyncCBType::IO);
    EXPECT_FALSE(futureTimer.IsTimeOut());
    // already expired moment, uptime seconds are always above this after boot
    futureTimer.mTimeout = TEST_EXPIRED_TIMEOUT_SEC;
    EXPECT_TRUE(futureTimer.IsTimeOut());

    EXPECT_NE(futureTimer.State(), MultiCastAsyncCBState::FINISHED);
    futureTimer.MarkFinished();
    EXPECT_TRUE(futureTimer.IsFinished());
    futureTimer.MarkTimeout();
    EXPECT_EQ(futureTimer.State(), MultiCastAsyncCBState::TIMEOUT);
    futureTimer.TimeoutDump();
    futureTimer.BrokenDump();
}

TEST_F(TestMulticastCallback, TestServiceTimerRunAndDeleteCallback)
{
    TestCallback cb;
    MultiCastServiceTimer timer(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    timer.mCallback = reinterpret_cast<uintptr_t>(&cb);
    PublisherContext ctx;
    timer.RunCallBack(ctx);
    EXPECT_EQ(cb.runCount, 1);
    // callback pointer is cleared after run
    timer.RunCallBack(ctx);
    EXPECT_EQ(cb.runCount, 1);

    timer.mCallback = reinterpret_cast<uintptr_t>(&cb);
    timer.DeleteCallBack();
    EXPECT_TRUE(cb.destroyed);
    timer.DeleteCallBack();
    EXPECT_EQ(cb.runCount, 1);
}

TEST_F(TestMulticastCallback, TestServiceTimerRefCounter)
{
    auto *timer = new MultiCastServiceTimer(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    timer->IncreaseRef();
    EXPECT_EQ(timer->GetRef(), 1);
    timer->DecreaseRef(); // refcount reaches zero, object returns itself (no ctxStore, stays alive)
    delete timer;
}

TEST_F(TestMulticastCallback, TestTimerEraseWithoutCtxStore)
{
    MultiCastServiceTimer timer(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    timer.EraseSeqNo();
    EXPECT_FALSE(timer.EraseSeqNoWithRet());
}

TEST_F(TestMulticastCallback, TestTimerListHeaderAddRemove)
{
    MultiCastTimerListHeader header;
    MultiCastServiceTimer timerA(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    MultiCastServiceTimer timerB(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::BROKEN);
    MultiCastServiceTimer timerC(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    timerA.SeqNo(TEST_SUB_ID_A);
    timerB.SeqNo(TEST_SUB_ID_B);
    timerC.SeqNo(TEST_SUB_ID_C);
    header.AddTimerCtx(nullptr);
    header.AddTimerCtx(&timerA);
    header.AddTimerCtx(&timerB);
    header.AddTimerCtx(&timerC);
    EXPECT_EQ(header.GetCtxCount(), 3);

    header.RemoveTimerCtx(&timerB);
    EXPECT_EQ(header.GetCtxCount(), 2);
    // removing a timer that is not in the list is a no-op
    MultiCastServiceTimer outsider(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    header.RemoveTimerCtx(&outsider);
    EXPECT_EQ(header.GetCtxCount(), 2);

    std::vector<MultiCastServiceTimer *> remainCtx;
    header.GetTimerCtx(remainCtx);
    EXPECT_EQ(remainCtx.size(), 2);
    EXPECT_EQ(header.GetCtxCount(), 0);
    header.GetTimerCtx(remainCtx);
    EXPECT_TRUE(remainCtx.empty());
}

TEST_F(TestMulticastCallback, TestServiceTimerCompare)
{
    MultiCastServiceTimer early(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    MultiCastServiceTimer late(nullptr, nullptr, NEVER_TIMEOUT, 0, MultiCastSyncCBType::IO);
    early.mTimeout = 100;
    late.mTimeout = 200;
    MultiCastServiceTimer *pEarly = &early;
    MultiCastServiceTimer *pLate = &late;
    MultiCastServiceTimerCompare compare;
    // the comparator prefers the timer with the larger timeout
    EXPECT_TRUE(compare(pLate, pEarly));
    EXPECT_FALSE(compare(pEarly, pLate));
    // same timeout, larger seqNo first
    late.mTimeout = early.mTimeout;
    early.SeqNo(TEST_SUB_ID_B);
    late.SeqNo(TEST_SUB_ID_A);
    EXPECT_TRUE(compare(pEarly, pLate));
    EXPECT_FALSE(compare(pLate, pEarly));
}

TEST_F(TestMulticastCallback, TestPublisherContextStatusFlow)
{
    std::string ipA = "10.0.0.1";
    std::string ipB = "10.0.0.2";
    SubscriptionInfoPtr subA(new SubscriptionInfo(TEST_SUB_ID_A, "subA", ipA, 0, nullptr));
    SubscriptionInfoPtr subB(new SubscriptionInfo(TEST_SUB_ID_B, "subB", ipB, 0, nullptr));
    std::vector<SubscriptionInfoPtr> subs = {subA, subB};

    PublisherContext ctx(2);
    EXPECT_EQ(ctx.InitSubscribers(subs), SER_OK);
    EXPECT_EQ(ctx.GetSubscriberRspInfo().size(), subs.size());
    EXPECT_EQ(ctx.GetReplyCount(), 0);

    EXPECT_FALSE(ctx.SetResponseStatus(nullptr, nullptr, SubscriberRspStatus::SUCCESS));
    // match is by id OR ip, the unknown sub must differ in both
    std::string unknownIp = "10.0.0.99";
    SubscriptionInfoPtr unknown(new SubscriptionInfo(TEST_SUB_ID_C, "unknown", unknownIp, 0, nullptr));
    EXPECT_FALSE(ctx.SetResponseStatus(unknown, nullptr, SubscriberRspStatus::SUCCESS));
    EXPECT_TRUE(ctx.SetResponseStatus(subA, nullptr, SubscriberRspStatus::SUCCESS));
    // status no longer INIT, second update fails
    EXPECT_FALSE(ctx.SetResponseStatus(subA, nullptr, SubscriberRspStatus::SEND_ERROR));
    ctx.MarkReplied(subB, nullptr);
    EXPECT_EQ(ctx.GetReplyCount(), 1);

    ctx.SetSendCount(2);
    EXPECT_EQ(ctx.GetSendCount(), 2);
    ctx.Reset();
    EXPECT_EQ(ctx.GetSendCount(), 0);
    EXPECT_EQ(ctx.GetReplyCount(), 0);
    EXPECT_TRUE(ctx.GetSubscriberRspInfo().empty());
}
} // namespace hcom
} // namespace ock
