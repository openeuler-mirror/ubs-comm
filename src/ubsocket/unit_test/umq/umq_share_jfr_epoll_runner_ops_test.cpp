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

#include "umq_share_jfr_epoll_runner_ops.h"

#include <sys/epoll.h>
#include <cerrno>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_proto.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "umq_errno.h"
#include "umq_pro_types.h"
#include "umq_socket.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
constexpr int TEST_EPOLL_FD = 10;
constexpr int TEST_EVENT_FD = 11;
constexpr uint64_t TEST_UMQ_HANDLE = 100;
constexpr uint64_t TEST_UMQ_HANDLE_2 = 200;
umq_buf_t *g_pollBuf = nullptr;
umq_buf_t *g_badBuf = nullptr;
int g_pollCallCount = 0;
int g_shutdownFd = -1;
int g_dispatchCount = 0;
int g_dispatchFlushCount = 0;

int PollOneBuffer(uint64_t, umq_io_option_t *, umq_buf_t **buffers, uint32_t)
{
    buffers[0] = g_pollBuf;
    return 1;
}

int PollOneBufferThenEmpty(uint64_t, umq_io_option_t *, umq_buf_t **buffers, uint32_t)
{
    if (g_pollCallCount++ == 0) {
        buffers[0] = g_pollBuf;
        return 1;
    }
    return 0;
}

int PollOneBufferTwiceThenEmpty(uint64_t, umq_io_option_t *, umq_buf_t **buffers, uint32_t)
{
    if (g_pollCallCount++ < 2) {
        buffers[0] = g_pollBuf;
        return 1;
    }
    return 0;
}

int ShutdownSuccess(int fd, int)
{
    g_shutdownFd = fd;
    return 0;
}

void DispatchEvent(uint64_t, uint32_t)
{
    ++g_dispatchCount;
}

void DispatchFlush()
{
    ++g_dispatchFlushCount;
}

int GetCqEventFailureAndRemoveSocket(uint64_t, umq_interrupt_option_t *)
{
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, nullptr);
    errno = EIO;
    return UMQ_FAIL;
}

int PostFailureWithBadBuffer(uint64_t, umq_buf_t *, umq_io_option_t *, umq_buf_t **badBuffer)
{
    *badBuffer = g_badBuf;
    return UMQ_FAIL;
}

epoll_event MakeRunnerEvent(uint64_t type)
{
    RunnerEventData data{};
    data.event_data.type = type;
    data.event_data.data = TEST_EVENT_FD;
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = data.u64;
    return event;
}

epoll_event MakeSubUmqEvent(Socket *socket)
{
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_SUB_UMQ_RX;
    data.event_data.data = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(socket));
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = data.u64;
    return event;
}

UmqSocketPtr MakeDataPlaneSocket()
{
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    socket->umq_handle_ = TEST_UMQ_HANDLE;
    auto *txOps = socket->ReinitTxOps();
    auto *rxOps = socket->ReinitRxOps();
    auto *entry = DataPlaneTable::Live(TEST_EVENT_FD);
    if (entry != nullptr) {
        entry->txw.tx_ops_ = txOps;
        entry->rxw.rx_ops_ = rxOps;
    }
    return socket;
}
} // namespace

class UmqShareJfrEpollRunnerOpsTest : public testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        g_pollBuf = nullptr;
        g_badBuf = nullptr;
        g_pollCallCount = 0;
        g_shutdownFd = -1;
        g_dispatchCount = 0;
        g_dispatchFlushCount = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UmqSetting::UMQ_TP_TYPE = POOL;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = false;
        GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD = 0;
        GlobalSetting::UBS_TX_POLLER_ACTIVE_YIELD = false;
        GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 200;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_POLLER_OPS = nullptr;
        auto &poller = TxCqePoller::Instance();
        poller.stopped_.store(false, std::memory_order_release);
        poller.any_inflight_.store(false, std::memory_order_release);
        poller.SetSleeping(false, std::memory_order_release);
    }

    void TearDown() override
    {
        auto &poller = TxCqePoller::Instance();
        poller.stopped_.store(false, std::memory_order_release);
        poller.any_inflight_.store(false, std::memory_order_release);
        poller.SetSleeping(false, std::memory_order_release);
        LibcApi::shutdown_ptr = nullptr;
        GlobalSetting::UBS_POLLER_OPS = nullptr;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }
};

