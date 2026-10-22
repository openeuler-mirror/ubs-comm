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

#include "urma_poller.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <sstream>

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static urma_jfc_t g_rawJfcs[4];
static int g_createJfcCalls = 0;
static int g_createJfcFailAt = -1;
static int g_pollReturn = 0;
static UrmaPoller *g_busyPoller = nullptr;
static UrmaPoller *g_eventPoller = nullptr;

static urma_status_t MockDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}

static urma_status_t MockDeleteJfc(urma_jfc_t *)
{
    return URMA_SUCCESS;
}

static urma_jfc_t *MockCreateJfc(urma_context_t *, urma_jfc_cfg_t *)
{
    int index = g_createJfcCalls++;
    if (index == g_createJfcFailAt) {
        return nullptr;
    }
    return &g_rawJfcs[index % 4];
}

static int MockPollJfc(urma_jfc_t *, int, urma_cr_t *)
{
    if (g_busyPoller != nullptr) {
        g_busyPoller->stopping_ = true;
    }
    return g_pollReturn;
}

static int MockWaitJfc(urma_jfce_t *, uint32_t, int, urma_jfc_t *[])
{
    if (g_eventPoller != nullptr) {
        g_eventPoller->stopping_ = true;
    }
    return 0;
}

static ock::ubs::Result HandleCqe(uint32_t, int, urma_cr_t *)
{
    return UBS_OK;
}
} // namespace

class UrmaPollerTest : public testing::Test {
protected:
    void SetUp() override
    {
        savedMode_ = UrmaSetting::UB_EPOLL_HANDLE_MODE;
        UrmaSetting::UB_EPOLL_HANDLE_MODE = DISPATCHER_THREAD_POOL;
        memset(g_rawJfcs, 0, sizeof(g_rawJfcs));
        memset(&rawContext_, 0, sizeof(rawContext_));
        g_createJfcCalls = 0;
        g_createJfcFailAt = -1;
        g_pollReturn = 0;
        g_busyPoller = nullptr;
        g_eventPoller = nullptr;

        UrmaApi::urma_create_jfc_ptr = MockCreateJfc;
        UrmaApi::urma_delete_jfc_ptr = MockDeleteJfc;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;
        UrmaApi::urma_poll_jfc_ptr = MockPollJfc;
        UrmaApi::urma_wait_jfc_ptr = MockWaitJfc;

        device_ = MakeRef<UrmaDevice>("test_dev", "/sys/test", urma_device_attr_t{});
        context_ = MakeRef<UrmaContext>(&rawContext_, device_, urma_eid_info_t{});
    }

    void TearDown() override
    {
        context_ = nullptr;
        device_ = nullptr;
        UrmaSetting::UB_EPOLL_HANDLE_MODE = savedMode_;
        g_busyPoller = nullptr;
        g_eventPoller = nullptr;
        GlobalMockObject::verify();
    }

    UrmaJfcPtr MakeJfc()
    {
        return MakeRef<UrmaJfc>(BUSY_POLLING, &g_rawJfcs[0], nullptr, context_);
    }

    EpollHandleMode savedMode_;
    urma_context_t rawContext_{};
    UrmaDevicePtr device_;
    UrmaContextPtr context_;
};

TEST_F(UrmaPollerTest, OptionsStreamIncludesConfiguredValues)
{
    UrmaPollerOptions options;
    options.poller_count = 3;
    options.jfc_depth = 4096;
    options.polling_type = BUSY_POLLING;
    std::ostringstream out;
    out << options;
    EXPECT_NE(std::string::npos, out.str().find("3"));
    EXPECT_NE(std::string::npos, out.str().find("busy"));
}

TEST_F(UrmaPollerTest, StartAndStopAreNoOpsInDispatcherMode)
{
    auto jfc = MakeJfc();
    UrmaPoller poller(1, HandleCqe, jfc);
    EXPECT_EQ(UBS_OK, poller.Start());
    EXPECT_FALSE(poller.started_.load());
    poller.Stop();
    EXPECT_FALSE(poller.started_.load());
}

