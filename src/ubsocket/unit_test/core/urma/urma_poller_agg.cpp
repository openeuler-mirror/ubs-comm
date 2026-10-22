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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <sstream>

#include "urma_poller_agg.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
int g_pollReturns[4]{};
uint32_t g_pollIndex = 0;
uint32_t g_pollCount = 0;
uint32_t g_rearmCount = 0;
uint32_t g_pollCountAtRearm = 0;
uint32_t g_handlerCount = 0;
uint32_t g_handlerCqeCount = 0;
urma_jfce_t g_createdJfce{};
urma_jfc_t g_createdJfc[2]{};
uint32_t g_createdJfcCount = 0;
bool g_failCreateJfce = false;
bool g_failCreateJfc = false;

int MockPollJfc(urma_jfc_t *, int, urma_cr_t *)
{
    ++g_pollCount;
    return g_pollReturns[g_pollIndex < 4 ? g_pollIndex++ : 3];
}

urma_status_t MockRearmJfc(urma_jfc_t *, bool)
{
    g_pollCountAtRearm = g_pollCount;
    ++g_rearmCount;
    return URMA_SUCCESS;
}

int MockWaitJfc(urma_jfce_t *, uint32_t, int, urma_jfc_t *[])
{
    return 0;
}

void MockAckJfc(urma_jfc_t *[], uint32_t[], uint32_t) {}

urma_status_t MockDeleteJfc(urma_jfc_t *)
{
    return URMA_SUCCESS;
}

urma_status_t MockDeleteJfce(urma_jfce_t *)
{
    return URMA_SUCCESS;
}

urma_status_t MockDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}

urma_jfce_t *MockCreateJfce(urma_context_t *)
{
    return g_failCreateJfce ? nullptr : &g_createdJfce;
}

urma_jfc_t *MockCreateJfc(urma_context_t *, urma_jfc_cfg_t *)
{
    if (g_failCreateJfc || g_createdJfcCount >= 2) {
        return nullptr;
    }
    return &g_createdJfc[g_createdJfcCount++];
}

ock::ubs::Result HandleCqe(uint32_t, int count, urma_cr_t *)
{
    ++g_handlerCount;
    g_handlerCqeCount += static_cast<uint32_t>(count);
    return UBS_OK;
}
} // namespace

