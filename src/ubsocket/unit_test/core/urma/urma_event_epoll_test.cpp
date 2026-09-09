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
#include <memory>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_event_epoll.h"
#include "core/urma/urma_poller.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_wrapper.h"
#include "under_api/urma/dl_urma_api.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_EPOLL_FD = 42;
static const int TEST_SOCK_FD = 100;
static const int TEST_EVENT_FD = 200;
static const int TEST_JFCE_FD = 300;
static const int TEST_SOCK_FD_2 = 101;
static const int TEST_EVENT_FD_2 = 201;

static int g_epollCtlOp = -1;
static int g_epollCtlFd = -1;
static int g_epollCtlAddCount = 0;
static int g_epollCtlReturn = 0;

static int MockEpollCtl(int, int op, int fd, struct epoll_event *)
{
    if (op == EPOLL_CTL_ADD) {
        g_epollCtlAddCount++;
    }
    g_epollCtlOp = op;
    g_epollCtlFd = fd;
    return g_epollCtlReturn;
}

struct epoll_event *g_lastEvents = nullptr;
int g_lastMaxevents = 0;
int g_epollWaitReturn = 0;
int g_epollWaitCalls = 0;

static int MockEpollWait(int, struct epoll_event *events, int maxevents, int)
{
    g_epollWaitCalls++;
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

/* ---- DISPATCHER mode driver mocks ---- */
static int g_cqeHandlerCalls = 0;
static int g_cqeHandlerLastCnt = 0;
static int g_ackJfcCalls = 0;
static int g_rearmJfcCalls = 0;
static int g_pollJfcCalls = 0;
static int g_pollJfcSeqPos = 0;
static int g_pollJfcSeq[8] = {0};
static int g_pollJfcSeqLen = 0;

static ock::ubs::Result MockCqeHandler(uint32_t, int crCount, urma_cr_t *)
{
    g_cqeHandlerCalls++;
    g_cqeHandlerLastCnt = crCount;
    return UBS_OK;
}

static int MockPollJfc(urma_jfc_t *, int, urma_cr_t *)
{
    g_pollJfcCalls++;
    int ret = 0;
    if (g_pollJfcSeqPos < g_pollJfcSeqLen) {
        ret = g_pollJfcSeq[g_pollJfcSeqPos++];
    }
    return ret;
}

static urma_status_t MockRearmJfc(urma_jfc_t *, bool)
{
    g_rearmJfcCalls++;
    return URMA_SUCCESS;
}

static void MockAckJfc(urma_jfc_t *[], uint32_t[], uint32_t)
{
    g_ackJfcCalls++;
}

static urma_status_t MockDeleteJfc(urma_jfc_t *)
{
    return URMA_SUCCESS;
}

static urma_status_t MockDeleteJfce(urma_jfce_t *)
{
    return URMA_SUCCESS;
}

static urma_status_t MockDeleteContext(urma_context_t *)
{
    return URMA_SUCCESS;
}

static void ResetMockCounters()
{
    g_epollCtlOp = -1;
    g_epollCtlFd = -1;
    g_epollCtlAddCount = 0;
    g_epollCtlReturn = 0;
    g_lastEvents = nullptr;
    g_lastMaxevents = 0;
    g_epollWaitReturn = 0;
    g_epollWaitCalls = 0;
    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;
    g_eventfdReadFd = -1;
    g_cqeHandlerCalls = 0;
    g_cqeHandlerLastCnt = 0;
    g_ackJfcCalls = 0;
    g_rearmJfcCalls = 0;
    g_pollJfcCalls = 0;
    g_pollJfcSeqPos = 0;
    g_pollJfcSeqLen = 0;
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
        sock->is_bind_remote_ = true;
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

TEST_F(UrmaEventPollTest, EpollWait_UbSocketInEvent_SockNotFoundInArraySet_Dropped)
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
    EXPECT_EQ(0, ret); /* socket 已从 ArraySet 移除的事件不再返回给应用 */
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
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLOUT);
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

namespace {
struct DispatcherPollerEnv {
    std::unique_ptr<urma_context_t> rawContext;
    std::unique_ptr<urma_jfce_t> rawJfce;
    std::unique_ptr<urma_jfc_t> rawJfc;
    UrmaDevicePtr device{nullptr};
    UrmaContextPtr context{nullptr};
    UrmaJfcPtr jfc{nullptr};
    UrmaPollerPtr poller{nullptr};
};

DispatcherPollerEnv MakeDispatcherPollerEnv()
{
    DispatcherPollerEnv env;
    env.rawContext = std::make_unique<urma_context_t>();
    env.rawJfce = std::make_unique<urma_jfce_t>();
    env.rawJfc = std::make_unique<urma_jfc_t>();
    env.rawJfce->fd = TEST_JFCE_FD;
    env.device = MakeRef<UrmaDevice>("ut_dev", "", urma_device_attr_t{});
    env.context = MakeRef<UrmaContext>(env.rawContext.get(), env.device, urma_eid_info_t{});
    env.jfc = MakeRef<UrmaJfc>(EVENT_POLLING, env.rawJfc.get(), env.rawJfce.get(), env.context);
    env.poller = MakeRef<UrmaPoller>(0, MockCqeHandler, env.jfc);
    return env;
}
} // namespace

class UrmaEventPollDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();
        savedMode_ = UrmaSetting::UB_EPOLL_HANDLE_MODE;
        UrmaSetting::UB_EPOLL_HANDLE_MODE = DISPATCHER_THREAD_POOL;
        ResetMockCounters();
        g_epollCtlReturn = 0;
        g_epollWaitReturn = 0;
        ClearPollerSet();
        SetUpMocks();
        RegisterDriverMocks();
    }

    void TearDown() override
    {
        ClearPollerSet();
        UrmaSetting::UB_EPOLL_HANDLE_MODE = savedMode_;
        UnregisterDriverMocks();
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
    }

    void RegisterDriverMocks()
    {
        UrmaApi::urma_poll_jfc_ptr = MockPollJfc;
        UrmaApi::urma_rearm_jfc_ptr = MockRearmJfc;
        UrmaApi::urma_ack_jfc_ptr = MockAckJfc;
        UrmaApi::urma_delete_jfc_ptr = MockDeleteJfc;
        UrmaApi::urma_delete_jfce_ptr = MockDeleteJfce;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;
    }

    void UnregisterDriverMocks()
    {
        UrmaApi::urma_poll_jfc_ptr = nullptr;
        UrmaApi::urma_rearm_jfc_ptr = nullptr;
        UrmaApi::urma_ack_jfc_ptr = nullptr;
        UrmaApi::urma_delete_jfc_ptr = nullptr;
        UrmaApi::urma_delete_jfce_ptr = nullptr;
        UrmaApi::urma_delete_context_ptr = nullptr;
    }

    void ClearPollerSet()
    {
        auto &set = UrmaPollerSet::Instance();
        set.pollers_.clear();
        set.inited_ = false;
    }

    void RegisterPoller(DispatcherPollerEnv &env)
    {
        UrmaPollerSet::Instance().pollers_.push_back(env.poller);
    }

    void SetUpMocks()
    {
        MOCKER(epoll_ctl).stubs().will(invoke(MockEpollCtl));
        MOCKER(epoll_wait).stubs().will(invoke(MockEpollWait));
        MOCKER(eventfd_write).stubs().will(invoke(MockEventfdWrite));
        MOCKER(eventfd_read).stubs().will(invoke(MockEventfdRead));
    }

    void CreateAndRegisterUrmaSocket(int fd = TEST_SOCK_FD, int eventFd = TEST_EVENT_FD)
    {
        auto sock = MakeRef<UrmaSocket>(fd);
        sock->raw_socket_ = fd;
        sock->event_fd_ = eventFd;
        sock->writable_ready_.store(true, std::memory_order_release);
        sock->socket_readable_ready_.store(false, std::memory_order_release);
        sock->is_bind_remote_ = true;
        ArraySet<Socket>::GetInstance().OverrideItem(fd, sock.Get());
    }

    EpollHandleMode savedMode_;
};