TEST_F(UrmaPollerTest, StartValidatesDedicatedModeDependencies)
{
    UrmaPoller nullJfcPoller(1, HandleCqe, nullptr);
    nullJfcPoller.handle_mode_caps_.dedicated_poller_thread = true;
    EXPECT_EQ(UBS_ERROR, nullJfcPoller.Start());
    nullJfcPoller.handle_mode_caps_.dedicated_poller_thread = false;

    auto jfc = MakeJfc();
    UrmaPoller nullHandlerPoller(2, nullptr, jfc);
    nullHandlerPoller.handle_mode_caps_.dedicated_poller_thread = true;
    EXPECT_EQ(UBS_ERROR, nullHandlerPoller.Start());
    nullHandlerPoller.handle_mode_caps_.dedicated_poller_thread = false;

    UrmaPoller startedPoller(3, HandleCqe, jfc);
    startedPoller.handle_mode_caps_.dedicated_poller_thread = true;
    startedPoller.started_.store(true);
    EXPECT_EQ(UBS_OK, startedPoller.Start());
    startedPoller.handle_mode_caps_.dedicated_poller_thread = false;
}

TEST_F(UrmaPollerTest, StartAndStopRunDedicatedBusyPollingThread)
{
    auto jfc = MakeJfc();
    UrmaPoller poller(4, HandleCqe, jfc);
    poller.handle_mode_caps_.dedicated_poller_thread = true;
    g_busyPoller = &poller;

    ASSERT_EQ(UBS_OK, poller.Start());
    EXPECT_TRUE(poller.started_.load());
    poller.Stop();
    EXPECT_FALSE(poller.started_.load());
    poller.handle_mode_caps_.dedicated_poller_thread = false;
}

TEST_F(UrmaPollerTest, StartAndStopRunDedicatedEventPollingThread)
{
    auto jfc = MakeRef<UrmaJfc>(EVENT_POLLING, &g_rawJfcs[0], nullptr, context_);
    UrmaPoller poller(5, HandleCqe, jfc);
    poller.handle_mode_caps_.dedicated_poller_thread = true;
    g_eventPoller = &poller;

    ASSERT_EQ(UBS_OK, poller.Start());
    EXPECT_TRUE(poller.started_.load());
    poller.Stop();
    EXPECT_FALSE(poller.started_.load());
    poller.handle_mode_caps_.dedicated_poller_thread = false;
}

TEST_F(UrmaPollerTest, PollAndDispatchRejectsInvalidState)
{
    UrmaPoller nullJfcPoller(1, HandleCqe, nullptr);
    EXPECT_EQ(UBS_ERROR, nullJfcPoller.PollAndDispatch());

    auto jfc = MakeJfc();
    UrmaPoller nullHandlerPoller(2, nullptr, jfc);
    EXPECT_EQ(UBS_ERROR, nullHandlerPoller.PollAndDispatch());

    UrmaPoller stoppingPoller(3, HandleCqe, jfc);
    stoppingPoller.stopping_ = true;
    EXPECT_EQ(UBS_ERROR, stoppingPoller.PollAndDispatch());
}

TEST_F(UrmaPollerTest, PollAndDispatchPropagatesPollFailure)
{
    auto jfc = MakeJfc();
    UrmaPoller poller(6, HandleCqe, jfc);
    MOCKER_CPP(&UrmaApi::urma_rearm_jfc)
        .expects(once())
        .with(eq(&g_rawJfcs[0]), eq(false))
        .will(returnValue(URMA_SUCCESS));
    g_pollReturn = -1;
    EXPECT_EQ(UBS_ERROR, poller.PollAndDispatch());
    EXPECT_TRUE(poller.stopping_);
    GlobalMockObject::verify();
}

TEST_F(UrmaPollerTest, BusyPollProcessesCompletionAndStops)
{
    auto jfc = MakeJfc();
    UrmaPoller poller(1, HandleCqe, jfc);
    g_pollReturn = 2;
    g_busyPoller = &poller;
    poller.BusyPoll();
    EXPECT_TRUE(poller.stopping_);
}

