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
#include <atomic>
#include <cerrno>
#include <memory>
#include <mockcpp/mockcpp.hpp>
#include <thread>
#include <vector>

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
#include "ubsocket_sock.h"
#include "under_api/dl_libc_api.h"
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
static void *g_lastAddDataPtr = nullptr; /* ADD 时传给内核的内部条目指针 */
static bool g_epollWaitAutoFill = false; /* 开启后模拟内核回传内部条目 */

static int MockEpollCtl(int, int op, int fd, struct epoll_event *event)
{
    if (op == EPOLL_CTL_ADD) {
        g_epollCtlAddCount++;
    }
    g_epollCtlOp = op;
    g_epollCtlFd = fd;
    if (op == EPOLL_CTL_ADD && event != nullptr) {
        g_lastAddDataPtr = event->data.ptr;
    }
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
    if (g_epollWaitAutoFill && g_lastAddDataPtr != nullptr && maxevents > 0) {
        events[0].data.ptr = g_lastAddDataPtr; /* 模拟内核回传注册时的 data.ptr */
    }
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

/* ---- close() 拦截: ubsocket_close 全链路不触碰真实 fd ---- */
static int g_libcCloseCalls = 0;
static int MockLibcCloseForTest(int)
{
    g_libcCloseCalls++;
    return 0;
}

static int (*g_origClosePtr)(int) = nullptr;
static void RestoreClosePtr()
{
    if (g_origClosePtr != nullptr) {
        LibcApi::close_ptr = g_origClosePtr;
        g_origClosePtr = nullptr;
    }
}

/* ---- DISPATCHER mode driver mocks ---- */
static int g_cqeHandlerCalls = 0;
static int g_cqeHandlerLastCnt = 0;
static int g_ackJfcCalls = 0;
static uint32_t g_ackJfcLastNevents = 0;
static int g_deleteJfcCalls = 0;
static int g_waitJfcCalls = 0;
static int g_waitJfcSeq[4] = {0};
static int g_waitJfcSeqLen = 0;
static urma_jfc_t *g_waitJfcOut = nullptr;
static UrmaPoller *g_pollerToStop = nullptr;
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

static void MockAckJfc(urma_jfc_t *[], uint32_t nevents[], uint32_t cnt)
{
    g_ackJfcCalls++;
    if (cnt > 0) {
        g_ackJfcLastNevents = nevents[0];
    }
}

static urma_status_t MockDeleteJfc(urma_jfc_t *)
{
    g_deleteJfcCalls++;
    return URMA_SUCCESS;
}

/* wait_jfc mock: 序列驱动返回值；返回 1 时回填 g_waitJfcOut；
 * 第二次及以后调用若设置了 g_pollerToStop 则置 stopping_ 让 EventPoll 循环退出 */
static int MockWaitJfc(urma_jfce_t *, uint32_t, int, urma_jfc_t *jfc[])
{
    int idx = g_waitJfcCalls++;
    if (idx > 0 && g_pollerToStop != nullptr) {
        g_pollerToStop->stopping_ = true;
    }
    int ret = (idx < g_waitJfcSeqLen) ? g_waitJfcSeq[idx] : -1;
    if (ret == 1) {
        jfc[0] = g_waitJfcOut;
    }
    return ret;
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
    g_lastAddDataPtr = nullptr;
    g_epollWaitAutoFill = false;
    g_lastEvents = nullptr;
    g_lastMaxevents = 0;
    g_epollWaitReturn = 0;
    g_epollWaitCalls = 0;
    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;
    g_eventfdReadFd = -1;
    g_libcCloseCalls = 0;
    g_cqeHandlerCalls = 0;
    g_cqeHandlerLastCnt = 0;
    g_ackJfcCalls = 0;
    g_ackJfcLastNevents = 0;
    g_deleteJfcCalls = 0;
    g_waitJfcCalls = 0;
    g_waitJfcSeq[0] = 0;
    g_waitJfcSeq[1] = 0;
    g_waitJfcSeqLen = 0;
    g_waitJfcOut = nullptr;
    g_pollerToStop = nullptr;
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
        /* UT 不经过 ubsocket_init: 手动创建全局 socket-epoll 读写锁, 让 mapper 的加锁
         * 路径被真实覆盖; 否则句柄为空, 加锁形同虚设且每次访问都刷 "pointer is nullptr" */
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
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
        RestoreClosePtr(); /* 防止 close_ptr mock 残留污染后续用例 */
        ArraySet<Socket>::GetInstance().ReleaseAll();
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
    }

    void SetUpMocks()
    {
        MOCKER(epoll_ctl).stubs().will(invoke(MockEpollCtl));
        MOCKER(epoll_wait).stubs().will(invoke(MockEpollWait));
        MOCKER(eventfd_write).stubs().will(invoke(MockEventfdWrite));
        MOCKER(eventfd_read).stubs().will(invoke(MockEventfdRead));
    }

    /* SetUpMocks + close 双路拦截 (LibcApi::close_ptr 与裸 close) */
    void SetUpCloseMocks()
    {
        SetUpMocks();
        if (LibcApi::close_ptr == nullptr) {
            (void)LibcApi::Load(); /* close_ptr 原始地址只保存一次 */
        }
        if (g_origClosePtr == nullptr) {
            g_origClosePtr = LibcApi::close_ptr;
        }
        /* 拦截两条 close 路径: LibcApi::close_ptr(ubsocket_close 尾部) 与裸 close(event_fd_)(UnInitialize) */
        LibcApi::close_ptr = MockLibcCloseForTest;
        MOCKER(::close).stubs().will(invoke(MockLibcCloseForTest));
    }

    void CreateAndRegisterUrmaSocket(int fd = TEST_SOCK_FD, int event_fd = TEST_EVENT_FD)
    {
        auto sock = MakeRef<UrmaSocket>(fd);
        sock->raw_socket_ = fd;
        sock->event_fd_ = event_fd;
        sock->writable_ready_.store(true, std::memory_order_release);
        sock->socket_readable_ready_.store(false, std::memory_order_release);
        sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
        ArraySet<Socket>::GetInstance().OverrideItem(fd, sock.Get());
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

TEST_F(UrmaEventPollTest, EpollCtlDel_RawSocket_NotRegistered_ReturnsENOENT)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);

    errno = 0;
    int ret = poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOENT, errno);

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