class UrmaPollerAggTest : public testing::Test {
protected:
    void SetUp() override
    {
        savedApiLoaded_ = UrmaApi::LOADED;
        UrmaApi::LOADED = true;
        g_pollIndex = 0;
        g_pollCount = 0;
        g_rearmCount = 0;
        g_pollCountAtRearm = 0;
        g_handlerCount = 0;
        g_handlerCqeCount = 0;
        g_createdJfcCount = 0;
        g_failCreateJfce = false;
        g_failCreateJfc = false;
        for (auto &value : g_pollReturns) {
            value = 0;
        }
        UrmaApi::urma_poll_jfc_ptr = MockPollJfc;
        UrmaApi::urma_rearm_jfc_ptr = MockRearmJfc;
        UrmaApi::urma_wait_jfc_ptr = MockWaitJfc;
        UrmaApi::urma_ack_jfc_ptr = MockAckJfc;
        UrmaApi::urma_delete_jfc_ptr = MockDeleteJfc;
        UrmaApi::urma_delete_jfce_ptr = MockDeleteJfce;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;
        UrmaApi::urma_create_jfce_ptr = MockCreateJfce;
        UrmaApi::urma_create_jfc_ptr = MockCreateJfc;

        device_ = MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{});
        context_ = MakeRef<UrmaContext>(&raw_context_, device_, urma_eid_info_t{});
        context_->context_id_ = 7;
        raw_jfce_.fd = 91;
        jfce_ = MakeRef<UrmaJfce>(&raw_jfce_, context_);
        jfc1_ = MakeRef<UrmaJfc>(EVENT_POLLING, &raw_jfc_[0], &raw_jfce_, context_);
        jfc2_ = MakeRef<UrmaJfc>(EVENT_POLLING, &raw_jfc_[1], &raw_jfce_, context_);
    }

    void TearDown() override
    {
        jfc2_ = nullptr;
        jfc1_ = nullptr;
        jfce_ = nullptr;
        context_ = nullptr;
        device_ = nullptr;
        UrmaApi::urma_poll_jfc_ptr = nullptr;
        UrmaApi::urma_rearm_jfc_ptr = nullptr;
        UrmaApi::urma_wait_jfc_ptr = nullptr;
        UrmaApi::urma_ack_jfc_ptr = nullptr;
        UrmaApi::urma_delete_jfc_ptr = nullptr;
        UrmaApi::urma_delete_jfce_ptr = nullptr;
        UrmaApi::urma_delete_context_ptr = nullptr;
        UrmaApi::urma_create_jfce_ptr = nullptr;
        UrmaApi::urma_create_jfc_ptr = nullptr;
        GlobalMockObject::verify();
        UrmaApi::LOADED = savedApiLoaded_;
    }

    UrmaAppPollerStartOptions MakeStartOptions()
    {
        UrmaAppPollerStartOptions options;
        options.jfce = jfce_;
        options.handler = HandleCqe;
        options.index = 3;
        options.jfcVector = {jfc1_};
        return options;
    }

    UrmaAggPollerOptions MakeSetOptions()
    {
        UrmaAggPollerOptions options;
        options.cqe_handler = HandleCqe;
        options.context_pool = MakeRef<UrmaContextPool>();
        return options;
    }

    urma_context_t raw_context_{};
    urma_jfce_t raw_jfce_{};
    urma_jfc_t raw_jfc_[2]{};
    UrmaDevicePtr device_{nullptr};
    UrmaContextPtr context_{nullptr};
    UrmaJfcePtr jfce_{nullptr};
    UrmaJfcPtr jfc1_{nullptr};
    UrmaJfcPtr jfc2_{nullptr};
    bool savedApiLoaded_{false};
};

TEST_F(UrmaPollerAggTest, Options_StreamContainsConfiguration)
{
    auto options = MakeSetOptions();
    options.run_in_thread = true;
    options.poller_count_per_context = 2;
    options.jfc_depth = 128;
    std::ostringstream os;
    os << options;
    EXPECT_NE(std::string::npos, os.str().find("poller thread:2"));
    EXPECT_NE(std::string::npos, os.str().find("jfc_depth: 128"));
}

TEST_F(UrmaPollerAggTest, Dedicated_StartSelectStopAndIdempotence)
{
    UrmaDedicatedJfcAggPoller poller;
    UrmaJfcPtr selected;
    EXPECT_EQ(UBS_ERROR, poller.SelectJfc(selected));
    auto options = MakeStartOptions();
    ASSERT_EQ(UBS_OK, poller.Start(options));
    EXPECT_EQ(UBS_OK, poller.Start(options));
    EXPECT_EQ(jfce_.Get(), poller.Jfce().Get());
    ASSERT_EQ(UBS_OK, poller.SelectJfc(selected));
    EXPECT_EQ(jfc1_.Get(), selected.Get());
    poller.Stop();
    poller.Stop();
    EXPECT_EQ(nullptr, poller.Jfce().Get());
}

TEST_F(UrmaPollerAggTest, Dedicated_SelectJfcUsesRoundRobin)
{
    UrmaDedicatedJfcAggPoller poller;
    auto options = MakeStartOptions();
    options.jfcVector.push_back(jfc2_);
    ASSERT_EQ(UBS_OK, poller.Start(options));
    UrmaJfcPtr first;
    UrmaJfcPtr second;
    ASSERT_EQ(UBS_OK, poller.SelectJfc(first));
    ASSERT_EQ(UBS_OK, poller.SelectJfc(second));
    EXPECT_EQ(jfc1_.Get(), first.Get());
    EXPECT_EQ(jfc2_.Get(), second.Get());
}