TEST_F(UrmaEventPollDispatcherTest, Init_NoPoller_ReturnsError)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    EXPECT_EQ(UBS_ERROR, poll.Init());
    EXPECT_EQ(0u, poll.jfce_handlers_.size());
}

TEST_F(UrmaEventPollDispatcherTest, Init_RegistersJfceFd_DestructorRemoves)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);

    g_epollCtlOp = -1;
    g_epollCtlFd = -1;
    {
        UrmaEventPoll poll(TEST_EPOLL_FD);
        ASSERT_EQ(UBS_OK, poll.Init());
        ASSERT_EQ(1u, poll.jfce_handlers_.size());
        EXPECT_EQ(TEST_JFCE_FD, poll.jfce_handlers_.begin()->first);
        EXPECT_EQ(EPOLL_CTL_ADD, g_epollCtlOp);
        EXPECT_EQ(TEST_JFCE_FD, g_epollCtlFd);
        EXPECT_EQ(4096u, poll.readable_events_.Capacity());
        EXPECT_EQ(0u, poll.readable_events_.Size());
        EXPECT_EQ(0u, poll.readable_events_.PendingCount());

        EXPECT_EQ(UBS_OK, poll.Init()); /* Init is idempotent: no second JFCE registration */
        EXPECT_EQ(1, g_epollCtlAddCount);
    }
    EXPECT_EQ(EPOLL_CTL_DEL, g_epollCtlOp);
    EXPECT_EQ(TEST_JFCE_FD, g_epollCtlFd);
}