TEST_F(UrmaEventPollTest, EpollCtlMod_RawSocket_NoEventData_SetsErrnoENOENT)
{
    SetUpMocks();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOENT, errno);

    GlobalMockObject::verify();
}

/* 内核语义：MOD 未注册 fd 返回 -1/ENOENT（与 DEL 一致），urma socket 路径亦然 */
TEST_F(UrmaEventPollTest, EpollCtlMod_UrmaSocket_NotRegistered_ReturnsENOENT)
{
    SetUpMocks();
    CreateAndRegisterUrmaSocket();
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN; /* 已注册 ArraySet 的 urma socket，但从未 ADD 到本 epoll */

    int ret = poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOENT, errno);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollCtl_FdEqualsEpfd_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    evt.events = EPOLLIN;

    EXPECT_EQ(-1, poll.EpollCtl(EPOLL_CTL_ADD, TEST_EPOLL_FD, &evt));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_EQ(-1, poll.EpollCtl(EPOLL_CTL_MOD, TEST_EPOLL_FD, &evt));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_EQ(-1, poll.EpollCtl(EPOLL_CTL_DEL, TEST_EPOLL_FD, nullptr));
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
    SetUpMocks();
    g_epollWaitReturn = 0; /* 底层无就绪事件 */
    UrmaEventPoll poll(TEST_EPOLL_FD);

    int ret = poll.EpollWait(nullptr, 1, 0);
    EXPECT_EQ(0, ret);
    EXPECT_EQ(nullptr, g_lastEvents);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_NullEvents_Ready_ReturnsEFAULT)
{
    SetUpMocks();
    g_epollWaitReturn = -1;
    errno = EFAULT;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    int ret = poll.EpollWait(nullptr, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EFAULT, errno);

    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_NegativeMaxevents_SetsErrnoEINVAL)
{
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollWait(&evt, -1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaEventPollTest, EpollWait_ZeroMaxevents_SetsErrnoEINVAL)
{
    /* 对齐内核契约：maxevents == 0 属非法入参（内核返回 EINVAL） */
    UrmaEventPoll poll(TEST_EPOLL_FD);
    struct epoll_event evt {};
    int ret = poll.EpollWait(&evt, 0, 0);
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

TEST_F(UrmaEventPollTest, EpollWait_LtMode_RewritesEventfd_KeepsReachBits)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN; /* 未置 EPOLLET → LT */
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLIN);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_NE(0u, evt.events & EPOLLIN);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdReadFd);  /* 事件 eventfd 被读空 */
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd); /* LT 保活：回写 eventfd */
    EXPECT_EQ(1, g_eventfdWriteCount);
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLIN); /* 达位保留 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_EtMode_ClearsReachBits_NoEventfdWrite)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN | EPOLLET; /* ET */
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    urma_sock->SetReachEpollEvents(EPOLLIN);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_NE(0u, evt.events & EPOLLIN); /* 本轮正常上报 */
    EXPECT_EQ(-1, g_eventfdWriteFd);     /* 不回写 */
    EXPECT_EQ(0, g_eventfdWriteCount);
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents() & EPOLLIN); /* 达位已清 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_NoReachBits_DoesNotRewriteNorClear)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN; /* LT 但无达位数据 */
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket(); /* reach 默认 0 */
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(0, ret); /* 无达位空事件被过滤，不返回给调用方（对齐内核：无事件返回 0） */
    EXPECT_EQ(0u, evt.events);
    EXPECT_EQ(-1, g_eventfdWriteFd);
    EXPECT_EQ(0, g_eventfdWriteCount); /* 无数据不保活，防 LT 空转 */
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents());
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, NotifyClosed_ReachHupRdhup_WakesEventfd)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();

    /* NotifyClosed 不依赖上层关注位（events_ 为 0 也必须注入） */
    EXPECT_EQ(0, urma_sock->NotifyClosed());

    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLHUP);
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLRDHUP);
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd); /* 唤醒 eventfd 使 epoll_wait 可感知 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_ClosedSocket_ReportsHupUnconditionally)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN | EPOLLET; /* 上层只关注 IN，HUP 仍须上报 */
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    ASSERT_EQ(0, urma_sock->NotifyClosed());
    g_eventfdWriteFd = -1; /* 清除 NotifyClosed 的唤醒记录，仅观察 EpollWait 是否回写 */
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_NE(0u, evt.events & EPOLLHUP);
    EXPECT_NE(0u, evt.events & EPOLLRDHUP);
    EXPECT_EQ(-1, g_eventfdWriteFd); /* 关闭终态：不回写 eventfd，防空转 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, EpollWait_LtMode_ClosedSocket_NoEventfdRewrite)
{
    struct epoll_event kernel_evt {};
    kernel_evt.events = EPOLLIN; /* LT */
    EpollEvent event_data(EPOLL_EVENT_UB_SOCKET_IN, TEST_SOCK_FD, kernel_evt);
    kernel_evt.data.ptr = &event_data;

    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    ASSERT_EQ(0, urma_sock->NotifyClosed());
    g_eventfdWriteFd = -1; /* 清除 NotifyClosed 的唤醒记录，仅观察 EpollWait 是否回写 */
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.data.ptr = &event_data;
    int ret = poll.EpollWait(&evt, 1, 100);
    EXPECT_EQ(1, ret);
    EXPECT_NE(0u, evt.events & EPOLLHUP); /* LT 下关闭同样无条件上报 */
    EXPECT_NE(0u, evt.events & EPOLLRDHUP);
    EXPECT_EQ(-1, g_eventfdWriteFd);      /* 不回写，防关闭后 LT 空转 */
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLHUP); /* HUP 达位保留（终态） */
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
        /* 同 UrmaEventPollTest: UT 不经过 ubsocket_init, 手动创建全局 socket-epoll 读写锁 */
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
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
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
    }

    void RegisterDriverMocks()
    {
        UrmaApi::urma_poll_jfc_ptr = MockPollJfc;
        UrmaApi::urma_rearm_jfc_ptr = MockRearmJfc;
        UrmaApi::urma_ack_jfc_ptr = MockAckJfc;
        UrmaApi::urma_wait_jfc_ptr = MockWaitJfc;
        UrmaApi::urma_delete_jfc_ptr = MockDeleteJfc;
        UrmaApi::urma_delete_jfce_ptr = MockDeleteJfce;
        UrmaApi::urma_delete_context_ptr = MockDeleteContext;
    }

    void UnregisterDriverMocks()
    {
        UrmaApi::urma_poll_jfc_ptr = nullptr;
        UrmaApi::urma_rearm_jfc_ptr = nullptr;
        UrmaApi::urma_ack_jfc_ptr = nullptr;
        UrmaApi::urma_wait_jfc_ptr = nullptr;
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
        sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
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
    EXPECT_EQ(0, g_ackJfcCalls); /* jfc_->Ack removed */
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

TEST_F(UrmaEventPollDispatcherTest, Poller_PollAndDispatch_DrainUntilZero)
{
    auto env = MakeDispatcherPollerEnv();

    g_pollJfcSeq[0] = 2;
    g_pollJfcSeq[1] = 0;
    g_pollJfcSeqLen = 2;
    ock::ubs::Result res = env.poller->PollAndDispatch();
    EXPECT_EQ(UBS_OK, res);
    EXPECT_EQ(0, g_ackJfcCalls);
    EXPECT_EQ(1, g_cqeHandlerCalls);
    EXPECT_EQ(2, g_cqeHandlerLastCnt);
    EXPECT_GE(g_rearmJfcCalls, 1);
}

TEST_F(UrmaEventPollDispatcherTest, Poller_EventPoll_AcksAfterEachWait)
{
    auto env = MakeDispatcherPollerEnv();
    g_waitJfcOut = env.rawJfc.get(); /* wait 返回本 poller 的 jfc 句柄 */
    g_pollerToStop = env.poller.Get();
    g_waitJfcSeq[0] = 1; /* 首次 wait 返回事件；第二次由 mock 置 stopping_ 并返回 -1 退出循环 */
    g_waitJfcSeqLen = 1;
    g_pollJfcSeq[0] = 0; /* DrainCqes 首轮 poll 返回 0 → rearm → 退出 */
    g_pollJfcSeqLen = 1;

    env.poller->EventPoll();

    EXPECT_EQ(2, g_waitJfcCalls);      /* 一次事件 + 一次退出 */
    EXPECT_EQ(1, g_ackJfcCalls);       /* 每个 wait 事件恰好 ack 一次 */
    EXPECT_EQ(1u, g_ackJfcLastNevents); /* nevents=1 */
    EXPECT_GE(g_rearmJfcCalls, 1);
}

TEST_F(UrmaEventPollDispatcherTest, Poller_EventPoll_WaitError_NoAck)
{
    auto env = MakeDispatcherPollerEnv();
    g_pollerToStop = env.poller.Get();
    g_waitJfcSeqLen = 0; /* 所有 wait 返回 -1（错误）；第二次置 stopping_ 退出 */

    env.poller->EventPoll();

    EXPECT_EQ(2, g_waitJfcCalls);
    EXPECT_EQ(0, g_ackJfcCalls); /* 错误返回无事件，不得 ack */
}

/* Destroy 兜底：停止竞态残留事件（stop 前最后一波）也必须排空并 ack，
 * 否则 delete_jfc 仍会失败 */
TEST_F(UrmaEventPollDispatcherTest, Jfc_Destroy_DrainsAndAcksResidualEvents)
{
    auto env = MakeDispatcherPollerEnv();
    g_waitJfcOut = env.rawJfc.get();
    g_waitJfcSeq[0] = 1; /* 一条残留事件，随后 wait 返回 -1 表示无更多 */
    g_waitJfcSeqLen = 1;
    g_pollJfcSeq[0] = 3; /* 残留 CQE 一批 3 条，随后 0 表示排空 */
    g_pollJfcSeq[1] = 0;
    g_pollJfcSeqLen = 2;

    env.jfc->Destroy();

    EXPECT_EQ(1, g_ackJfcCalls);       /* 残留事件被 ack */
    EXPECT_GE(g_pollJfcCalls, 2);      /* CQE 轮询到排空 */
    EXPECT_EQ(1, g_deleteJfcCalls);    /* delete_jfc 正常执行 */
}

/* ===================== P4: EPOLLONESHOT =====================
 * ONESHOT 用例统一走 g_epollWaitAutoFill（内核回传内部条目）路径，
 * 保证"触发置禁 / 门控 / MOD 清禁用"落在同一 EpollEvent 上形成闭环 */

TEST_F(UrmaEventPollTest, Oneshot_FirstWaitReports_SecondWaitSilent)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100));
    EXPECT_NE(0u, wait_evt.events & EPOLLIN); /* 第一次正常上报 */
    EXPECT_EQ(0, g_eventfdWriteCount);        /* ONESHOT 触发不回写（对齐内核） */

    /* 数据未消费，第二次 wait 应静默（disabled 门控） */
    struct epoll_event wait_evt2 {};
    ASSERT_EQ(0, poll.EpollWait(&wait_evt2, 1, 100)); /* 禁用门控：空事件被过滤，返回 0 */
    EXPECT_EQ(0u, wait_evt2.events); /* 上报一次后禁用 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_Disabled_NewDataStillSilent)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100)); /* 触发 → 禁用 */
    EXPECT_NE(0u, wait_evt.events & EPOLLIN);

    /* 禁用期间新数据注入：达位累计但不上报 */
    urma_sock->FetchOrReachEpollEvents(EPOLLIN);
    struct epoll_event wait_evt2 {};
    ASSERT_EQ(0, poll.EpollWait(&wait_evt2, 1, 100)); /* 禁用即静默：空事件被过滤，返回 0 */
    EXPECT_EQ(0u, wait_evt2.events); /* 禁用即静默 */
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLIN); /* 达位保留待重武装 */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_ModReArm_ReportsPendingAgain)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100)); /* 触发 → 禁用 */

    /* 禁用期注入 pending，MOD 重武装（保持 ONESHOT 语义） */
    urma_sock->FetchOrReachEpollEvents(EPOLLIN);
    struct epoll_event rearm_evt {};
    rearm_evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &rearm_evt));

    struct epoll_event wait_evt2 {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt2, 1, 100));
    EXPECT_NE(0u, wait_evt2.events & EPOLLIN); /* 重武装后立即上报 pending */
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_ModReArm_WakesBlockedWait)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    /* 注册（无达位，种子不点火）→ 注入达位 → wait 触发禁用 */
    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100)); /* 触发 → 禁用 */
    GlobalMockObject::verify();

    /* 禁用期注入 pending（仅 FetchOr，无 eventfd 令牌；种子标志均置 false） */
    SetUpMocks();
    urma_sock->writable_ready_.store(false, std::memory_order_release);
    urma_sock->socket_readable_ready_.store(false, std::memory_order_release);
    urma_sock->FetchOrReachEpollEvents(EPOLLIN);
    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;

    struct epoll_event rearm_evt {};
    rearm_evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &rearm_evt));
    /* 唤醒补灌：种子 Exchange 均为 false，pending 只体现在达位上，
     * MOD 必须检查 reach & mask 并补灌 eventfd，否则阻塞的 wait 无人唤醒 */
    EXPECT_EQ(TEST_EVENT_FD, g_eventfdWriteFd);
    EXPECT_EQ(1, g_eventfdWriteCount);
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_DelAfterFire_Succeeds)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100)); /* 触发 → 禁用（非摘除） */

    /* 内核契约：触发后 DEL 仍成功（注册关系保留） */
    errno = 0;
    EXPECT_EQ(0, poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr));
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_AddAfterFire_StillEexist)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    urma_sock->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true;
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100)); /* 触发 → 禁用 */

    /* 内核契约：禁用态注册关系仍在，重 ADD 返回 EEXIST 而非成功 */
    errno = 0;
    struct epoll_event add_evt {};
    add_evt.events = EPOLLIN | EPOLLONESHOT;
    EXPECT_EQ(-1, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &add_evt));
    EXPECT_EQ(EEXIST, errno);
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_Closed_FiresOnceThenSilent)
{
    CreateAndRegisterUrmaSocket();
    auto sock = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock = RefConvert<Socket, UrmaSocket>(sock);
    SetUpMocks();
    ASSERT_EQ(0, urma_sock->NotifyClosed());
    g_eventfdWriteFd = -1;
    g_eventfdWriteCount = 0;
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt {};
    evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));

    g_epollWaitAutoFill = true;
    /* 第一次：HUP 无条件上报 */
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100));
    EXPECT_NE(0u, wait_evt.events & EPOLLHUP);
    EXPECT_NE(0u, wait_evt.events & EPOLLRDHUP);

    /* 第二次：触发后禁用，HUP 达位常驻也不再上报（无条件位同样被门控） */
    struct epoll_event wait_evt2 {};
    ASSERT_EQ(0, poll.EpollWait(&wait_evt2, 1, 100)); /* 空事件被过滤，返回 0 */
    EXPECT_EQ(0u, wait_evt2.events);

    /* MOD 重武装后：HUP 达位保留 → 立即再报（对齐内核 closed fd 重武装行为） */
    struct epoll_event rearm_evt {};
    rearm_evt.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_MOD, TEST_SOCK_FD, &rearm_evt));
    struct epoll_event wait_evt3 {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt3, 1, 100));
    EXPECT_NE(0u, wait_evt3.events & EPOLLHUP);
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Oneshot_FreelistReuse_NotDisabled)
{
    static const int TEST_SOCK_FD2 = 101;
    static const int TEST_EVENT_FD2 = 201;

    /* fd A：注册 ONESHOT → 内核回传触发禁用 → DEL（条目进 freelist） */
    CreateAndRegisterUrmaSocket(TEST_SOCK_FD, TEST_EVENT_FD);
    auto sock_a = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD);
    auto urma_sock_a = RefConvert<Socket, UrmaSocket>(sock_a);
    SetUpMocks();
    g_epollWaitReturn = 1;
    UrmaEventPoll poll(TEST_EPOLL_FD);

    struct epoll_event evt_a {};
    evt_a.events = EPOLLIN | EPOLLONESHOT;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt_a));
    urma_sock_a->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true; /* wait 经内核回传真实内部条目，禁用标记落在其上 */
    struct epoll_event wait_evt {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt, 1, 100));
    EXPECT_NE(0u, wait_evt.events & EPOLLIN); /* A 触发上报 */
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_DEL, TEST_SOCK_FD, nullptr)); /* 禁用条目回收 */
    GlobalMockObject::verify();

    /* fd B：ADD 复用 A 的条目，oneshot_disabled 必须被复位，否则 B 永久静默 */
    SetUpMocks();
    CreateAndRegisterUrmaSocket(TEST_SOCK_FD2, TEST_EVENT_FD2);
    auto sock_b = ArraySet<Socket>::GetInstance().GetItem(TEST_SOCK_FD2);
    auto urma_sock_b = RefConvert<Socket, UrmaSocket>(sock_b);

    struct epoll_event evt_b {};
    evt_b.events = EPOLLIN;
    ASSERT_EQ(0, poll.EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD2, &evt_b));
    urma_sock_b->SetReachEpollEvents(EPOLLIN);

    g_epollWaitAutoFill = true; /* 回传的是复用条目（内核视角 data.ptr 不变） */
    struct epoll_event wait_evt_b {};
    ASSERT_EQ(1, poll.EpollWait(&wait_evt_b, 1, 100));
    EXPECT_NE(0u, wait_evt_b.events & EPOLLIN); /* 复用条目未带禁用脏状态 */
    GlobalMockObject::verify();
}

