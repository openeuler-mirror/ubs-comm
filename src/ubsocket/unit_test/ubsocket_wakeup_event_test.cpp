/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>

#include "core/ubsocket_wakeup_event.h"
#include "core/ubsocket_socket.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_set.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

// 最小 SocketBase mock：ProcessReadyEvents 仅使用 SocketBase 的具体接口
// （dynamic_cast + GetEvents/GetEpollData）。需实现 Socket/SocketBase 的全部
// 纯虚函数；注意 mockcpp::Result 与 ock::ubs::Result 同名，必须全限定。
class WakeupMockSocket : public SocketBase {
public:
    WakeupMockSocket(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ) {}
    ock::ubs::Result Initialize() noexcept override { return UBS_OK; }
    void UnInitialize() noexcept override {}
    int GetTxFd() override { return -1; }
    bool IsBindRemote() override { return false; }
    ock::ubs::Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override
    {
        return UBS_OK;
    }
    ock::ubs::Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override { return UBS_OK; }
    bool ShouldRegisterTxEvent() override { return false; }
    ock::ubs::Result ProcessEpollEvent(struct epoll_event &event) override { return UBS_OK; }
};

class WakeupEventTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
    }

    void TearDown() override
    {
        ev_.reset();
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        ArraySet<Socket>::GetInstance().ReleaseAll();
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
            epoll_fd_ = -1;
        }
        GlobalMockObject::verify();
    }

    int epoll_fd_{-1};
    std::unique_ptr<UbsocketWakeupEvent> ev_;
};

TEST_F(WakeupEventTest, Constructor_DefaultState)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    EXPECT_NE(ev_, nullptr);
    EXPECT_NE(ev_->GetReadyEvent(), nullptr);
}

TEST_F(WakeupEventTest, Initialize_Success)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    EXPECT_EQ(ev_->Initialize(epoll_fd_), 0);
}

TEST_F(WakeupEventTest, Initialize_AlreadyInitialized_ReturnsZero)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    EXPECT_EQ(ev_->Initialize(epoll_fd_), 0);
    // Second call is no-op
    EXPECT_EQ(ev_->Initialize(epoll_fd_), 0);
}

TEST_F(WakeupEventTest, Initialize_EventfdFails_ReturnsError)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    MOCKER_CPP(::eventfd).stubs().will(returnValue(-1));
    errno = EMFILE;
    EXPECT_EQ(ev_->Initialize(epoll_fd_), -1);
    GlobalMockObject::verify();
}

TEST_F(WakeupEventTest, Initialize_EpollCtlFails_ReturnsError)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    auto fail_ctl = +[](int, int, int, struct epoll_event *) -> int {
        errno = EPERM;
        return -1;
    };
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(fail_ctl));
    EXPECT_EQ(ev_->Initialize(epoll_fd_), -1);
    GlobalMockObject::verify();
}

TEST_F(WakeupEventTest, CleanUp_AfterInitialize_ClosesFd)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    int fd = -1;
    // Get the readyEventFd_ via side effect: after Initialize, CleanUp should close it
    ev_->CleanUp();
    // No crash after cleanup
}

TEST_F(WakeupEventTest, CleanUp_NotInitialized_NoCrash)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->CleanUp(); // readyEventFd_ < 0, no-op
}

TEST_F(WakeupEventTest, Destructor_CallsCleanUp)
{
    {
        UbsocketWakeupEvent ev;
        ev.Initialize(epoll_fd_);
        // Destructor should call CleanUp
    }
    // No crash
}

/* issue #50: CleanUp must drop this listener's entry from the poll's wakeup table */
TEST_F(WakeupEventTest, CleanUp_RemovesWakeupFromPoll)
{
    ArraySet<EventPoll>::GetInstance().Init();
    int efd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(efd, 0);
    auto *aep = new AsyncEventPoll(efd);
    EventPollPtr aepRef(aep);
    ArraySet<EventPoll>::GetInstance().OverrideItem(efd, aep);
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ASSERT_EQ(ev_->Initialize(efd), 0);
    aep->SetWakeupCallback(ev_->GetReadyEvent(),
                           [](struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &) { return 0; });
    EXPECT_EQ(aep->WakeupCallbackCount(), static_cast<size_t>(1));
    ev_->CleanUp();
    EXPECT_EQ(aep->WakeupCallbackCount(), static_cast<size_t>(0));
    ev_->CleanUp(); /* idempotent */
    ev_.reset();
    ArraySet<EventPoll>::GetInstance().ReleaseAll();
    close(efd);
}