TEST_F(UrmaEventPollDispatcherTest, NotifySocketReadable_OffWaitThread_FallsBackToEventfd)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetEvents(EPOLLIN);

    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;
    int ret = urma_sock->NotifySocketReadable();
    EXPECT_EQ(0, ret);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd);
    EXPECT_EQ(1, g_eventfdWriteCount);
}

TEST_F(UrmaEventPollDispatcherTest, NotifySocketReadable_NotWatchingIn_NoEventfd)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetEvents(0);

    g_eventfdWriteFd = -1;
    int ret = urma_sock->NotifySocketReadable();
    EXPECT_EQ(0, ret);
    EXPECT_EQ(-1, g_eventfdWriteFd);
    EXPECT_TRUE(urma_sock->socket_readable_ready_.load(std::memory_order_acquire));
}

TEST_F(UrmaEventPollDispatcherTest, RecordReadable_ThenEpollWait_DrainsInlineEvent)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    CreateAndRegisterUrmaSocket();

    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());
    struct epoll_event appEvt {};
    appEvt.events = EPOLLIN;
    appEvt.data.u64 = static_cast<uint64_t>(0xABCD);
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &appEvt));

    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLIN);

    ASSERT_TRUE(poll.RecordReadableSocket(urma_sock.Get()));
    EXPECT_EQ(1u, poll.readable_events_.PendingCount());
    EXPECT_EQ(1u, poll.readable_events_.Size());
    /* 同一次未返回前的重复可读通知应被去重 */
    ASSERT_TRUE(poll.RecordReadableSocket(urma_sock.Get()));
    EXPECT_EQ(1u, poll.readable_events_.PendingCount());
    EXPECT_EQ(1u, poll.readable_events_.Size());

    struct epoll_event out[2]{};
    g_epollWaitReturn = 0;
    int ret = poll.EpollWait(out, 2, 10);
    EXPECT_EQ(1, ret);
    EXPECT_EQ(static_cast<uint64_t>(0xABCD), out[0].data.u64);
    EXPECT_NE(0u, out[0].events & EPOLLIN);
    EXPECT_EQ(0, g_epollWaitCalls);
    EXPECT_EQ(0u, poll.readable_events_.PendingCount());
    EXPECT_EQ(0u, poll.readable_events_.Size());
}

TEST_F(UrmaEventPollDispatcherTest, RecordReadable_SocketNotRegisteredOnEpoll_False)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    CreateAndRegisterUrmaSocket();

    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLIN);

    EXPECT_FALSE(poll.RecordReadableSocket(urma_sock.Get()));
    EXPECT_EQ(0u, poll.readable_events_.PendingCount());
}

TEST_F(UrmaEventPollDispatcherTest, RecordReadable_StaleSocketDroppedOnDrain)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    CreateAndRegisterUrmaSocket();

    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());
    struct epoll_event appEvt {};
    appEvt.events = EPOLLIN;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &appEvt));

    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLIN);
    ASSERT_TRUE(poll.RecordReadableSocket(urma_sock.Get()));

    /* 返回前 socket 被注销: 环形中该事件应被丢弃而非返回给应用 */
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr));

    struct epoll_event out[2]{};
    g_epollWaitReturn = 0;
    int ret = poll.EpollWait(out, 2, 10);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0u, poll.readable_events_.PendingCount());
}