/* ---- close 注册表清理钩子 OnSocketClosed ---- */
class ScopedEventPollRegistration {
public:
    ScopedEventPollRegistration(int epfd, EventPoll *poll) : epfd_(epfd)
    {
        (void)ArraySet<EventPoll>::GetInstance().Init();
        (void)ArraySet<EventPoll>::GetInstance().OverrideItem(epfd_, poll);
    }

    ~ScopedEventPollRegistration()
    {
        (void)ArraySet<EventPoll>::GetInstance().OverrideItem(epfd_, nullptr);
    }

private:
    int epfd_;
};

TEST_F(UrmaEventPollTest, Close_UrmaSocket_CleansMapperAndSocketData)
{
    SetUpCloseMocks();
    CreateAndRegisterUrmaSocket();
    Ref<UrmaEventPoll> pollRef = MakeRef<UrmaEventPoll>(TEST_EPOLL_FD);
    UrmaEventPoll *poll = pollRef.Get();
    ScopedEventPollRegistration pollReg(TEST_EPOLL_FD, poll);

    struct epoll_event evt {};
    evt.events = EPOLLIN;
    ASSERT_EQ(0, poll->EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    ASSERT_NE(nullptr, GetSocketEpollMapper(TEST_SOCK_FD));
    ASSERT_TRUE(poll->socket_data_.find(TEST_SOCK_FD) != poll->socket_data_.end());

    EXPECT_EQ(0, ubsocket_close(TEST_SOCK_FD));

    /* close 后注册表双残留必须被清: 全局 EpollMapper + EventPoll 实例的 socket_data_ */
    EXPECT_EQ(nullptr, GetSocketEpollMapper(TEST_SOCK_FD));
    EXPECT_TRUE(poll->socket_data_.find(TEST_SOCK_FD) == poll->socket_data_.end());
    GlobalMockObject::verify();
}

TEST_F(UrmaEventPollTest, Close_FdReuse_AddAgain_NoEEXIST)
{
    SetUpCloseMocks();
    CreateAndRegisterUrmaSocket();
    Ref<UrmaEventPoll> pollRef = MakeRef<UrmaEventPoll>(TEST_EPOLL_FD);
    UrmaEventPoll *poll = pollRef.Get();
    ScopedEventPollRegistration pollReg(TEST_EPOLL_FD, poll);

    struct epoll_event evt {};
    evt.events = EPOLLIN;
    ASSERT_EQ(0, poll->EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));
    ASSERT_EQ(0, ubsocket_close(TEST_SOCK_FD));
    GlobalMockObject::verify();

    /* mockcpp: verify() 会校验并重置所有桩——后续 epoll_ctl/close 必须重新挂载,
     * 否则第二个 ADD 内部的 epoll_ctl(假 epfd=42) 打到真实 libc 返回 -1/EBADF */
    SetUpCloseMocks();

    /* fd 复用: 新 socket 同 fd, 再次 ADD 不应被残留条目误判为重复注册 */
    CreateAndRegisterUrmaSocket();
    errno = 0;
    int ret = poll->EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt);
    EXPECT_EQ(0, ret);
    EXPECT_NE(EEXIST, errno);
    GlobalMockObject::verify();

    /* cleanup: 第二个 socket (verify 已重置桩, cleanup close 前同样恢复拦截) */
    SetUpCloseMocks();
    (void)ubsocket_close(TEST_SOCK_FD);
    GlobalMockObject::verify();
}