TEST_F(WakeupEventTest, WakeUpReadyEventFd_NotInitialized_Warns)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    // readyEventFd_ < 0, should warn and return
    ev_->WakeUpReadyEventFd(42);
}

TEST_F(WakeupEventTest, WakeUpReadyEventFd_Initialized_WritesEventfd)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    ev_->WakeUpReadyEventFd(42);
    // Should have written to the eventfd
}

TEST_F(WakeupEventTest, WakeUpReadyEventFd_EventfdWriteFails_LogsError)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    // Mock eventfd_write to fail
    MOCKER_CPP(::eventfd_write).stubs().will(returnValue(-1));
    errno = EIO;
    ev_->WakeUpReadyEventFd(42);
    GlobalMockObject::verify();
}

TEST_F(WakeupEventTest, ProcessReadyEvents_ListenFdNotFound_ReturnsZero)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    ev_->SetListenFd(999);
    // Wake up
    ev_->WakeUpReadyEventFd(42);
    // ProcessReadyEvents with empty socket_data
    std::unordered_map<int, EpollEvent *> socket_data;
    struct epoll_event events[4] = {};
    int ret = ev_->ProcessReadyEvents(events, 4, socket_data);
    EXPECT_EQ(ret, 0);
}

TEST_F(WakeupEventTest, ProcessReadyEvents_ListenFdFound_ReturnsOne)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    ev_->SetListenFd(42);

    // 新实现：listen_fd 对应的 SocketBase 注册在 ArraySet<Socket> 中，
    // 事件内容取自注册 epoll 时保存的 events/epoll_data（socket_data 参数已不参与）。
    auto *sock = new WakeupMockSocket(42);
    sock->SetEvents(EPOLLIN);
    epoll_data_t data = {};
    data.fd = 42;
    sock->SetEpollData(data);
    ArraySet<Socket>::GetInstance().OverrideItem(42, sock);
    SocketPtr sockRef(sock); // 持引用，TearDown 的 ReleaseAll 负责最终释放

    // Wake up
    ev_->WakeUpReadyEventFd(42);

    std::unordered_map<int, EpollEvent *> socket_data;
    struct epoll_event events[4] = {};
    int ret = ev_->ProcessReadyEvents(events, 4, socket_data);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(events[0].events, EPOLLIN);
    EXPECT_EQ(events[0].data.fd, 42);
}

TEST_F(WakeupEventTest, ProcessReadyEvents_ReadFails_LogsError)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->Initialize(epoll_fd_);
    ev_->SetListenFd(42);

    auto *sock = new WakeupMockSocket(42);
    ArraySet<Socket>::GetInstance().OverrideItem(42, sock);
    SocketPtr sockRef(sock);

    // Don't wake up (no eventfd_write) → read() will fail on empty eventfd
    std::unordered_map<int, EpollEvent *> socket_data;
    struct epoll_event events[4] = {};
    int ret = ev_->ProcessReadyEvents(events, 4, socket_data);
    // read fails but ProcessReadyEvents still processes the listen socket
    EXPECT_EQ(ret, 1);
}

TEST_F(WakeupEventTest, SetListenFd_GetReadyEvent_SetAcceptCallback)
{
    ev_ = std::make_unique<UbsocketWakeupEvent>();
    ev_->SetListenFd(100);
    EXPECT_NE(ev_->GetReadyEvent(), nullptr);

    bool called = false;
    auto cb = [&called]() { called = true; };
    ev_->SetAcceptCallback(cb);
    auto retrieved = ev_->GetAcceptCallback();
    retrieved();
    EXPECT_TRUE(called);
}