TEST_F(UmqShareJfrEpollRunnerOpsTest, InsertJfrMainUmq_FirstRegistration_AddsMappingAndList)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event{};
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.InsertJfrMainUmq(TEST_EVENT_FD, TEST_UMQ_HANDLE, TEST_EPOLL_FD, &event), 0);
    EXPECT_EQ(ops.jfr_main_umq_.at(TEST_EVENT_FD), TEST_UMQ_HANDLE);
    ASSERT_EQ(ops.jfr_main_umq_list_.size(), 1U);
    EXPECT_EQ(ops.jfr_main_umq_list_.front(), TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, InsertJfrMainUmq_DuplicateFd_KeepsOriginalMapping)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event{};
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));

    ASSERT_EQ(ops.InsertJfrMainUmq(TEST_EVENT_FD, TEST_UMQ_HANDLE, TEST_EPOLL_FD, &event), 0);
    EXPECT_EQ(ops.InsertJfrMainUmq(TEST_EVENT_FD, TEST_UMQ_HANDLE_2, TEST_EPOLL_FD, &event), 0);
    EXPECT_EQ(ops.jfr_main_umq_.at(TEST_EVENT_FD), TEST_UMQ_HANDLE);
    EXPECT_EQ(ops.jfr_main_umq_list_.size(), 1U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, InsertJfrMainUmq_EpollCtlFails_DoesNotRegister)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event{};
    MOCKER(epoll_ctl).expects(once()).will(returnValue(-1));

    EXPECT_EQ(ops.InsertJfrMainUmq(TEST_EVENT_FD, TEST_UMQ_HANDLE, TEST_EPOLL_FD, &event), -1);
    EXPECT_TRUE(ops.jfr_main_umq_.empty());
    EXPECT_TRUE(ops.jfr_main_umq_list_.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_TxTimerEpollCtlSucceeds_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TX_CQE_TIMER);
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, nullptr), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_TxWakeEpollCtlFails_ReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TX_WAKE);
    MOCKER(epoll_ctl).expects(once()).will(returnValue(-1));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, nullptr), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_ShareJfrWithoutContext_ReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER(epoll_ctl).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, nullptr), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_ShareJfrWithoutRearm_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    UmqShareJfrEpollRunnerOps::ShareJfrExtContext context;
    context.umq_handle = TEST_UMQ_HANDLE;
    context.should_rearm_interrupt = false;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, &context), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_ShareJfrEpollCtlFails_ReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    UmqShareJfrEpollRunnerOps::ShareJfrExtContext context;
    context.umq_handle = TEST_UMQ_HANDLE;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(-1));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, &context), UBS_ERROR);
    EXPECT_TRUE(ops.jfr_main_umq_.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_RearmFails_MapsSavedErrno)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    UmqShareJfrEpollRunnerOps::ShareJfrExtContext context;
    context.umq_handle = TEST_UMQ_HANDLE;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(UMQ_FAIL));
    errno = EINVAL;

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, &context), UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, AddEventToRunner_RearmSucceeds_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    UmqShareJfrEpollRunnerOps::ShareJfrExtContext context;
    context.umq_handle = TEST_UMQ_HANDLE;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, &context), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, DelEpollEvent_EpollCtlSucceeds_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.DelEpollEvent(TEST_EPOLL_FD, TEST_EVENT_FD), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, DelEpollEvent_EpollCtlFails_ReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    MOCKER(epoll_ctl).expects(once()).will(returnValue(-1));

    EXPECT_EQ(ops.DelEpollEvent(TEST_EPOLL_FD, TEST_EVENT_FD), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_ShareJfrGetCqEventFails_MapsSavedErrno)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(UMQ_FAIL));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));
    errno = EINVAL;

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_ShareJfrNoCqEvents_SkipsRearm)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    EXPECT_EQ(ops.event_num_, 0U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_ShareJfrEventBelowAckThreshold_AccumulatesEventCount)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(1));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_ack_interrupt).expects(exactly(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    EXPECT_EQ(ops.event_num_, 1U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_ShareJfrRearmFails_MapsErrno)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(1));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(UMQ_FAIL));
    MOCKER_CPP(::umq_ack_interrupt).expects(exactly(0));
    errno = EIO;

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    EXPECT_EQ(errno, EIO);
    EXPECT_EQ(ops.event_num_, 1U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_ShareJfrAckThresholdReached_AcksAndResetsCount)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.event_num_ = GET_PER_ACK - 1;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(1));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_ack_interrupt).expects(once());

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    EXPECT_EQ(ops.event_num_, 0U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_PollFails_MapsSavedErrno)
{
    UmqShareJfrEpollRunnerOps ops;
    int totalPolled = 7;
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(UMQ_FAIL));
    errno = ENOMEM;

    EXPECT_FALSE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(errno, ENOMEM);
    EXPECT_EQ(totalPolled, 7);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_NoBuffers_ReturnsFalseWithoutChangingTotal)
{
    UmqShareJfrEpollRunnerOps ops;
    int totalPolled = 7;
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(0));

    EXPECT_FALSE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(totalPolled, 7);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_LegacyShareJfrNoBuffers_ReturnsErrorWithoutRearm)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_get_cq_event).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, PollMainUmqRxAll_NoRegisteredHandles_ReturnsFalse)
{
    UmqShareJfrEpollRunnerOps ops;
    MOCKER_CPP(::umq_poll).expects(exactly(0)).will(returnValue(0));

    EXPECT_FALSE(ops.PollMainUmqRxAll());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, PollMainUmqRxAll_RegisteredHandlesPollEmpty_ReturnsFalse)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_list_ = {TEST_UMQ_HANDLE, TEST_UMQ_HANDLE_2};
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(returnValue(0));

    EXPECT_FALSE(ops.PollMainUmqRxAll());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_UnknownType_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_INVALID);

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_TxTimerLegacyMode_PollsAndReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TX_CQE_TIMER);

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_TxWakeWithoutFdLegacyMode_PollsAndReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TX_WAKE);
    RunnerEventData eventData{};
    eventData.u64 = event.data.u64;
    eventData.event_data.data = 0;
    event.data.u64 = eventData.u64;

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_SubUmqPollFails_MapsSavedErrno)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    epoll_event event = MakeSubUmqEvent(socket.Get());
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(UMQ_FAIL));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));
    errno = EIO;

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    EXPECT_EQ(errno, EIO);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_SubUmqPollEmpty_RearmsAndReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    epoll_event event = MakeSubUmqEvent(socket.Get());
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(exactly(0)).will(returnValue(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_SubUmqPollEmptyAndRearmFails_ReturnsError)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    epoll_event event = MakeSubUmqEvent(socket.Get());
    MOCKER_CPP(::umq_poll).expects(once()).will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).expects(once()).will(returnValue(UMQ_FAIL));
    errno = EIO;

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    EXPECT_EQ(errno, EIO);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_SubUmqSuccessBuffer_ReturnsOkWithoutFree)
{
    UmqShareJfrEpollRunnerOps ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    umq_buf_t buffer{};
    g_pollBuf = &buffer;
    epoll_event event = MakeSubUmqEvent(socket.Get());
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(exactly(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, HandleSubUmqPollBuffers_FcUpdate_WakesTxAndFreesBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetTxOps(), nullptr);
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_UPDATE;
    buffer.qbuf_next = &buffer;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.HandleSubUmqPollBuffers(socket.Get(), buffers, 1);

    EXPECT_EQ(buffer.qbuf_next, nullptr);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, HandleSubUmqPollBuffers_ErrorStatus_HandlesErrorAndFreesBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    umq_buf_t buffer{};
    buffer.status = UMQ_BUF_UNSUPPORTED_OPCODE_ERR;
    buffer.qbuf_next = &buffer;
    umq_buf_t *buffers[] = {&buffer};
    LibcApi::shutdown_ptr = ShutdownSuccess;
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.HandleSubUmqPollBuffers(socket.Get(), buffers, 1);

    EXPECT_EQ(g_shutdownFd, TEST_EVENT_FD);
    EXPECT_EQ(buffer.qbuf_next, nullptr);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_FcErrorBuffer_FreesWithoutSocketLookup)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR;
    buffer.qbuf_next = &buffer;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_EQ(buffer.qbuf_next, nullptr);
    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, HandleSubUmqPollBuffers_SuccessBuffer_DoesNotFree)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocket socket(TEST_EVENT_FD);
    umq_buf_t buffer{};
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(exactly(0));

    ops.HandleSubUmqPollBuffers(&socket, buffers, 1);

    EXPECT_EQ(buffer.status, 0U);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_FcUpdateForMissingSocket_FreesBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_UPDATE;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_FcUpdateForRegisteredSocket_NotifiesAndFreesBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_UPDATE;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(socket->Ready());
    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_DataForMissingSocket_SkipsBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    umq_buf_t buffer{};
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(exactly(0));

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_DataForClosedSocket_FreesBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    socket->State(SOCK_STAT_CLOSE);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    buffer.qbuf_next = &buffer;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_EQ(buffer.qbuf_next, nullptr);
    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_BigControlForClosedSocket_FreesBeforeDispatch)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    socket->State(SOCK_STAT_CLOSE);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    properties->imm_data = proto::mark_big_ctrl(0);
    buffer.qbuf_next = &buffer;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_EQ(buffer.qbuf_next, nullptr);
    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_ReadDoneControl_ConsumesWithoutRxQueue)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    UbsCtrlHdr control{};
    control.type = UBS_READ_DONE;
    control.total_len = sizeof(control);
    umq_buf_t buffer{};
    buffer.buf_data = reinterpret_cast<char *>(&control);
    buffer.data_size = sizeof(control);
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    properties->imm_data = proto::mark_big_ctrl(0);
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_ReadDoneControlWithTxOps_QuickPollsOnce)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetTxOps(), nullptr);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    UbsCtrlHdr control{};
    control.type = UBS_READ_DONE;
    control.total_len = sizeof(control);
    umq_buf_t buffer{};
    buffer.buf_data = reinterpret_cast<char *>(&control);
    buffer.data_size = sizeof(control);
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    properties->imm_data = proto::mark_big_ctrl(0);
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());
    MOCKER_CPP(&DataTxOps::QuickPollTx).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(sockets.empty());
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, SiftSocketEvents_OpenSocketWithoutRxQueue_SkipsAfterAddFailure)
{
    UmqShareJfrEpollRunnerOps ops;
    FlashDynamicBitSet socketFds(ArraySet<Socket>::GetInstance().Capacity());
    std::vector<SocketPtr> sockets;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t *buffers[] = {&buffer};
    MOCKER_CPP(::umq_buf_free).expects(once());

    ops.SiftSocketEventsWithUmqBuffers(buffers, 1, socketFds, sockets);

    EXPECT_TRUE(sockets.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_FcOnlyBatch_DoesNotCountOrRefill)
{
    UmqShareJfrEpollRunnerOps ops;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR_FATAL;
    g_pollBuf = &buffer;
    int totalPolled = 3;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());
    MOCKER_CPP(::umq_buf_alloc).expects(exactly(0));
    MOCKER_CPP(::umq_post).expects(exactly(0)).will(returnValue(0));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(totalPolled, 3);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_DataForMissingSocket_CountsButDoesNotPostRefill)
{
    UmqShareJfrEpollRunnerOps ops;
    umq_buf_t buffer{};
    buffer.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &buffer;
    int totalPolled = 3;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));
    MOCKER_CPP(::umq_post).expects(exactly(0)).will(returnValue(0));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(totalPolled, 4);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_RefillPostSucceeds_ReturnsTrue)
{
    UmqShareJfrEpollRunnerOps ops;
    umq_buf_t input{};
    input.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(input.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t refill{};
    g_pollBuf = &input;
    int totalPolled = 0;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(&refill));
    MOCKER_CPP(::umq_post).expects(once()).will(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(::umq_buf_free).expects(exactly(0));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(totalPolled, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_RefillPostFails_MapsErrnoAndFreesBadBuffer)
{
    UmqShareJfrEpollRunnerOps ops;
    umq_buf_t input{};
    input.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(input.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    umq_buf_t refill{};
    g_pollBuf = &input;
    g_badBuf = &refill;
    int totalPolled = 0;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(&refill));
    MOCKER_CPP(::umq_post).expects(once()).will(invoke(PostFailureWithBadBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());
    errno = EIO;

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE, &totalPolled));
    EXPECT_EQ(totalPolled, 1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_RegisteredSocketWithoutEpoll_QueuesWithoutNotification)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    socket->rxQueue.reset(new UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t input{};
    input.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(input.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &input;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE));
    umq_buf_t *dequeued[1] = {nullptr};
    EXPECT_EQ(socket->GetAndPopQbuf(dequeued, 1), 1);
    EXPECT_EQ(dequeued[0], &input);
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_RegisteredSocketWithEpoll_QueuesAndNotifiesOnce)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    socket->rxQueue.reset(new UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    AsyncEventPoll eventPoll(TEST_EPOLL_FD);
    epoll_data_t eventData{};
    eventData.fd = TEST_EVENT_FD;
    socket->SetAddedEpollFd(&eventPoll, eventData);
    umq_buf_t input{};
    input.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(input.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &input;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));
    MOCKER_CPP(::eventfd_write).expects(once()).will(returnValue(0));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE));
    umq_buf_t *dequeued[1] = {nullptr};
    EXPECT_EQ(socket->GetAndPopQbuf(dequeued, 1), 1);
    EXPECT_EQ(dequeued[0], &input);
    socket->SetAddedEpollFd(nullptr, {});
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RxPollQuantum_DirectDispatch_QueuesAndFlushesOnce)
{
    UmqShareJfrEpollRunnerOps ops;
    UmqSocketPtr socket = MakeDataPlaneSocket();
    ASSERT_NE(socket->GetRxOps(), nullptr);
    socket->rxQueue.reset(new UmqBufferReceiveQueue());
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    AsyncEventPoll eventPoll(TEST_EPOLL_FD);
    epoll_data_t eventData{};
    eventData.fd = TEST_EVENT_FD;
    socket->SetAddedEpollFd(&eventPoll, eventData);
    u_external_poller_ops_t pollerOps{};
    pollerOps.dispatch_event = DispatchEvent;
    pollerOps.dispatch_flush = DispatchFlush;
    GlobalSetting::UBS_POLLER_OPS = &pollerOps;
    umq_buf_t input{};
    input.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(input.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &input;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));
    MOCKER_CPP(::eventfd_write).expects(exactly(0)).will(returnValue(0));

    EXPECT_TRUE(ops.RxPollQuantum(TEST_UMQ_HANDLE));
    EXPECT_EQ(g_dispatchCount, 1);
    EXPECT_EQ(g_dispatchFlushCount, 1);
    umq_buf_t *dequeued[1] = {nullptr};
    EXPECT_EQ(socket->GetAndPopQbuf(dequeued, 1), 1);
    socket->SetAddedEpollFd(nullptr, {});
    socket->umq_handle_ = UMQ_INVALID_HANDLE;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessShareJfrEvent_FcOnlyWithoutRearm_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR;
    g_pollBuf = &buffer;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR_RETRY);
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessShareJfrEvent_RearmWithoutEvents_ReturnsOk)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR_FATAL;
    g_pollBuf = &buffer;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(2)).will(invoke(PollOneBufferThenEmpty));
    MOCKER_CPP(::umq_buf_free).expects(once());
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessShareJfrEvent_TraceAndBatchThreshold_UpdatesTrace)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD = 1;
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = true;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    socket->State(SOCK_STAT_CLOSE);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    buffer.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &buffer;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR_RETRY);
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessShareJfrEvent_RearmFailsAfterSocketRemoval_SkipsTraceUpdate)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = true;
    UmqSocketPtr socket = MakeRef<UmqSocket>(TEST_EVENT_FD);
    socket->State(SOCK_STAT_CLOSE);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_EVENT_FD, socket.Get());
    umq_buf_t buffer{};
    buffer.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &buffer;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());
    MOCKER_CPP(::umq_buf_alloc).expects(once()).will(returnValue(static_cast<umq_buf_t *>(nullptr)));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(invoke(GetCqEventFailureAndRemoveSocket));

    EXPECT_EQ(ops.ProcessOneEvent(event), -1);
    EXPECT_EQ(errno, EIO);
    GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, ProcessOneEvent_UnifiedPostRearmData_LogsBatchThreshold)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_[TEST_EVENT_FD] = TEST_UMQ_HANDLE;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    GlobalSetting::UBS_RX_BATCH_PRINT_THRESHOLD = 1;
    umq_buf_t buffer{};
    buffer.data_size = 64;
    auto *properties = reinterpret_cast<umq_buf_pro_t *>(buffer.qbuf_ext);
    properties->umq_ctx = TEST_EVENT_FD;
    g_pollBuf = &buffer;
    epoll_event event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR);
    MOCKER_CPP(::umq_poll).expects(exactly(3)).will(invoke(PollOneBufferTwiceThenEmpty));
    MOCKER_CPP(::umq_buf_alloc).expects(exactly(2)).will(returnValue(static_cast<umq_buf_t *>(nullptr)));
    MOCKER_CPP(::umq_get_cq_event).expects(once()).will(returnValue(0));

    EXPECT_EQ(ops.ProcessOneEvent(event), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RunUnifiedActiveLoop_RxAlwaysProgresses_StopsAtActiveRoundLimit)
{
    UmqShareJfrEpollRunnerOps ops;
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR;
    g_pollBuf = &buffer;
    MOCKER_CPP(::umq_poll).expects(exactly(64)).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(exactly(64));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RunUnifiedActiveLoop_TxAlwaysProgresses_StopsAtActiveRoundLimit)
{
    UmqShareJfrEpollRunnerOps ops;
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    MOCKER_CPP(&TxCqePoller::TxSweepOnce).expects(exactly(64)).will(returnValue(true));

    ops.RunUnifiedActiveLoop();

    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RunUnifiedActiveLoop_InflightBackoff_RechecksBeforeDeepIdle)
{
    UmqShareJfrEpollRunnerOps ops;
    auto &poller = TxCqePoller::Instance();
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 20;
    poller.stopped_.store(true, std::memory_order_release);
    poller.any_inflight_.store(true, std::memory_order_release);
    MOCKER_CPP(::umq_poll).expects(exactly(4)).will(returnValue(0));

    ops.RunUnifiedActiveLoop(TEST_UMQ_HANDLE);

    EXPECT_FALSE(poller.IsSleeping());
    poller.stopped_.store(false, std::memory_order_release);
    poller.any_inflight_.store(false, std::memory_order_release);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, RunUnifiedActiveLoop_TxOnlyInflightBackoff_RechecksBeforeDeepIdle)
{
    UmqShareJfrEpollRunnerOps ops;
    auto &poller = TxCqePoller::Instance();
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 20;
    poller.stopped_.store(true, std::memory_order_release);
    poller.any_inflight_.store(true, std::memory_order_release);

    ops.RunUnifiedActiveLoop();

    EXPECT_FALSE(poller.IsSleeping());
    poller.stopped_.store(false, std::memory_order_release);
    poller.any_inflight_.store(false, std::memory_order_release);
    GlobalMockObject::verify();
}

