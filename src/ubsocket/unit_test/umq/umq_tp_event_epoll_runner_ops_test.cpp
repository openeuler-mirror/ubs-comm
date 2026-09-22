/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "umq_tp_event_epoll_runner_ops.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "ubsocket_errno.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_lock.h"
#include "umq_tp_wait_queue.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
static const int TEST_EVENT_FD = 42;
static const int TEST_EPOLL_FD = 7;
} // namespace

class UmqTpEventEpollRunnerOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        errno = 0;
    }

    /* Build an epoll_event whose data.u64 encodes (type, data) the same way
     * production registers it: high 4 bits type, low 60 bits the fd. */
    static struct epoll_event MakeRunnerEvent(uint64_t type, uint64_t data)
    {
        RunnerEventData runner_data{};
        runner_data.event_data.type = type;
        runner_data.event_data.data = data;
        struct epoll_event event{};
        event.events = EPOLLIN;
        event.data.u64 = runner_data.u64;
        return event;
    }
};

// ==================== ProcessOneEvent ====================

// TP_EVENT wakeup: eventfd_read succeeds, the wait queue must be poked
// exactly once and the runner keeps going (UBS_OK).
TEST_F(UmqTpEventEpollRunnerOpsTest, ProcessOneEvent_TpEventReadSuccess_ReturnsOkAndWakesUpOne)
{
    UmqTpEventEpollRunnerOps ops;

    MOCKER_CPP(::eventfd_read).expects(exactly(1)).will(returnValue(0));
    MOCKER_CPP(&UmqTpWaitQueue::TryWakeupOne).expects(exactly(1)).will(returnValue(static_cast<int>(UBS_OK)));

    auto event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TP_EVENT, TEST_EVENT_FD);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
}

// eventfd_read failure is non-fatal by design: the runner only logs it and
// still wakes up one waiter, returning UBS_OK.
TEST_F(UmqTpEventEpollRunnerOpsTest, ProcessOneEvent_TpEventReadFail_StillReturnsOkAndWakesUpOne)
{
    UmqTpEventEpollRunnerOps ops;

    MOCKER_CPP(::eventfd_read).expects(exactly(1)).will(returnValue(-1));
    MOCKER_CPP(&UmqTpWaitQueue::TryWakeupOne).expects(exactly(1)).will(returnValue(static_cast<int>(UBS_OK)));

    auto event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TP_EVENT, TEST_EVENT_FD);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_OK);
}

// Any event type this runner does not own must be rejected with UBS_ERROR
// and must not touch the wait queue.
TEST_F(UmqTpEventEpollRunnerOpsTest, ProcessOneEvent_UnknownEventType_ReturnsError)
{
    UmqTpEventEpollRunnerOps ops;

    MOCKER_CPP(::eventfd_read).expects(exactly(0)).will(returnValue(0));
    MOCKER_CPP(&UmqTpWaitQueue::TryWakeupOne).expects(exactly(0)).will(returnValue(static_cast<int>(UBS_OK)));

    auto event = MakeRunnerEvent(RUNNER_EVENT_TYPE_SHARE_JFR, TEST_EVENT_FD);
    EXPECT_EQ(ops.ProcessOneEvent(event), UBS_ERROR);
}

// ==================== AddEventToRunner ====================

// Happy path: epoll_ctl(ADD) succeeds and the event is accepted.
TEST_F(UmqTpEventEpollRunnerOpsTest, AddEventToRunner_EpollCtlAddSuccess_ReturnsOk)
{
    UmqTpEventEpollRunnerOps ops;

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(0));

    auto event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TP_EVENT, TEST_EVENT_FD);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, nullptr), UBS_OK);
}

// epoll_ctl(ADD) failure must propagate as UBS_ERROR.
TEST_F(UmqTpEventEpollRunnerOpsTest, AddEventToRunner_EpollCtlAddFail_ReturnsError)
{
    UmqTpEventEpollRunnerOps ops;

    MOCKER_CPP(::epoll_ctl).expects(exactly(1)).will(returnValue(-1));

    auto event = MakeRunnerEvent(RUNNER_EVENT_TYPE_TP_EVENT, TEST_EVENT_FD);
    EXPECT_EQ(ops.AddEventToRunner(TEST_EPOLL_FD, TEST_EVENT_FD, &event, nullptr), UBS_ERROR);
}