TEST_F(UrmaPollerAggTest, Dedicated_PollValidatesState)
{
    UrmaDedicatedJfcAggPoller poller;
    EXPECT_EQ(UBS_ERROR, poller.poll());
    auto options = MakeStartOptions();
    options.handler = nullptr;
    ASSERT_EQ(UBS_OK, poller.Start(options));
    EXPECT_EQ(UBS_ERROR, poller.poll());
}

TEST_F(UrmaPollerAggTest, Dedicated_PollDispatchesAndRearms)
{
    g_pollReturns[0] = 2;
    g_pollReturns[1] = 0;
    UrmaDedicatedJfcAggPoller poller;
    ASSERT_EQ(UBS_OK, poller.Start(MakeStartOptions()));
    EXPECT_EQ(UBS_OK, poller.poll());
    EXPECT_EQ(2u, g_pollCount);
    EXPECT_EQ(1u, g_rearmCount);
    EXPECT_EQ(0u, g_pollCountAtRearm);
    EXPECT_EQ(1u, g_handlerCount);
    EXPECT_EQ(2u, g_handlerCqeCount);
}

TEST_F(UrmaPollerAggTest, Dedicated_PollPropagatesDriverFailure)
{
    g_pollReturns[0] = -1;
    UrmaDedicatedJfcAggPoller poller;
    ASSERT_EQ(UBS_OK, poller.Start(MakeStartOptions()));
    EXPECT_EQ(UBS_ERROR, poller.poll());
}

TEST_F(UrmaPollerAggTest, Shared_SelectAndPollCoverStoredState)
{
    UrmaSharedJfcAggPollerWithThread poller;
    UrmaJfcPtr selected;
    EXPECT_EQ(UBS_ERROR, poller.SelectJfc(selected));
    poller.jfce_ = jfce_;
    poller.jfc_vector_ = {jfc1_, jfc2_};
    ASSERT_EQ(UBS_OK, poller.SelectJfc(selected));
    EXPECT_EQ(jfc1_.Get(), selected.Get());
    ASSERT_EQ(UBS_OK, poller.SelectJfc(selected));
    EXPECT_EQ(jfc2_.Get(), selected.Get());
    EXPECT_EQ(jfce_.Get(), poller.Jfce().Get());
    EXPECT_EQ(UBS_OK, poller.poll());
    poller.jfce_ = nullptr;
    poller.jfc_vector_.clear();
}

TEST_F(UrmaPollerAggTest, Shared_StartRejectsNullHandler)
{
    UrmaSharedJfcAggPollerWithThread poller;
    auto options = MakeStartOptions();
    options.handler = nullptr;
    EXPECT_EQ(UBS_ERROR, poller.Start(options));
    EXPECT_FALSE(poller.started_.load());
}

TEST_F(UrmaPollerAggTest, Shared_StartAndStopCoversBusyThreadLifecycle)
{
    UrmaSharedJfcAggPollerWithThread poller;
    auto options = MakeStartOptions();
    options.busyPoll = true;

    ASSERT_EQ(UBS_OK, poller.Start(options));
    EXPECT_TRUE(poller.started_.load());
    EXPECT_EQ(UBS_OK, poller.Start(options));
    poller.Stop();
    EXPECT_FALSE(poller.started_.load());
    poller.Stop();
}

TEST_F(UrmaPollerAggTest, Shared_StartAndStopCoversEventThreadLifecycle)
{
    UrmaSharedJfcAggPollerWithThread poller;
    auto options = MakeStartOptions();
    options.busyPoll = false;
    MOCKER_CPP(&UrmaSharedJfcAggPollerWithThread::SetupWakeupJetty)
        .expects(once())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&UrmaSharedJfcAggPollerWithThread::Wakeup).expects(once());
    MOCKER_CPP(&UrmaSharedJfcAggPollerWithThread::DestroyWakeupJetty).expects(once());

    ASSERT_EQ(UBS_OK, poller.Start(options));
    EXPECT_TRUE(poller.started_.load());
    poller.Stop();
    EXPECT_FALSE(poller.started_.load());
}