// Tx-only BACKOFF must NOT call PollMainUmqRxAll during backoff iterations.
// PollMainUmqRxAll IS called once at DEEP_IDLE entry (straggler catch).
// With one umq registered and umq_poll returning 0:
//   Before fix: 2 umq_poll calls (1 BACKOFF + 1 DEEP_IDLE)
//   After fix:  1 umq_poll call (DEEP_IDLE only)
TEST_F(UmqShareJfrEpollRunnerOpsTest, RunUnifiedActiveLoop_TxOnlyBackoff_NoPollMainUmqRxAllDuringBackoff)
{
    UmqShareJfrEpollRunnerOps ops;
    auto &poller = TxCqePoller::Instance();
    GlobalSetting::UBS_SHARE_JFR_LOOP_POLL_ENABLED = true;
    GlobalSetting::UBS_TX_POLLER_BACKOFF_MAX_US = 20;
    ops.jfr_main_umq_list_ = {TEST_UMQ_HANDLE};
    poller.stopped_.store(true, std::memory_order_release);
    poller.any_inflight_.store(true, std::memory_order_release);

    MOCKER_CPP(::umq_poll).expects(exactly(1)).will(returnValue(0));

    ops.RunUnifiedActiveLoop();

    poller.stopped_.store(false, std::memory_order_release);
    poller.any_inflight_.store(false, std::memory_order_release);
    GlobalMockObject::verify();
}

TEST_F(UmqShareJfrEpollRunnerOpsTest, PollMainUmqRxAll_RegisteredHandleWithBuffer_ReturnsTrue)
{
    UmqShareJfrEpollRunnerOps ops;
    ops.jfr_main_umq_list_ = {TEST_UMQ_HANDLE};
    umq_buf_t buffer{};
    buffer.status = UMQ_FAKE_BUF_FC_ERR;
    g_pollBuf = &buffer;
    MOCKER_CPP(::umq_poll).expects(once()).will(invoke(PollOneBuffer));
    MOCKER_CPP(::umq_buf_free).expects(once());

    EXPECT_TRUE(ops.PollMainUmqRxAll());
    GlobalMockObject::verify();
}
