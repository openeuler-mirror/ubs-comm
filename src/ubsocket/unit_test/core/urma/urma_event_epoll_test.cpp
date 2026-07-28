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
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <cerrno>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_event_epoll.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_EPOLL_FD = 42;
static const int TEST_SOCK_FD = 100;
static const int TEST_EVENT_FD = 200;

static int g_epollCtlOp = -1;
static int g_epollCtlFd = -1;
static int g_epollCtlReturn = 0;

static int MockEpollCtl(int, int op, int fd, struct epoll_event *)
{
    g_epollCtlOp = op;
    g_epollCtlFd = fd;
    return g_epollCtlReturn;
}

struct epoll_event *g_lastEvents = nullptr;
int g_lastMaxevents = 0;
int g_epollWaitReturn = 0;

static int MockEpollWait(int, struct epoll_event *events, int maxevents, int)
{
    g_lastEvents = events;
    g_lastMaxevents = maxevents;
    return g_epollWaitReturn;
}

static int g_eventfdWriteFd = -1;
static int g_eventfdWriteCount = 0;

static int MockEventfdWrite(int fd, uint64_t)
{
    g_eventfdWriteFd = fd;
    g_eventfdWriteCount++;
    return 0;
}

static int g_eventfdReadFd = -1;
static int MockEventfdRead(int fd, uint64_t *)
{
    g_eventfdReadFd = fd;
    return 0;
}

static void ResetMockCounters()
{
    g_epollCtlOp = -1;
    g_epollCtlFd = -1;
    g_epollCtlReturn = 0;
    g_lastEvents = nullptr;
    g_lastMaxevents = 0;
    g_epollWaitReturn = 0;
    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;
    g_eventfdReadFd = -1;
}
} // namespace

class UrmaEventPollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();
        ResetMockCounters();
        g_epollCtlReturn = 0;
        g_epollWaitReturn = 0;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
    }

    void SetUpMocks()
    {
        MOCKER(epoll_ctl).stubs().will(invoke(MockEpollCtl));
        MOCKER(epoll_wait).stubs().will(invoke(MockEpollWait));
        MOCKER(eventfd_write).stubs().will(invoke(MockEventfdWrite));
        MOCKER(eventfd_read).stubs().will(invoke(MockEventfdRead));
    }

    void CreateAndRegisterUrmaSocket()
    {
        auto sock = MakeRef<UrmaSocket>(TEST_SOCK_FD);
        sock->raw_socket_ = TEST_SOCK_FD;
        sock->event_fd_ = TEST_EVENT_FD;
        sock->writable_ready_.store(true, std::memory_order_release);
        sock->socket_readable_ready_.store(false, std::memory_order_release);
        ArraySet<Socket>::GetInstance().OverrideItem(TEST_SOCK_FD, sock.Get());
    }
};

TEST_F(UrmaEventPollTest, EpollCtl_UnknownOp_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_DEL + EPOLL_CTL_MOD + EPOLL_CTL_ADD, 0, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_NullEvent_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, 0, nullptr);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_NegativeFd_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, -1, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_FdAlreadyRegistered_SetsErrnoEEXIST)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    int first_ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, first_ret);

    errno = 0;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EEXIST, errno);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_RawSocket_EpollCtlSucceeds)
{
    g_epollCtlReturn = 0;
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(EPOLL_CTL_ADD, g_epollCtlOp);
    EXPECT_EQ(TEST_SOCK_FD, g_epollCtlFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_RawSocket_EpollCtlFails_ReturnsMinusOne)
{
    g_epollCtlReturn = -1;
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlDel_NegativeFd_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    int ret = poll.EpollCtl(EPOLL_CTL_DEL, -1, nullptr);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlDel_RawSocket_RemoveExisting_Success)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();
    g_epollCtlReturn = 0;

    errno = 0;
    int ret = poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr);
    EXPECT_EQ(0, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlDel_RawSocket_EpollCtlDelFails_ReturnsMinusOne)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();
    g_epollCtlReturn = -1;

    int ret = poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr);
    EXPECT_EQ(-1, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlMod_NegativeFd_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, -1, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlMod_NullEvent_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, 0, nullptr);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollCtlMod_RawSocket_NoEventData_SetsErrnoEINVAL)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlMod_RawSocket_EpollCtlModSucceeds)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;

    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    GlobalMockObject::verify();

    SetUpMocks();
    g_epollCtlReturn = 0;
    struct epoll_event evt2 {};
    evt2.events = EPOLLOUT;
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt2);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(EPOLL_CTL_MOD, g_epollCtlOp);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlMod_RawSocket_EpollCtlModFails_ReturnsMinusOne)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;

    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    GlobalMockObject::verify();

    SetUpMocks();
    g_epollCtlReturn = -1;
    struct epoll_event evt2 {};
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt2);
    EXPECT_EQ(-1, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_NullEvents_SetsErrnoEFAULT)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    int ret = poll.EpollWait(nullptr, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EFAULT, errno);
}

TEST_F(UrmaEventPollTest, EpollWait_NegativeMaxevents_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollWait(&evt, -1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollWait_NoEvents_ReturnsZero)
{
    SetUpMocks();
    g_epollWaitReturn = 0;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, g_lastMaxevents);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_RawSocketEvent_PassDataThrough)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN;
    kernel_evt.data.ptr = reinterpret_cast<void *>(static_cast<uintptr_t>(0xDEADBEEF));
    EpollEvent event_data(EPOLL_EVENT_RAW_SOCKET, TEST_SOCK_FD, kernel_evt);

    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_EQ(kernel_evt.data.u64, evt.data.u64);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_UbSocketInEvent_SockNotFoundInArraySet)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN;
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_EQ(static_cast<uint32_t>(0), evt.events);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_UrmaSocket_EpollCtlAddSucceeds)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_UrmaSocket_EpollCtlAddFails_ReturnsMinusOne)
{
    g_epollCtlReturn = -1;
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_UrmaSocket_WithEpoolOut_WritableReadyTrue_Notifies)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLOUT;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_UrmaSocket_WithEpoolIn_ReadableReadyFalse_NoNotify)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    g_eventfdWriteFd = -1;
    struct epoll_event evt {};
    evt.events = EPOLLIN;

    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(-1, g_eventfdWriteFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlAdd_UrmaSocket_WithBothEpoolInOut_BothReady_Notifies)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLOUT;
    int ret = poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlMod_UrmaSocket_EpollCtlModSucceeds)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();
    g_epollCtlReturn = 0;

    struct epoll_event evt2 {};
    evt2.events = EPOLLOUT;
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt2);
    EXPECT_EQ(0, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlMod_UrmaSocket_WithEpoolOut_WritableReadyTrue_Notifies)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();

    g_eventfdWriteFd = -1;
    struct epoll_event evt2 {};
    evt2.events = EPOLLOUT;
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt2);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlDel_UrmaSocket_Success)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();
    g_epollCtlReturn = 0;

    int ret = poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr);
    EXPECT_EQ(0, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtlDel_UrmaSocket_EpollCtlDelFails_ReturnsMinusOne)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;
    poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);

    GlobalMockObject::verify();
    SetUpMocks();
    g_epollCtlReturn = -1;

    int ret = poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr);
    EXPECT_EQ(-1, ret);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_UbSocketInEvent_EpoolOut_WritableReady_ReportsEpoolOut)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLOUT;
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket();
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_NE(static_cast<uint32_t>(0), evt.events & EPOLLOUT);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdReadFd);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Construct_Destruct_NoCrash)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    SUCCEED();
}