TEST_F(UrmaPollerTest, BusyPollHandlesEmptyPollAndStops)
{
    auto jfc = MakeJfc();
    UrmaPoller poller(1, HandleCqe, jfc);
    g_pollReturn = 0;
    g_busyPoller = &poller;
    poller.BusyPoll();
    EXPECT_TRUE(poller.stopping_);
}

TEST_F(UrmaPollerTest, RunInThreadExitsWhenJfcIsNull)
{
    UrmaPoller poller(1, HandleCqe, nullptr);
    poller.RunInThread();
    EXPECT_TRUE(poller.started_.load());
    poller.handle_mode_caps_.dedicated_poller_thread = false;
}

TEST_F(UrmaPollerTest, VerifyOptionsRejectsMissingDependencies)
{
    UrmaPollerSet set;
    UrmaPollerOptions options;
    options.cqe_handler = HandleCqe;
    EXPECT_EQ(UBS_ERROR, set.VerifyOptions(options));

    options.context_ = context_;
    options.cqe_handler = nullptr;
    EXPECT_EQ(UBS_ERROR, set.VerifyOptions(options));
}

TEST_F(UrmaPollerTest, VerifyOptionsNormalizesDepthAndCount)
{
    UrmaPollerSet set;
    UrmaPollerOptions options;
    options.context_ = context_;
    options.cqe_handler = HandleCqe;
    options.jfc_depth = 1;
    options.poller_count = 0;
    EXPECT_EQ(UBS_OK, set.VerifyOptions(options));
    EXPECT_EQ(SIZE_4K, set.options_.jfc_depth);
    EXPECT_EQ(2, set.options_.poller_count);

    options.poller_count = 129;
    EXPECT_EQ(UBS_OK, set.VerifyOptions(options));
    EXPECT_EQ(128, set.options_.poller_count);
}

TEST_F(UrmaPollerTest, InitCreatesPollersAndSelectsRoundRobin)
{
    UrmaPollerSet set;
    UrmaPollerOptions options;
    options.context_ = context_;
    options.cqe_handler = HandleCqe;
    options.polling_type = BUSY_POLLING;
    options.poller_count = 2;

    ASSERT_EQ(UBS_OK, set.Init(options));
    EXPECT_TRUE(set.inited_);
    EXPECT_EQ(2U, set.pollers_.size());
    EXPECT_EQ(UBS_OK, set.Init(options));

    UrmaJfcPtr first;
    UrmaJfcPtr second;
    EXPECT_EQ(UBS_OK, set.SelectPoller(first));
    EXPECT_EQ(UBS_OK, set.SelectPoller(second));
    EXPECT_NE(first.Get(), second.Get());

    set.UnInit();
    EXPECT_FALSE(set.inited_);
    EXPECT_TRUE(set.pollers_.empty());
    set.UnInit();
}

TEST_F(UrmaPollerTest, InitRollsBackWhenJfcCreationFails)
{
    UrmaPollerSet set;
    UrmaPollerOptions options;
    options.context_ = context_;
    options.cqe_handler = HandleCqe;
    options.polling_type = BUSY_POLLING;
    options.poller_count = 2;
    g_createJfcFailAt = 1;

    EXPECT_EQ(UBS_ERROR, set.Init(options));
    EXPECT_FALSE(set.inited_);
    EXPECT_TRUE(set.pollers_.empty());
}

TEST_F(UrmaPollerTest, SelectPollerCoversInvalidContainerEntries)
{
    UrmaPollerSet set;
    UrmaJfcPtr out;
    EXPECT_EQ(UBS_ERROR, set.SelectPoller(out));

    set.inited_ = true;
    set.options_.poller_count = 1;
    set.pollers_.emplace_back(nullptr);
    EXPECT_EQ(UBS_ERROR, set.SelectPoller(out));

    set.pollers_.clear();
    set.pollers_.emplace_back(MakeRef<UrmaPoller>(1, HandleCqe, nullptr));
    EXPECT_EQ(UBS_ERROR, set.SelectPoller(out));
    set.inited_ = false;
}