TEST_F(UrmaPollerAggTest, Shared_WakeupSetupFailureDoesNotStartThread)
{
    UrmaSharedJfcAggPollerWithThread poller;
    auto options = MakeStartOptions();
    options.busyPoll = false;
    MOCKER_CPP(&UrmaSharedJfcAggPollerWithThread::SetupWakeupJetty)
        .expects(once())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    MOCKER_CPP(&UrmaSharedJfcAggPollerWithThread::Wakeup).expects(never());

    EXPECT_EQ(UBS_ERROR, poller.Start(options));
    EXPECT_FALSE(poller.started_.load());
    EXPECT_FALSE(poller.thread_.joinable());
    poller.Stop();
}

TEST_F(UrmaPollerAggTest, Shared_WakeupRegistrationFailureReleasesBuffer)
{
    UrmaSharedJfcAggPollerWithThread poller;
    poller.jfc_vector_ = {jfc1_};
    MOCKER_CPP(&UrmaApi::urma_register_seg)
        .expects(once())
        .will(returnValue(static_cast<urma_target_seg_t *>(nullptr)));
    MOCKER_CPP(&UrmaContext::CreateJfsJfrJetty).expects(never());

    EXPECT_EQ(UBS_ERROR, poller.SetupWakeupJetty());
    EXPECT_EQ(nullptr, poller.wakeup_buf_);
    EXPECT_EQ(nullptr, poller.wakeup_seg_);
    EXPECT_EQ(nullptr, poller.wakeup_jetty_.Get());
}

TEST_F(UrmaPollerAggTest, Shared_WakeupResourceFailureUnregistersSegment)
{
    urma_target_seg_t segment{};
    UrmaSharedJfcAggPollerWithThread poller;
    poller.jfc_vector_ = {jfc1_};
    MOCKER_CPP(&UrmaApi::urma_register_seg).expects(once()).will(returnValue(&segment));
    MOCKER_CPP(&UrmaContext::CreateJfsJfrJetty)
        .expects(once())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    MOCKER_CPP(&UrmaApi::urma_unregister_seg).expects(once()).will(returnValue(URMA_SUCCESS));

    EXPECT_EQ(UBS_ERROR, poller.SetupWakeupJetty());
    EXPECT_EQ(nullptr, poller.wakeup_buf_);
    EXPECT_EQ(nullptr, poller.wakeup_seg_);
    EXPECT_EQ(nullptr, poller.wakeup_jetty_.Get());
}

TEST_F(UrmaPollerAggTest, Shared_BusyPollDispatchesUntilStopped)
{
    UrmaSharedJfcAggPollerWithThread poller;
    poller.jfc_vector_ = {jfc1_};
    poller.cqe_handler_ = [&poller](uint32_t, int, urma_cr_t *) {
        ++g_handlerCount;
        poller.stopping_ = true;
        return UBS_OK;
    };
    g_pollReturns[0] = 1;
    poller.BusyPoll();
    EXPECT_EQ(1u, g_handlerCount);
}

TEST_F(UrmaPollerAggTest, Shared_EventPollRejectsInvalidJfcCount)
{
    UrmaSharedJfcAggPollerWithThread poller;
    poller.EventPoll();
    poller.jfc_vector_.resize(65, jfc1_);
    poller.EventPoll();
    SUCCEED();
}

TEST_F(UrmaPollerAggTest, Shared_DrainCqesHandlesEmptySuccessAndFailure)
{
    UrmaSharedJfcAggPollerWithThread poller;
    poller.cqe_handler_ = HandleCqe;
    urma_jfc_t *events[2] = {nullptr, &raw_jfc_[0]};
    g_pollReturns[0] = 1;
    g_pollReturns[1] = 0;
    EXPECT_EQ(UBS_OK, poller.DrainCqes(events, 2));
    EXPECT_EQ(1u, g_handlerCount);
    EXPECT_EQ(2u, g_pollCount);
    EXPECT_EQ(1u, g_rearmCount);
    EXPECT_EQ(0u, g_pollCountAtRearm);

    poller.stopping_ = false;
    g_pollIndex = 0;
    g_pollReturns[0] = -1;
    EXPECT_EQ(UBS_ERROR, poller.DrainCqes(events + 1, 1));
    EXPECT_TRUE(poller.stopping_);
    EXPECT_EQ(3u, g_pollCount);
    EXPECT_EQ(2u, g_rearmCount);
    EXPECT_EQ(2u, g_pollCountAtRearm);
}