TEST_F(UrmaEventPollDispatcherTest, RecordReadable_TwoSockets_LeftoverDrainedNextCall)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    CreateAndRegisterUrmaSocket(TEST_SOCK_FD, TEST_EVENT_FD);
    CreateAndRegisterUrmaSocket(TEST_SOCK_FD_2, TEST_EVENT_FD_2);

    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());
    struct epoll_event appEvt1 {};
    appEvt1.events = EPOLLIN;
    appEvt1.data.u64 = 1;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &appEvt1));
    struct epoll_event appEvt2 {};
    appEvt2.events = EPOLLIN;
    appEvt2.data.u64 = 2;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD_2, &appEvt2));

    auto sock1 = RefConvert<Socket, UrmaSocket>(ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD));
    auto sock2 = RefConvert<Socket, UrmaSocket>(ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD_2));
    sock1->SetReachEpollEvents(EPOLLIN);
    sock2->SetReachEpollEvents(EPOLLIN);
    ASSERT_TRUE(poll.RecordReadableSocket(sock1.Get()));
    ASSERT_TRUE(poll.RecordReadableSocket(sock2.Get()));

    struct epoll_event out1[1]{};
    g_epollWaitReturn = 0;
    int ret = poll.EpollWait(out1, 1, 10);
    EXPECT_EQ(1, ret);
    EXPECT_EQ(0, g_epollWaitCalls);

    /* 第二次 wait 仍不阻塞, 直接把剩余事件带走 */
    struct epoll_event out2[1]{};
    ret = poll.EpollWait(out2, 1, 10);
    EXPECT_EQ(1, ret);
    EXPECT_EQ(2u, out2[0].data.u64);
    EXPECT_EQ(0, g_epollWaitCalls);

    /* 环形清空后再 wait 才真正阻塞在 epoll_wait */
    struct epoll_event out3[1]{};
    ret = poll.EpollWait(out3, 1, 10);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(1, g_epollWaitCalls);
}

TEST_F(UrmaEventPollDispatcherTest, EpollWait_JfceEvent_NotReturned_InlinePollInvoked)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());

    struct epoll_event kernelEvt {};
    EpollEvent jfceEvent(EPOLL_EVENT_JFCE_FD, TEST_JFCE_FD, kernelEvt);
    jfceEvent.event.data.ptr = env.poller.Get();
    kernelEvt.data.ptr = &jfceEvent;

    g_epollWaitReturn = 1;
    g_pollJfcSeq[0] = 1;
    g_pollJfcSeq[1] = 0;
    g_pollJfcSeqLen = 2;
    struct epoll_event evt {};
    evt.data.ptr = &jfceEvent;
    int ret = poll.EpollWait(&evt, 1, 100);

    EXPECT_EQ(0, ret); /* JFCE 事件本身不外发给应用 */
    EXPECT_GE(g_cqeHandlerCalls, 1);
    EXPECT_EQ(1, g_ackJfcCalls);
    EXPECT_GE(g_rearmJfcCalls, 1);
}

TEST_F(UrmaEventPollDispatcherTest, EpollWait_JfceEvent_NullPoller_Ignored)
{
    auto env = MakeDispatcherPollerEnv();
    RegisterPoller(env);
    UrmaEventPoll poll(TEST_EPOLL_FD);
    ASSERT_EQ(UBS_OK, poll.Init());

    struct epoll_event kernelEvt {};
    EpollEvent jfceEvent(EPOLL_EVENT_JFCE_FD, TEST_JFCE_FD, kernelEvt);
    jfceEvent.event.data.ptr = nullptr; /* 非法载荷: 不属于任何 poller */
    kernelEvt.data.ptr = &jfceEvent;

    g_epollWaitReturn = 1;
    struct epoll_event evt {};
    evt.data.ptr = &jfceEvent;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(0, g_cqeHandlerCalls);
}

TEST_F(UrmaEventPollDispatcherTest, Poller_StartStop_SkipIndependentThread)
{
    auto poller = MakeRef<UrmaPoller>(7, MockCqeHandler, UrmaJfcPtr{});
    ASSERT_NE(nullptr, poller.Get());
    EXPECT_EQ(UBS_OK, poller->Start());
    poller->Stop();
}

TEST_F(UrmaEventPollDispatcherTest, Poller_PollAndDispatch_AckThenDrainUntilZero)
{
    auto env = MakeDispatcherPollerEnv();

    g_pollJfcSeq[0] = 2;
    g_pollJfcSeq[1] = 0;
    g_pollJfcSeqLen = 2;
    ock::ubs::Result res = env.poller->PollAndDispatch(true);
    EXPECT_EQ(UBS_OK, res);
    EXPECT_EQ(1, g_ackJfcCalls);
    EXPECT_EQ(1, g_cqeHandlerCalls);
    EXPECT_EQ(2, g_cqeHandlerLastCnt);
    EXPECT_GE(g_rearmJfcCalls, 1);
}
