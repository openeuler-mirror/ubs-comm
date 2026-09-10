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
#include <signal.h>
#include <fcntl.h>
#include <cerrno>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "core/urma/urma_event_epoll.h"
#include "core/urma/urma_setting.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

extern int ubsocket_epoll_create1(int flags);
extern int ubsocket_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
extern int ubsocket_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
extern int ubsocket_epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                const sigset_t *sigmask);

namespace {
sigset_t g_maskDuringWait;

int MockEpollWait(int, struct epoll_event *, int, int)
{
    pthread_sigmask(SIG_SETMASK, nullptr, &g_maskDuringWait);
    return 0;
}
} // namespace

class UbsocketEpollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        sigemptyset(&g_maskDuringWait);
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
        /* UT does not go through ubsocket_init, must manually register default lock ops,
         * otherwise EventPoll construction will segfault due to LOCK_OPS.create being nullptr */
        (void)LockRegistry::RegisterDefaultOps();
        ArraySet<EventPoll>::GetInstance().Init();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<EventPoll>::GetInstance().ReleaseAll();
    }
};

TEST_F(UbsocketEpollTest, EpollCreate1_InvalidFlags_ReturnsEinval)
{
    EXPECT_EQ(-1, ubsocket_epoll_create1(EPOLL_CLOEXEC | 0x1));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_EQ(-1, ubsocket_epoll_create1(0x2));
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UbsocketEpollTest, EpollCreate1_ZeroFlags_NoCloexec)
{
    int fd = ubsocket_epoll_create1(0);
    ASSERT_GE(fd, 0);

    int fd_flags = fcntl(fd, F_GETFD);
    ASSERT_GE(fd_flags, 0);
    EXPECT_EQ(0, fd_flags & FD_CLOEXEC);

    EXPECT_NE(nullptr, ArraySet<EventPoll>::GetInstance().GetItem(fd).Get());
}

TEST_F(UbsocketEpollTest, EpollCreate1_CloexecFlag_SetsCloexec)
{
    int fd = ubsocket_epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(fd, 0);

    int fd_flags = fcntl(fd, F_GETFD);
    ASSERT_GE(fd_flags, 0);
    EXPECT_NE(0, fd_flags & FD_CLOEXEC);
}

TEST_F(UbsocketEpollTest, EpollPwait_ReplacesSigmaskDuringWaitAndRestores)
{
    int epfd = ubsocket_epoll_create1(0);
    ASSERT_GE(epfd, 0);

    MOCKER(epoll_wait).stubs().will(invoke(MockEpollWait));

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    struct epoll_event evt {};
    EXPECT_EQ(0, ubsocket_epoll_pwait(epfd, &evt, 1, 0, &mask));

    EXPECT_TRUE(sigismember(&g_maskDuringWait, SIGUSR1));
    sigset_t cur;
    sigemptyset(&cur);
    ASSERT_EQ(0, pthread_sigmask(SIG_SETMASK, nullptr, &cur));
    EXPECT_FALSE(sigismember(&cur, SIGUSR1));
}

TEST_F(UbsocketEpollTest, EpollPwait_NullSigmask_NoMaskChange)
{
    int epfd = ubsocket_epoll_create1(0);
    ASSERT_GE(epfd, 0);

    MOCKER(epoll_wait).stubs().will(invoke(MockEpollWait));

    struct epoll_event evt {};
    EXPECT_EQ(0, ubsocket_epoll_pwait(epfd, &evt, 1, 0, nullptr));

    sigset_t cur;
    sigemptyset(&cur);
    ASSERT_EQ(0, pthread_sigmask(SIG_SETMASK, nullptr, &cur));
    EXPECT_FALSE(sigismember(&cur, SIGUSR1));
}

TEST_F(UbsocketEpollTest, InvalidEpfd_CtlWaitPwait_SetsErrnoEBADF)
{
    const int bad_epfd = 999999;
    struct epoll_event evt {};

    errno = 0;
    EXPECT_EQ(-1, ubsocket_epoll_ctl(bad_epfd, EPOLL_CTL_ADD, 0, &evt));
    EXPECT_EQ(EBADF, errno);

    errno = 0;
    EXPECT_EQ(-1, ubsocket_epoll_wait(bad_epfd, &evt, 1, 0));
    EXPECT_EQ(EBADF, errno);

    errno = 0;
    EXPECT_EQ(-1, ubsocket_epoll_pwait(bad_epfd, &evt, 1, 0, nullptr));
    EXPECT_EQ(EBADF, errno);
}

TEST_F(UbsocketEpollTest, EpollCreate1_InitFailure_ReturnsError)
{
    // Dispatcher 模式（无 poller 注册）下 UrmaEventPoll::Init() 返回 UBS_ERROR，
    // epoll_create1 必须调用 Init() 并在失败时返回 -1（对齐 epoll_create）。
    EpollHandleMode saved = UrmaSetting::UB_EPOLL_HANDLE_MODE;
    UrmaSetting::UB_EPOLL_HANDLE_MODE = DISPATCHER_THREAD_POOL;

    errno = 0;
    EXPECT_EQ(-1, ubsocket_epoll_create1(0));

    UrmaSetting::UB_EPOLL_HANDLE_MODE = saved;
}