TEST_F(UrmaPollerAggTest, PollerSet_VerifyOptionsAndEmptyPoolLifecycle)
{
    UrmaAggPollerSet pollers;
    auto options = MakeSetOptions();
    options.poller_count_per_context = 17;
    EXPECT_EQ(UBS_INVALID_PARAM, pollers.Init(options));
    options.poller_count_per_context = 1;
    options.cqe_handler = nullptr;
    EXPECT_EQ(UBS_INVALID_PARAM, pollers.Init(options));
    options.cqe_handler = HandleCqe;
    options.context_pool = nullptr;
    EXPECT_EQ(UBS_INVALID_PARAM, pollers.Init(options));

    options = MakeSetOptions();
    EXPECT_EQ(UBS_OK, pollers.Init(options));
    EXPECT_EQ(UBS_OK, pollers.Init(options));
    EXPECT_TRUE(pollers.GetAllPollers().empty());
    pollers.UnInit();
    pollers.UnInit();
}

TEST_F(UrmaPollerAggTest, PollerSet_CreateDedicatedPollerAndSelectJfc)
{
    UrmaAggPollerSet pollers;
    auto options = MakeSetOptions();
    options.poller_count_per_context = 2;
    options.context_pool->contexts_[{"ut_dev", 0}] = context_;
    ASSERT_EQ(UBS_OK, pollers.Init(options));
    ASSERT_EQ(1u, pollers.GetAllPollers().size());
    UrmaJfcPtr selected;
    EXPECT_EQ(UBS_OK, pollers.SelectJfc(context_, selected));
    EXPECT_NE(nullptr, selected.Get());
    EXPECT_EQ(2u, g_createdJfcCount);
}

TEST_F(UrmaPollerAggTest, PollerSet_CreateThreadedPollerAndStop)
{
    UrmaAggPollerSet pollers;
    auto options = MakeSetOptions();
    options.run_in_thread = true;
    options.polling_type = BUSY_POLLING;
    options.context_pool->contexts_[{"ut_dev", 0}] = context_;

    ASSERT_EQ(UBS_OK, pollers.Init(options));
    ASSERT_EQ(1U, pollers.GetAllPollers().size());
    UrmaJfcPtr selected;
    EXPECT_EQ(UBS_OK, pollers.SelectJfc(context_, selected));
    EXPECT_NE(nullptr, selected.Get());
    pollers.UnInit();
    EXPECT_TRUE(pollers.GetAllPollers().empty());
}

TEST_F(UrmaPollerAggTest, PollerSet_CreatePollerRollsBackDriverFailures)
{
    auto options = MakeSetOptions();
    options.context_pool->contexts_[{"ut_dev", 0}] = context_;
    UrmaAggPollerSet jfceFailure;
    g_failCreateJfce = true;
    EXPECT_EQ(UBS_ERROR, jfceFailure.Init(options));
    EXPECT_TRUE(jfceFailure.GetAllPollers().empty());

    g_failCreateJfce = false;
    g_failCreateJfc = true;
    UrmaAggPollerSet jfcFailure;
    EXPECT_EQ(UBS_ERROR, jfcFailure.Init(options));
    EXPECT_TRUE(jfcFailure.GetAllPollers().empty());
}

TEST_F(UrmaPollerAggTest, PollerSet_SelectJfcValidatesContextAndPoller)
{
    UrmaAggPollerSet pollers;
    UrmaJfcPtr selected;
    EXPECT_EQ(UBS_INVALID_PARAM, pollers.SelectJfc(nullptr, selected));
    EXPECT_EQ(UBS_ERROR, pollers.SelectJfc(context_, selected));
    pollers.pollers_[context_->Id()] = nullptr;
    EXPECT_EQ(UBS_ERROR, pollers.SelectJfc(context_, selected));
}
