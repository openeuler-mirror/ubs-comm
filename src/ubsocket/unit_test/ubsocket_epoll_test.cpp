/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the license at:
 * http://license.coscl.org.cn/MulanPSL2
 */

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_event_epoll.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>

#include <gtest/gtest.h>

extern "C" {
int ubsocket_epoll_create(int size);
int ubsocket_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int ubsocket_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int ubsocket_epoll_create1(int flags);
int ubsocket_epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask);
}

using ock::ubs::GlobalSetting;
using ock::ubs::LibcApi;

namespace {

int MockCreate(int)
{
    return 77;
}
int MockCreateFail(int)
{
    return -1;
}
int MockCreateSyscall(int)
{
    return static_cast<int>(syscall(SYS_epoll_create1, 0));
}
int MockCtl(int, int, int, struct epoll_event *)
{
    return 3;
}
int MockWait(int, struct epoll_event *, int, int)
{
    return 4;
}

class UbsocketEpollTest : public testing::Test {
protected:
    void SetUp() override
    {
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
        GlobalSetting::UBS_INITED = true;
        oldCreate = LibcApi::epoll_create_ptr;
        oldCreate1 = LibcApi::epoll_create1_ptr;
        oldCtl = LibcApi::epoll_ctl_ptr;
        oldWait = LibcApi::epoll_wait_ptr;
        LibcApi::epoll_create_ptr = MockCreate;
        LibcApi::epoll_create1_ptr = MockCreate;
        LibcApi::epoll_ctl_ptr = MockCtl;
        LibcApi::epoll_wait_ptr = MockWait;
    }

    void TearDown() override
    {
        LibcApi::epoll_create_ptr = oldCreate;
        LibcApi::epoll_create1_ptr = oldCreate1;
        LibcApi::epoll_ctl_ptr = oldCtl;
        LibcApi::epoll_wait_ptr = oldWait;
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
        GlobalSetting::UBS_INITED = true;
    }

    decltype(LibcApi::epoll_create_ptr) oldCreate;
    decltype(LibcApi::epoll_create1_ptr) oldCreate1;
    decltype(LibcApi::epoll_ctl_ptr) oldCtl;
    decltype(LibcApi::epoll_wait_ptr) oldWait;
};

TEST_F(UbsocketEpollTest, NativeModeDelegatesAllApis)
{
    epoll_event event{};
    EXPECT_EQ(ubsocket_epoll_create(1), 77);
    EXPECT_EQ(ubsocket_epoll_create1(EPOLL_CLOEXEC), 77);
    EXPECT_EQ(ubsocket_epoll_ctl(1, EPOLL_CTL_ADD, 2, &event), 3);
    EXPECT_EQ(ubsocket_epoll_wait(1, &event, 1, 0), 4);
    EXPECT_EQ(ubsocket_epoll_pwait(1, &event, 1, 0, nullptr), 4);
}

TEST_F(UbsocketEpollTest, NonNativeCreateFailuresPropagate)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    LibcApi::epoll_create_ptr = MockCreateFail;
    LibcApi::epoll_create1_ptr = MockCreateFail;
    EXPECT_EQ(ubsocket_epoll_create(1), -1);
    EXPECT_EQ(ubsocket_epoll_create1(0), -1);
}

TEST_F(UbsocketEpollTest, MissingEventPollReturnsBadFdWhenInitialized)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    epoll_event event{};
    errno = 0;
    EXPECT_EQ(ubsocket_epoll_ctl(1234, EPOLL_CTL_ADD, 1, &event), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(ubsocket_epoll_wait(1234, &event, 1, 0), -1);
    EXPECT_EQ(errno, EBADF);
    EXPECT_EQ(ubsocket_epoll_pwait(1234, &event, 1, 0, nullptr), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST_F(UbsocketEpollTest, MissingEventPollReturnsEagainDuringTeardown)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    GlobalSetting::UBS_INITED = false;
    epoll_event event{};
    EXPECT_EQ(ubsocket_epoll_ctl(1234, EPOLL_CTL_ADD, 1, &event), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(ubsocket_epoll_wait(1234, &event, 1, 0), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(ubsocket_epoll_pwait(1234, &event, 1, 0, nullptr), -1);
    EXPECT_EQ(errno, EAGAIN);
}

TEST_F(UbsocketEpollTest, NonNativeCreateSuccessRegistersEventPoll)
{
    GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    ock::ubs::LockRegistry::RegisterDefaultOps();
    ASSERT_EQ(ock::ubs::ArraySet<ock::ubs::EventPoll>::GetInstance().Init(), 0);
    LibcApi::epoll_create_ptr = MockCreateSyscall;
    LibcApi::epoll_create1_ptr = MockCreateSyscall;
    int epfd = ubsocket_epoll_create(1);
    ASSERT_GE(epfd, 0);
    EXPECT_NE(ock::ubs::ArraySet<ock::ubs::EventPoll>::GetInstance().GetItem(epfd).Get(), nullptr);
    epoll_event event{};
    EXPECT_LE(ubsocket_epoll_ctl(epfd, EPOLL_CTL_DEL, epfd, &event), 0);
    EXPECT_GE(ubsocket_epoll_wait(epfd, &event, 1, 0), 0);
    EXPECT_GE(ubsocket_epoll_pwait(epfd, &event, 1, 0, nullptr), 0);
    ock::ubs::ArraySet<ock::ubs::EventPoll>::GetInstance().ReleaseAll();

    int epfd1 = ubsocket_epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(epfd1, 0);
    EXPECT_NE(ock::ubs::ArraySet<ock::ubs::EventPoll>::GetInstance().GetItem(epfd1).Get(), nullptr);
    ock::ubs::ArraySet<ock::ubs::EventPoll>::GetInstance().ReleaseAll();
}

} // namespace