/* ---- 并发安全: 并发 OnSocketClosed 不得 double-free ---- */
TEST_F(UrmaEventPollTest, OnSocketClosed_ConcurrentCalls_NoUafNoDoubleFree)
{
    SetUpCloseMocks();
    constexpr int ROUNDS = 200;
    for (int i = 0; i < ROUNDS; ++i) {
        CreateAndRegisterUrmaSocket();
        Ref<UrmaEventPoll> pollRef = MakeRef<UrmaEventPoll>(TEST_EPOLL_FD);
        UrmaEventPoll *poll = pollRef.Get();
        ScopedEventPollRegistration pollReg(TEST_EPOLL_FD, poll);

        struct epoll_event evt {};
        evt.events = EPOLLIN;
        ASSERT_EQ(0, poll->EpollCtl(EPOLL_CTL_ADD, TEST_SOCK_FD, &evt));

        std::atomic<int> go(0);
        auto racer = [&go]() {
            while (go.load(std::memory_order_acquire) == 0) {
            }
            OnSocketClosed(TEST_SOCK_FD); /* 与主线程竞速同一 mapper 的清理 */
        };
        std::thread t(racer);
        go.store(1, std::memory_order_release);
        OnSocketClosed(TEST_SOCK_FD);
        t.join();

        /* 收尾: mapper 已删, 第二次(串行)调用应零开销返回 */
        OnSocketClosed(TEST_SOCK_FD);
        EXPECT_EQ(nullptr, GetSocketEpollMapper(TEST_SOCK_FD));
        (void)ubsocket_close(TEST_SOCK_FD);
    }
    GlobalMockObject::verify();
}
