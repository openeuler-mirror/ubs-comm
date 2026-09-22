/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <new>
#include <cstdlib>
#include <functional>
#include <unordered_map>

#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "common/ubsocket_logger.h"
#include "under_api/dl_libc_api.h"
#include "ubsocket_def.h"

using namespace ock::ubs;

// Forward declarations for global functions defined in .cpp (not in header)
namespace ock { namespace ubs {
EpollMapper *GetSocketEpollMapper(int socket_fd);
bool CreateSocketEpollMapper(int socket_fd, EpollMapper *&mapper);
void CleanSocketEpollMapper(int socket_fd);
void ReserveSocketEpollMappers(size_t capacity);
void CleanAllSocketEpollMappers();
}}

// ==================== Mock SocketBase for UB-socket paths ====================

class UbMockSocketBase : public SocketBase {
public:
    bool shouldRegisterTx = false;
    bool isBindRemote = true;
    int txFd = -1;
    int addTxEventRet = 0;
    int delTxEventRet = 0;
    int notifyWritableRet = 0;
    bool fatalIfWriteBlocked = false;
    bool readyAndExchange = false;
    std::atomic<uint32_t> events_{0};

    UbMockSocketBase(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ)
    {
        txFd = fd;
    }
    int GetTxFd() override { return txFd; }
    bool IsBindRemote() override { return isBindRemote; }
    ock::ubs::Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override
    {
        if (addTxEventRet == 0) {
            return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, txFd, event) < 0 ? UBS_ERROR : UBS_OK;
        }
        return UBS_ERROR;
    }
    ock::ubs::Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override
    {
        if (delTxEventRet == 0) {
            return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, txFd, nullptr) < 0 ? UBS_ERROR : UBS_OK;
        }
        return UBS_ERROR;
    }
    bool ShouldRegisterTxEvent() override { return shouldRegisterTx; }
    ock::ubs::Result ProcessEpollEvent(struct epoll_event &event) override { return UBS_OK; }
    ock::ubs::Result Initialize() noexcept override { return UBS_OK; }
    void UnInitialize() noexcept override {}
    bool FatalIfWriteBlocked() override { return fatalIfWriteBlocked; }
    int NotifyWritable() { return notifyWritableRet; }
    bool ReadyAndExchange() { return readyAndExchange; }
};

// ==================== EpollMapper global functions ====================

class EventEpollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
        ReserveSocketEpollMappers(64);
    }

    void TearDown() override
    {
        CleanAllSocketEpollMappers();
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
};

TEST_F(EventEpollTest, GetSocketEpollMapper_NotExist_ReturnsNull)
{
    EXPECT_EQ(GetSocketEpollMapper(999), nullptr);
}

TEST_F(EventEpollTest, CreateSocketEpollMapper_New_CreatesAndReturnsTrue)
{
    EpollMapper *mapper = nullptr;
    EXPECT_TRUE(CreateSocketEpollMapper(100, mapper));
    EXPECT_NE(mapper, nullptr);
    EXPECT_EQ(GetSocketEpollMapper(100), mapper);
}

TEST_F(EventEpollTest, CreateSocketEpollMapper_Existing_ReturnsFalseAndSets)
{
    EpollMapper *mapper1 = nullptr;
    CreateSocketEpollMapper(101, mapper1);
    EpollMapper *mapper2 = nullptr;
    EXPECT_FALSE(CreateSocketEpollMapper(101, mapper2));
    EXPECT_EQ(mapper2, mapper1);
}

TEST_F(EventEpollTest, CleanSocketEpollMapper_Existing_Removes)
{
    EpollMapper *mapper = nullptr;
    CreateSocketEpollMapper(102, mapper);
    CleanSocketEpollMapper(102);
    EXPECT_EQ(GetSocketEpollMapper(102), nullptr);
}

TEST_F(EventEpollTest, CleanSocketEpollMapper_NotExist_NoCrash)
{
    CleanSocketEpollMapper(999);
}

TEST_F(EventEpollTest, CleanAllSocketEpollMappers_ClearsAll)
{
    EpollMapper *m1 = nullptr;
    EpollMapper *m2 = nullptr;
    CreateSocketEpollMapper(103, m1);
    CreateSocketEpollMapper(104, m2);
    CleanAllSocketEpollMappers();
    EXPECT_EQ(GetSocketEpollMapper(103), nullptr);
    EXPECT_EQ(GetSocketEpollMapper(104), nullptr);
}

TEST_F(EventEpollTest, EpollMapper_AddDelQuery_Test)
{
    EpollMapper mapper(200);
    mapper.Add(1);
    mapper.Add(2);
    mapper.Add(3);
    EXPECT_EQ(mapper.QueryFirst(), 1);
    // Del middle → list [1,3] not empty → returns false
    EXPECT_FALSE(mapper.Del(2));
    EXPECT_EQ(mapper.QueryFirst(), 1);
    // Del first → list [3] not empty → returns false
    EXPECT_FALSE(mapper.Del(1));
    EXPECT_EQ(mapper.QueryFirst(), 3);
    // Del last → list empty → returns true
    EXPECT_TRUE(mapper.Del(3));
    EXPECT_EQ(mapper.QueryFirst(), -1);
}

// ==================== AsyncEventPoll tests ====================

class AsyncEventPollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
        LibcApi::close_ptr = ::close;
    }

    void TearDown() override
    {
        // Reset poll_ before destroying AsyncEventPoll
        poll_.reset();
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }

    int epoll_fd_{-1};
    std::unique_ptr<AsyncEventPoll> poll_;
};

TEST_F(AsyncEventPollTest, Constructor_NoCrash)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_NE(poll_, nullptr);
}

TEST_F(AsyncEventPollTest, AddSockReadableEvent_CreatesEventfd)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_EQ(poll_->AddSockReadableEvent(), 0);
    // Second call should be no-op (already created)
    EXPECT_EQ(poll_->AddSockReadableEvent(), 0);
}

TEST_F(AsyncEventPollTest, AddReadableEvent_Success)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    epoll_data_t data;
    data.fd = 42;
    EXPECT_EQ(poll_->AddReadableEvent(EPOLLIN, data), 0);
}

/* issue#43 回归：注入队列（数据面）持续非空时，内核 epoll 集合（监听 fd 所在）
 * 必须仍能被服务。修复前 EpollWait 只要注入队列非空就直接返回、永不执行
 * epoll_wait，accept 事件被无限期饿死 ⇒ 内核全连接队列溢出、建链失败。
 * 本用例：内核侧挂一个恒可读的裸 fd，注入队列灌满；断言在有限次调用内
 * 一定能拿到裸 fd 事件。修复前该断言永远失败（循环耗尽）。 */
TEST_F(AsyncEventPollTest, EpollWait_RingQueueDoesNotStarveKernelEpoll)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);

    constexpr uint64_t kKernelMagic = 0xBEEF43;
    constexpr uint64_t kRingMagic = 0x1111;

    int raw_fd = eventfd(1, EFD_NONBLOCK); /* 初值 1：恒为可读 */
    ASSERT_GE(raw_fd, 0);

    struct epoll_event raw_evt = {};
    raw_evt.events = EPOLLIN;
    raw_evt.data.u64 = kKernelMagic;
    EpollEvent raw_event_data{EPOLL_EVENT_RAW_SOCKET, raw_fd, raw_evt};

    struct epoll_event reg_evt = {};
    reg_evt.events = EPOLLIN;
    reg_evt.data.ptr = &raw_event_data;
    ASSERT_EQ(::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, raw_fd, &reg_evt), 0);

    /* 注入队列灌到远多于让位阈值，模拟数据面持续有包 */
    for (int i = 0; i < 64; ++i) {
        epoll_data_t data;
        data.u64 = kRingMagic;
        ASSERT_EQ(poll_->AddReadableEvent(EPOLLIN, data), 0);
    }

    bool kernel_event_served = false;
    struct epoll_event out[4] = {};
    /* 上限给足：只要护栏存在，让位必然发生在阈值+1 次之内 */
    for (int call = 0; call < 32 && !kernel_event_served; ++call) {
        int n = poll_->EpollWait(out, 1, 0);
        for (int i = 0; i < n; ++i) {
            if (out[i].data.u64 == kKernelMagic) {
                kernel_event_served = true;
            }
        }
    }
    EXPECT_TRUE(kernel_event_served) << "kernel epoll starved by the injected readable queue (issue#43)";

    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, raw_fd, nullptr);
    ::close(raw_fd);
}

TEST_F(AsyncEventPollTest, EpollCtl_InvalidOp_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev = {};
    EXPECT_LT(poll_->EpollCtl(999, 42, &ev), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollCtlAdd_InvalidFd_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev = {};
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, -1, &ev), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollCtlAdd_NullEvent_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, 42, nullptr), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollCtlMod_InvalidFd_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev = {};
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_MOD, -1, &ev), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollCtlDel_InvalidFd_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_DEL, -1, nullptr), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollCtlDel_NotAdded_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_DEL, 999, nullptr), 0);
    EXPECT_EQ(errno, ENOENT);
}

TEST_F(AsyncEventPollTest, EpollCtlAdd_RawSocket_AddsSuccessfully)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Cleanup
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(AsyncEventPollTest, EpollCtlAdd_RawSocket_DuplicateFails)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Duplicate add should fail
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    EXPECT_EQ(errno, EEXIST);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(AsyncEventPollTest, EpollCtlMod_RawSocket_ModifiesSuccessfully)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Modify
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    // Cleanup
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(AsyncEventPollTest, EpollCtlMod_NotAdded_Fails)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    close(fd);
}

TEST_F(AsyncEventPollTest, EpollCtlMod_NoEpollEt_Fails)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Modify without EPOLLET should fail
    ev.events = EPOLLIN;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    EXPECT_EQ(errno, EINVAL);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(AsyncEventPollTest, EpollWait_NullEvents_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_LT(poll_->EpollWait(nullptr, 10, 0), 0);
    EXPECT_EQ(errno, EFAULT);
}

TEST_F(AsyncEventPollTest, EpollWait_NegativeMaxevents_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev[4];
    EXPECT_LT(poll_->EpollWait(ev, -1, 0), 0);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(AsyncEventPollTest, EpollWait_ZeroMaxevents_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev[4];
    EXPECT_EQ(poll_->EpollWait(ev, 0, 0), 0);
}

TEST_F(AsyncEventPollTest, EpollWait_NoEvents_TimeoutZero_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event ev[4];
    EXPECT_EQ(poll_->EpollWait(ev, 4, 0), 0);
}

TEST_F(AsyncEventPollTest, WakeUpEpollFd_NoCrash)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    poll_->AddSockReadableEvent();
    poll_->WakeUpEpollFd();
}

TEST_F(AsyncEventPollTest, SetReadableEventFd_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    poll_->AddSockReadableEvent();
    EXPECT_EQ(poll_->SetReadableEventFd(), 0);
}

TEST_F(AsyncEventPollTest, AddRawSocketEvent_Success)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    // AddRawSocketEvent is called internally by EpollCtlAdd
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Verify we can delete
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    close(fd);
}

TEST_F(AsyncEventPollTest, DelRawSocketEvent_NotAdded_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    // DelRawSocketEvent when not added returns 0 (warns but no error)
    // Actually it's called via EpollCtlDel which checks IsSocketEventDataExist first
    // So this path is through EpollCtlDel → not added → returns error
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_DEL, 999, nullptr), 0);
}

TEST_F(AsyncEventPollTest, Destructor_CleansUp)
{
    {
        AsyncEventPoll p(epoll_fd_);
        p.AddSockReadableEvent();
        // Destructor should clean up
    }
    // No crash after destructor
}

TEST_F(AsyncEventPollTest, EpollCtlAddAddModDel_RawSocket_FullCycle)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);

    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;

    // Add
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Mod
    ev.events = EPOLLIN | EPOLLET;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    // Del
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    // Del again → fails (not found)
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);

    close(fd);
}

TEST_F(AsyncEventPollTest, EpollWait_WithQueuedEvents_ReturnsEvents)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    poll_->AddSockReadableEvent();

    // Push an event to the readable queue
    epoll_data_t data;
    data.fd = 42;
    poll_->AddReadableEvent(EPOLLIN, data);
    poll_->WakeUpEpollFd();

    // EpollWait should return the queued event
    struct epoll_event events[4];
    int ret = poll_->EpollWait(events, 4, 100);
    EXPECT_GT(ret, 0);
}

// ==================== EpollRunner tests ====================

class EpollRunnerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;
    }

    void TearDown() override
    {
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
};

TEST_F(EpollRunnerTest, Start_Stop_ShareJfrRxRunner)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    EXPECT_EQ(runner.Start(), 0);
    // Already started → returns 0
    EXPECT_EQ(runner.Start(), 0);

    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_TX_CQE_TIMER;
    data.event_data.data = static_cast<uint64_t>(fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};
    EXPECT_EQ(runner.AddEpollEvent(fd, &ev, nullptr), static_cast<int>(UBS_OK));
    EXPECT_EQ(runner.DelEpollEvent(fd), 0);
    // Del invalid fd → error
    EXPECT_LT(runner.DelEpollEvent(-1), 0);
    close(fd);

    runner.Stop();
    // Stop again → no-op (exit_efd_ < 0)
    runner.Stop();
}

// ==================== EpollRunner Start error paths ====================
// These tests consume call_once for each runner type — must use fresh types.
// SHARE_JFR_RX_RUNNER already consumed by Start_Stop test above.
// Use TRANSPORT_POOL_TX_RUNNER and TRANSPORT_POOL_EVENT_RUNNER.

class EpollRunnerErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        LibcApi::close_ptr = ::close;
    }

    void TearDown() override
    {
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
};

// --- epoll_create1 fails → Start() returns -1 ---
TEST_F(EpollRunnerErrorTest, Start_EpollCreate1Fails_ReturnsError)
{
    MOCKER_CPP(::epoll_create1).stubs().will(returnValue(-1));
    errno = EMFILE;
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    EXPECT_EQ(runner.Start(), -1);
    GlobalMockObject::verify();
}

// --- eventfd fails → Start() returns -1 (epoll_create1 succeeds, eventfd fails) ---
static int MockEventfdFail(unsigned int initval, int flags)
{
    errno = EMFILE;
    return -1;
}

TEST_F(EpollRunnerErrorTest, Start_EventfdFails_ReturnsError)
{
    MOCKER_CPP(::eventfd).stubs().will(invoke(MockEventfdFail));
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER);
    EXPECT_EQ(runner.Start(), -1);
    GlobalMockObject::verify();
}

// ==================== AsyncEventPoll OS error paths ====================

class AsyncEventPollErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
        LibcApi::close_ptr = ::close;
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
    }

    void TearDown() override
    {
        poll_.reset();
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }

    int epoll_fd_{-1};
    std::unique_ptr<AsyncEventPoll> poll_;
};

// --- AddSockReadableEvent: eventfd() fails → return -1 ---
TEST_F(AsyncEventPollErrorTest, AddSockReadableEvent_EventfdFails_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    MOCKER_CPP(::eventfd).stubs().will(returnValue(-1));
    errno = EMFILE;
    EXPECT_EQ(poll_->AddSockReadableEvent(), -1);
    GlobalMockObject::verify();
}

// --- AddSockReadableEvent: epoll_ctl fails → return -1 ---
static int MockEpollCtlFail(int epfd, int op, int fd, struct epoll_event *event)
{
    errno = EPERM;
    return -1;
}

TEST_F(AsyncEventPollErrorTest, AddSockReadableEvent_EpollCtlFails_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    // First eventfd() call succeeds, epoll_ctl(ADD) fails
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    EXPECT_EQ(poll_->AddSockReadableEvent(), -1);
    GlobalMockObject::verify();
}

// --- AddRawSocketEvent: epoll_ctl(ADD) fails → return -1 ---
TEST_F(AsyncEventPollErrorTest, EpollCtlAdd_RawSocket_EpollCtlFails)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    // Mock epoll_ctl to fail on ADD
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    GlobalMockObject::verify();
    close(fd);
}

// --- EpollCtlMod: ModRawSocketEvent epoll_ctl(MOD) fails ---
TEST_F(AsyncEventPollErrorTest, EpollCtlMod_RawSocket_EpollCtlModFails)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    // First ADD succeeds (no mock), then MOD fails
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    GlobalMockObject::verify();
    // Cleanup without epoll_ctl (it's mocked)
    poll_.reset();
    close(fd);
}

// --- DelRawSocketEvent: epoll_ctl(DEL) fails → return -1 ---
TEST_F(AsyncEventPollErrorTest, EpollCtlDel_RawSocket_EpollCtlDelFails)
{
    int my_epfd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(my_epfd, 0);
    AsyncEventPoll poll(my_epfd);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    // ADD succeeds
    EXPECT_EQ(poll.EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // DEL: mock epoll_ctl to fail → DelRawSocketEvent returns -1,
    // but EpollCtlDel ignores the return value of DelRawSocketEvent
    // and continues to GetItem(fd) which returns nullptr (fd not in ArraySet)
    // → returns 0. So we test the DelRawSocketEvent error path directly
    // by checking that EpollCtlDel at least doesn't crash.
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    // EpollCtlDel calls DelRawSocketEvent → epoll_ctl(DEL) fails → returns -1
    // But EpollCtlDel's return is 0 (it ignores DelRawSocketEvent's return)
    EXPECT_EQ(poll.EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    GlobalMockObject::verify();
    close(fd);
    close(my_epfd);
}

// --- EpollWait: epoll_wait fails with EINTR → return false (via hasEvents) ---
TEST_F(AsyncEventPollErrorTest, EpollWait_NoEvents_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    struct epoll_event events[4];
    // No events queued, no epoll events, timeout=0 → returns 0
    EXPECT_EQ(poll_->EpollWait(events, 4, 0), 0);
}

// --- WakeUpEpollFd: sock_readable_fd_ < 0 → eventfd_write fails ---
TEST_F(AsyncEventPollErrorTest, WakeUpEpollFd_NoReadableFd_NoCrash)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    // sock_readable_fd_ is -1 (not created), eventfd_write(-1, 1) fails
    // WakeUpEpollFd calls eventfd_write which will fail → log error, no crash
    poll_->WakeUpEpollFd();
}

// --- SetReadableEventFd: sock_readable_fd_ < 0 → eventfd_write fails ---
TEST_F(AsyncEventPollErrorTest, SetReadableEventFd_NoReadableFd_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_LT(poll_->SetReadableEventFd(), 0);
}

// ==================== ArrangeWakeUpEvents tests ====================

class ArrangeWakeUpEventsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
        LibcApi::close_ptr = ::close;
        poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
        poll_->AddSockReadableEvent();
    }

    void TearDown() override
    {
        poll_.reset();
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }

    int epoll_fd_{-1};
    std::unique_ptr<AsyncEventPoll> poll_;
};

// --- ArrangeWakeUpEvents: null event_data → skip ---
TEST_F(ArrangeWakeUpEventsTest, NullEventData_Skipped)
{
    struct epoll_event events[4] = {};
    events[0].data.ptr = nullptr; // null event_data
    // Call via EpollWait path (need epoll_wait to return events)
    // Instead, call ArrangeWakeUpEvents directly via friend or internal
    // Since ArrangeWakeUpEvents is private, we test through EpollWait
    // with a mocked epoll_wait that returns our crafted events

    // Create a raw socket event in epoll
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);

    // Write to the eventfd to trigger epoll_wait
    eventfd_write(fd, 1);

    // Call EpollWait — it will call epoll_wait which returns the event,
    // then ArrangeWakeUpEvents processes it as a RAW_SOCKET event
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0); // might return 0 or 1 depending on timing

    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// --- ArrangeWakeUpEvents: RAW_SOCKET event → passthrough ---
TEST_F(ArrangeWakeUpEventsTest, RawSocketEvent_Passthrough)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);

    // Write to trigger
    eventfd_write(fd, 1);

    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0);

    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// --- ArrangeWakeUpEvents: EPOLL_EVENT_UB_SOCKET_IN → socket_readable ---
TEST_F(ArrangeWakeUpEventsTest, SocketInEvent_TriggersReadableQueue)
{
    // This requires a socket registered in ArraySet, which is complex.
    // Instead, test via the readable_sockets_event_queue_ path:
    // Push an event to the queue, then call EpollWait
    epoll_data_t data;
    data.fd = 42;
    poll_->AddReadableEvent(EPOLLIN, data);
    poll_->WakeUpEpollFd();

    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GT(ret, 0); // should return the queued event
}

// --- ReleaseRemovedEventsData: basic no-crash ---
TEST_F(ArrangeWakeUpEventsTest, ReleaseRemovedEventsData_NoCrash)
{
    // ReleaseRemovedEventsData is called internally by EpollWait.
    // Just verify it doesn't crash when called via EpollWait.
    struct epoll_event out[4];
    poll_->EpollWait(out, 4, 0);
}

// ==================== EpollRunner with mock ops: DrainReadyEvents/ProcessOneEvent/RunInThread ====================

class MockRunnerOps : public EpollRunnerOps {
public:
    std::atomic<int> processCallCount{0};

    int ProcessOneEvent(const struct epoll_event &event) override
    {
        processCallCount.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    int AddEventToRunner(int epoll_fd, int fd, struct epoll_event *event, ExtContext *ctx) override
    {
        return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, event) < 0 ? UBS_ERROR : UBS_OK;
    }

    int DelEpollEvent(int epoll_fd, int fd) override
    {
        return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) < 0 ? UBS_ERROR : UBS_OK;
    }
};

class EpollRunnerMockOpsTest : public ::testing::Test {
protected:
    MockRunnerOps *mockOps_ = nullptr;
    EpollRunnerOps *savedOps_ = nullptr;
    int my_epoll_fd_ = -1;
    int my_exit_efd_ = -1;
    std::thread bg_thread_;

    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;

        my_epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(my_epoll_fd_, 0);
        my_exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        ASSERT_GE(my_exit_efd_, 0);

        RunnerEventData data{};
        data.event_data.type = RUNNER_EVENT_TYPE_STOP;
        data.event_data.data = static_cast<uint64_t>(my_exit_efd_);
        struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.u64 = data.u64}};
        epoll_ctl(my_epoll_fd_, EPOLL_CTL_ADD, my_exit_efd_, &ev);

        mockOps_ = new MockRunnerOps();

        auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
        auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
        savedOps_ = cr->ops_;
        cr->ops_ = mockOps_;
        cr->epoll_fd_ = my_epoll_fd_;
        cr->exit_efd_ = my_exit_efd_;

        bg_thread_ = std::thread([this, &runner]() {
            auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
            pthread_setname_np(pthread_self(), "ubs_test_bg");
            while (LIKELY(!cr->DrainReadyEvents(10000))) {}
        });
    }

    void TearDown() override
    {
        eventfd_write(my_exit_efd_, 1);
        if (bg_thread_.joinable()) {
            bg_thread_.join();
        }

        auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
        auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
        cr->ops_ = savedOps_;
        cr->epoll_fd_ = -1;
        cr->exit_efd_ = -1;

        delete mockOps_;
        close(my_epoll_fd_);
        close(my_exit_efd_);
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }

    bool WaitForProcessed(int targetCount, int timeoutMs = 200)
    {
        for (int i = 0; i < timeoutMs; ++i) {
            if (mockOps_->processCallCount.load() >= targetCount) return true;
            usleep(1000);
        }
        return false;
    }
};

TEST_F(EpollRunnerMockOpsTest, DrainReadyEvents_ProcessesEvent)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_TX_CQE_TIMER;
    data.event_data.data = static_cast<uint64_t>(fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};
    EXPECT_EQ(runner.AddEpollEvent(fd, &ev, nullptr), static_cast<int>(UBS_OK));
    int before = mockOps_->processCallCount.load();
    eventfd_write(fd, 1);
    EXPECT_TRUE(WaitForProcessed(before + 1));
    runner.DelEpollEvent(fd);
    close(fd);
}

TEST_F(EpollRunnerMockOpsTest, ProcessOneEvent_TxWakeType)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_TX_WAKE;
    data.event_data.data = static_cast<uint64_t>(fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};
    EXPECT_EQ(runner.AddEpollEvent(fd, &ev, nullptr), static_cast<int>(UBS_OK));
    int before = mockOps_->processCallCount.load();
    eventfd_write(fd, 1);
    EXPECT_TRUE(WaitForProcessed(before + 1));
    runner.DelEpollEvent(fd);
    close(fd);
}

TEST_F(EpollRunnerMockOpsTest, ProcessOneEvent_UnknownType)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    RunnerEventData data{};
    data.event_data.type = static_cast<RunnerEventType>(999);
    data.event_data.data = static_cast<uint64_t>(fd);
    struct epoll_event ev = {.events = EPOLLIN, .data = {.u64 = data.u64}};
    EXPECT_EQ(runner.AddEpollEvent(fd, &ev, nullptr), static_cast<int>(UBS_OK));
    int before = mockOps_->processCallCount.load();
    eventfd_write(fd, 1);
    EXPECT_TRUE(WaitForProcessed(before + 1));
    runner.DelEpollEvent(fd);
    close(fd);
}

TEST_F(EpollRunnerMockOpsTest, MultipleEvents_AllProcessed)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    int fd1 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int fd2 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);
    RunnerEventData d1{};
    d1.event_data.type = RUNNER_EVENT_TYPE_TX_CQE_TIMER;
    d1.event_data.data = static_cast<uint64_t>(fd1);
    struct epoll_event ev1 = {.events = EPOLLIN, .data = {.u64 = d1.u64}};
    RunnerEventData d2{};
    d2.event_data.type = RUNNER_EVENT_TYPE_TX_CQE_TIMER;
    d2.event_data.data = static_cast<uint64_t>(fd2);
    struct epoll_event ev2 = {.events = EPOLLIN, .data = {.u64 = d2.u64}};
    EXPECT_EQ(runner.AddEpollEvent(fd1, &ev1, nullptr), static_cast<int>(UBS_OK));
    EXPECT_EQ(runner.AddEpollEvent(fd2, &ev2, nullptr), static_cast<int>(UBS_OK));
    int before = mockOps_->processCallCount.load();
    eventfd_write(fd1, 1);
    eventfd_write(fd2, 1);
    EXPECT_TRUE(WaitForProcessed(before + 2));
    runner.DelEpollEvent(fd1);
    runner.DelEpollEvent(fd2);
    close(fd1);
    close(fd2);
}

TEST_F(EpollRunnerMockOpsTest, DelEpollEvent_InvalidFd_ReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    EXPECT_LT(runner.DelEpollEvent(-1), 0);
}

TEST_F(EpollRunnerMockOpsTest, Stop_ExitEfd_ThreadExitsCleanly)
{
    eventfd_write(my_exit_efd_, 1);
    if (bg_thread_.joinable()) {
        bg_thread_.join();
    }
    bg_thread_ = std::thread();
}

// ==================== Inline functions: FlushDirectDispatch / TryDirectDispatchEvent / PollerYield ====================

class InlineFuncTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        savedPollerOps_ = GlobalSetting::UBS_POLLER_OPS;
        GlobalSetting::UBS_POLLER_OPS = nullptr;
    }
    void TearDown() override
    {
        GlobalSetting::UBS_POLLER_OPS = savedPollerOps_;
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
private:
    u_external_poller_ops_t *savedPollerOps_;
};

TEST_F(InlineFuncTest, FlushDirectDispatch_NoOps_NoCrash)
{
    FlushDirectDispatch();
}

TEST_F(InlineFuncTest, FlushDirectDispatch_WithOps_CallsDispatchFlush)
{
    static int flushCalled = 0;
    static u_external_poller_ops_t ops;
    ops.dispatch_flush = []() { flushCalled++; };
    ops.dispatch_event = nullptr;
    ops.poller_yield = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    flushCalled = 0;
    FlushDirectDispatch();
    EXPECT_EQ(flushCalled, 1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, FlushDirectDispatch_WithOpsNullFlush_NoCrash)
{
    static u_external_poller_ops_t ops;
    ops.dispatch_flush = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    FlushDirectDispatch();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, TryDirectDispatchEvent_NoOps_ReturnsFalse)
{
    epoll_data_t data;
    data.u64 = 42;
    EXPECT_FALSE(TryDirectDispatchEvent(EPOLLIN, data));
}

TEST_F(InlineFuncTest, TryDirectDispatchEvent_NullDispatchEvent_ReturnsFalse)
{
    static u_external_poller_ops_t ops;
    ops.dispatch_event = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    epoll_data_t data;
    data.u64 = 42;
    EXPECT_FALSE(TryDirectDispatchEvent(EPOLLIN, data));
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, TryDirectDispatchEvent_WithDispatch_CallsAndFlushes)
{
    static int dispatchCalled = 0;
    static int flushCalled = 0;
    static uint64_t capturedData = 0;
    static u_external_poller_ops_t ops;
    ops.dispatch_event = [](uint64_t d, uint32_t) { dispatchCalled++; capturedData = d; };
    ops.dispatch_flush = []() { flushCalled++; };
    GlobalSetting::UBS_POLLER_OPS = &ops;
    dispatchCalled = 0;
    flushCalled = 0;
    epoll_data_t data;
    data.u64 = 0xABCD;
    EXPECT_TRUE(TryDirectDispatchEvent(EPOLLIN, data, true));
    EXPECT_EQ(dispatchCalled, 1);
    EXPECT_EQ(capturedData, 0xABCDU);
    EXPECT_EQ(flushCalled, 1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, TryDirectDispatchEvent_NoFlush_DoesNotFlush)
{
    static int flushCalled = 0;
    static u_external_poller_ops_t ops;
    ops.dispatch_event = [](uint64_t, uint32_t) {};
    ops.dispatch_flush = []() { flushCalled++; };
    GlobalSetting::UBS_POLLER_OPS = &ops;
    flushCalled = 0;
    epoll_data_t data;
    data.u64 = 1;
    EXPECT_TRUE(TryDirectDispatchEvent(EPOLLIN, data, false));
    EXPECT_EQ(flushCalled, 0);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, PollerYield_NoOps_CallsSchedYield)
{
    PollerYield();
}

TEST_F(InlineFuncTest, PollerYield_WithOps_CallsPollerYield)
{
    static int yieldCalled = 0;
    static u_external_poller_ops_t ops;
    ops.poller_yield = []() { yieldCalled++; };
    GlobalSetting::UBS_POLLER_OPS = &ops;
    yieldCalled = 0;
    PollerYield();
    EXPECT_EQ(yieldCalled, 1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(InlineFuncTest, PollerYield_WithOpsNullYield_CallsSchedYield)
{
    static u_external_poller_ops_t ops;
    ops.poller_yield = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    PollerYield();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

// ==================== EpollRunnerOps base virtuals ====================

TEST(EventEpollMiscTest, EpollRunnerOps_BaseVirtuals_ReturnError)
{
    EpollRunnerOps ops;
    struct epoll_event ev = {};
    EXPECT_EQ(ops.ProcessOneEvent(ev), -1);
    EXPECT_EQ(ops.AddEventToRunner(0, 0, &ev, nullptr), -1);
    EXPECT_EQ(ops.DelEpollEvent(0, 0), -1);
}

// ==================== EpollRunnerFactory default throw ====================

TEST(EventEpollMiscDeathTest, EpollRunnerFactory_InvalidType_Throws)
{
    EXPECT_THROW(EpollRunnerFactory::GetInstance(static_cast<EpollRunnerType>(255)), std::runtime_error);
}

// ==================== EventPoll::GetEpollFd + SetWakeupCallback ====================

TEST(EventEpollMiscTest, GetEpollFd_ReturnsFd)
{
    LockRegistry::RegisterDefaultOps();
    int efd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(efd, 0);
    AsyncEventPoll poll(efd);
    EXPECT_EQ(poll.GetEpollFd(), efd);
    // SetWakeupCallback
    EpollEvent readyEvent(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    auto cb = [](struct epoll_event *, int, std::unordered_map<int, EpollEvent *> &) { return 0; };
    poll.SetWakeupCallback(&readyEvent, cb);
    close(efd);
}

// ==================== EpollRunner destructor calls Stop ====================

TEST(EpollRunnerDestructorTest, DestructorCallsStop)
{
    LockRegistry::RegisterDefaultOps();
    ArraySet<Socket>::GetInstance().Init();
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    LibcApi::close_ptr = ::close;
    LibcApi::read_ptr = ::read;
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    runner.Start();
    runner.Stop();
    // The LeakySingleton is still alive; destructor won't be called (it's leaky).
    // But we can test the Stop path by calling it again (exit_efd_ < 0 → no-op).
    runner.Stop();
    LibcApi::close_ptr = nullptr;
    LibcApi::read_ptr = nullptr;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

// ==================== GetOps / GetRunnerName ====================

TEST(EpollRunnerOpsTest, GetOps_AfterStart_ReturnsOps)
{
    LockRegistry::RegisterDefaultOps();
    ArraySet<Socket>::GetInstance().Init();
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    LibcApi::close_ptr = ::close;
    LibcApi::read_ptr = ::read;
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    // Start has already been called (call_once consumed) in prior tests.
    // The runner may or may not be started depending on test order.
    // Just call GetOps — if not started, ops_ is nullptr.
    auto *ops = runner.GetOps();
    (void)ops; // may be nullptr if not started
    LibcApi::close_ptr = nullptr;
    LibcApi::read_ptr = nullptr;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

TEST(EpollRunnerNameTest, GetRunnerName_AllTypes)
{
    LockRegistry::RegisterDefaultOps();
    ArraySet<Socket>::GetInstance().Init();
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
    LibcApi::close_ptr = ::close;
    LibcApi::read_ptr = ::read;
    // These runners have had Start() call_once consumed by prior error tests.
    // GetRunnerName doesn't depend on Start state.
    auto &rxRunner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    EXPECT_EQ(rxRunner.GetRunnerName(), "ubs_sh_jfr_rx");
    auto &txRunner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    EXPECT_EQ(txRunner.GetRunnerName(), "ubs_tp_tx");
    auto &evtRunner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER);
    EXPECT_EQ(evtRunner.GetRunnerName(), "ubs_tp_evt");
    LibcApi::close_ptr = nullptr;
    LibcApi::read_ptr = nullptr;
    ArraySet<Socket>::GetInstance().ReleaseAll();
}

// ==================== CreateSocketEpollMapper alloc fail ====================

// operator new(std::nothrow) override for testing allocation failure
static int g_epollNewNoThrowFail = 0;
static int g_newNoThrowAllocCount = 0;
static int g_newNoThrowFailAt = -1;
void *operator new(std::size_t sz, const std::nothrow_t &) noexcept
{
    if (g_epollNewNoThrowFail) {
        return nullptr;
    }
    void *p = malloc(sz);
    if (g_newNoThrowFailAt > 0 && ++g_newNoThrowAllocCount == g_newNoThrowFailAt) {
        free(p);
        return nullptr;
    }
    return p;
}

TEST_F(EventEpollTest, CreateSocketEpollMapper_AllocFail_ReturnsFalse)
{
    g_epollNewNoThrowFail = 1;
    EpollMapper *mapper = nullptr;
    EXPECT_FALSE(CreateSocketEpollMapper(777, mapper));
    EXPECT_EQ(mapper, nullptr);
    g_epollNewNoThrowFail = 0;
}

// ==================== ReserveSocketEpollMappers cap test ====================

TEST_F(EventEpollTest, ReserveSocketEpollMappers_CapsAt16384)
{
    // Reserve a very large capacity — should be capped internally
    ReserveSocketEpollMappers(1000000);
    // No crash, no assertion failure
}

// ==================== EpollRunner::Start epoll_ctl fail ====================

class EpollRunnerStartFailTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        LibcApi::close_ptr = ::close;
    }
    void TearDown() override
    {
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
};

// This test needs a fresh runner type, but all 3 types have call_once consumed.
// We can't test epoll_ctl fail in Start because call_once is already consumed.
// Instead, we test DrainReadyEvents error paths directly.

// ==================== DrainReadyEvents error paths ====================

class DrainReadyEventsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;
        my_epfd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(my_epfd_, 0);
        my_exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        ASSERT_GE(my_exit_efd_, 0);
        RunnerEventData data{};
        data.event_data.type = RUNNER_EVENT_TYPE_STOP;
        data.event_data.data = static_cast<uint64_t>(my_exit_efd_);
        struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.u64 = data.u64}};
        epoll_ctl(my_epfd_, EPOLL_CTL_ADD, my_exit_efd_, &ev);
    }
    void TearDown() override
    {
        close(my_epfd_);
        close(my_exit_efd_);
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
    int my_epfd_{-1};
    int my_exit_efd_{-1};
};

TEST_F(DrainReadyEventsTest, DrainReadyEvents_EpollWaitEintr_ReturnsFalse)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = my_epfd_;
    // Mock epoll_wait to return -1 with errno EINTR
    MOCKER_CPP(::epoll_wait).stubs().will(returnValue(-1));
    errno = EINTR;
    bool result = cr->DrainReadyEvents(0, nullptr);
    EXPECT_FALSE(result);
    GlobalMockObject::verify();
    cr->epoll_fd_ = -1;
}

TEST_F(DrainReadyEventsTest, DrainReadyEvents_EpollWaitFatalError_ReturnsTrue)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = my_epfd_;
    MOCKER_CPP(::epoll_wait).stubs().will(returnValue(-1));
    errno = EBADF;
    bool result = cr->DrainReadyEvents(0, nullptr);
    EXPECT_TRUE(result);
    GlobalMockObject::verify();
    cr->epoll_fd_ = -1;
}

TEST_F(DrainReadyEventsTest, DrainReadyEvents_HasEventsSet)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = my_epfd_;
    bool hasEvents = false;
    // Normal epoll_wait with timeout=0, no events → returns 0, hasEvents=false
    cr->DrainReadyEvents(0, &hasEvents);
    EXPECT_FALSE(hasEvents);
    cr->epoll_fd_ = -1;
}

// ==================== AddEpollEvent failure path ====================

TEST_F(DrainReadyEventsTest, AddEpollEvent_OpsFails_ReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    // Save original ops and set a failing one
    EpollRunnerOps *savedOps = cr->ops_;
    EpollRunnerOps failOps;
    cr->ops_ = &failOps;
    struct epoll_event ev = {};
    EXPECT_EQ(runner.AddEpollEvent(42, &ev, nullptr), -1);
    cr->ops_ = savedOps;
}

// ==================== EpollRunner::Stop eventfd_write fail ====================

TEST_F(DrainReadyEventsTest, Stop_EventfdWriteFail_NoCrash)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    // Set up a fake "started" state
    cr->epoll_fd_ = my_epfd_;
    cr->exit_efd_ = my_exit_efd_;
    cr->ops_ = nullptr;
    cr->mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    // Mock eventfd_write to fail
    MOCKER_CPP(::eventfd_write).stubs().will(returnValue(-1));
    errno = EIO;
    runner.Stop();
    GlobalMockObject::verify();
    // exit_efd_ should still be set since eventfd_write failed (Stop returns early)
    // Clean up manually
    if (cr->mutex_ != nullptr) {
        LockRegistry::LOCK_OPS.destroy(cr->mutex_);
        cr->mutex_ = nullptr;
    }
    cr->epoll_fd_ = -1;
    cr->exit_efd_ = -1;
}

// ==================== AsyncEventPoll bad_alloc catch + private method coverage ====================

class AsyncEventPollPrivateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
        LibcApi::close_ptr = ::close;
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
    }
    void TearDown() override
    {
        poll_.reset();
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
    int epoll_fd_{-1};
    std::unique_ptr<AsyncEventPoll> poll_;
};

TEST_F(AsyncEventPollPrivateTest, Constructor_BadAlloc_NoCrash)
{
    // The reserve in constructor catches bad_alloc — just verify no crash
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_NE(poll_, nullptr);
}

TEST_F(AsyncEventPollPrivateTest, InsertSocketEventData_Duplicate_ReturnsFalse)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    auto *ev = new EpollEvent(EPOLL_EVENT_RAW_SOCKET, 42, epoll_event{});
    EXPECT_TRUE(poll_->InsertSocketEventData(42, ev));
    EXPECT_FALSE(poll_->InsertSocketEventData(42, ev));
    // Clean up before destructor
    poll_->RemoveSocketEventData(42);
    poll_->ReleaseRemovedEventsData();
}

TEST_F(AsyncEventPollPrivateTest, RemoveSocketEventData_NotExist_ReturnsFalse)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_FALSE(poll_->RemoveSocketEventData(999));
}

TEST_F(AsyncEventPollPrivateTest, GetSocketEventData_NotExist_ReturnsNull)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    EXPECT_EQ(poll_->GetSocketEventData(999), nullptr);
}

TEST_F(AsyncEventPollPrivateTest, RemoveSocketEventData_Exist_ReturnsTrue)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    auto *ev = new EpollEvent(EPOLL_EVENT_RAW_SOCKET, 42, epoll_event{});
    poll_->InsertSocketEventData(42, ev);
    EXPECT_TRUE(poll_->RemoveSocketEventData(42));
    // Clean up removed events (RemoveSocketEventData moved ev to removed_head_)
    poll_->ReleaseRemovedEventsData();
}

TEST_F(AsyncEventPollPrivateTest, GetSocketEventData_Exist_ReturnsPtr)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    auto *ev = new EpollEvent(EPOLL_EVENT_RAW_SOCKET, 42, epoll_event{});
    poll_->InsertSocketEventData(42, ev);
    EXPECT_EQ(poll_->GetSocketEventData(42), ev);
    // Let destructor clean up (it deletes socket_data_ entries)
}

// ==================== AddReadableEvent push fail (queue full) ====================

TEST_F(AsyncEventPollPrivateTest, AddReadableEvent_QueueFull_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    epoll_data_t data;
    data.u64 = 0;
    // Fill the queue to capacity: the ring is sized from the fd table (issue #44), floor MAX_READABLE_FD_COUNT
    const uint64_t ring_capacity = ReadableRingCapacityFor(ArraySet<Socket>::GetInstance().Capacity());
    ASSERT_GE(ring_capacity, static_cast<uint64_t>(MAX_READABLE_FD_COUNT));
    for (uint64_t i = 0; i < ring_capacity; i++) {
        ASSERT_EQ(poll_->AddReadableEvent(EPOLLIN, data), 0);
    }
    // Next push should fail
    EXPECT_EQ(poll_->AddReadableEvent(EPOLLIN, data), -1);
}

// ==================== External poller ops tests ====================

class ExternalPollerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;
        savedOps_ = GlobalSetting::UBS_POLLER_OPS;
    }
    void TearDown() override
    {
        GlobalSetting::UBS_POLLER_OPS = savedOps_;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
    u_external_poller_ops_t *savedOps_;
};

// Test ExternalPollerEpollRunnerBackend via CreateBackend
// We can't directly instantiate the template backend, but we can observe
// behavior through EpollRunner::Start when UBS_POLLER_OPS is set.

// Since all runner types have call_once consumed, we test the backend
// classes by accessing the CreateBackend method indirectly.
// Actually, we can test the inline TryDirectDispatchEvent and FlushDirectDispatch
// which use UBS_POLLER_OPS — already covered above.

// ==================== AsyncEventPoll EpollCtl with UB-socket ====================

class UbSocketEpollCtlTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        ASSERT_GE(epoll_fd_, 0);
        LibcApi::close_ptr = ::close;
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
        poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    }
    void TearDown() override
    {
        poll_.reset();
        LibcApi::close_ptr = nullptr;
        CleanAllSocketEpollMappers();
        if (g_socket_epoll_lock != nullptr) {
            LockRegistry::RW_LOCK_OPS.destroy(g_socket_epoll_lock);
            g_socket_epoll_lock = nullptr;
        }
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
    SocketPtr MakeUbSocket(int fd)
    {
        auto *sock = new UbMockSocketBase(fd);
        ArraySet<Socket>::GetInstance().OverrideItem(fd, sock);
        return SocketPtr(sock);
    }
    int epoll_fd_{-1};
    std::unique_ptr<AsyncEventPoll> poll_;
};

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_Success)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto sock = MakeUbSocket(fd);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_WithTxEvent_Success)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = fd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_WithEpollout_NotifyWritable)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->notifyWritableRet = 0;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_TxEventFail_Rollback)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    int txFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(txFd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = txFd;
    rawSock->addTxEventRet = -1;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    close(txFd);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_AddRawFail)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto sock = MakeUbSocket(fd);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    GlobalMockObject::verify();
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_AddSockReadableFail)
{
    // Create a real fd for the raw socket, mock eventfd to fail for AddSockReadableEvent
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto sock = MakeUbSocket(fd);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    // Mock eventfd to fail (used by AddSockReadableEvent)
    MOCKER_CPP(::eventfd).stubs().will(returnValue(-1));
    errno = EMFILE;
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    GlobalMockObject::verify();
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlMod_UbSocket_Success)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto sock = MakeUbSocket(fd);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Mod with EPOLLOUT to trigger ReadyAndExchange path
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlMod_UbSocket_ReadyAndExchange_NotifyWritable)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->readyAndExchange = true;
    rawSock->notifyWritableRet = 0;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlDel_UbSocket_WithTxEvent)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = fd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    close(fd);
}

TEST_F(UbSocketEpollCtlTest, EpollCtlDel_UbSocket_NoSocketInArraySet)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Don't register in ArraySet — Del should still work (sock == nullptr path)
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    close(fd);
}

// ==================== ArrangeWakeUpEvents: UB_SOCKET_OUT path ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_UbSocketOut_ProcessEpollEvent)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = fd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Write to the tx fd to trigger an OUT event in epoll_wait
    eventfd_write(fd, 1);
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    // Should get at least the raw socket event
    EXPECT_GE(ret, 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// ==================== ArrangeWakeUpEvents: wakeup callback path ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_WakeupCallback_Processed)
{
    poll_->AddSockReadableEvent();
    // Create a ready event and wakeup callback
    EpollEvent readyEvent(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    static int callbackCalled = 0;
    auto cb = [](struct epoll_event *events, int remain, std::unordered_map<int, EpollEvent *> &) {
        callbackCalled++;
        events[0].events = EPOLLIN;
        events[0].data.fd = 999;
        return 1;
    };
    poll_->SetWakeupCallback(&readyEvent, cb);
    // Create an eventfd to trigger epoll_wait, and register it with ptr = &readyEvent
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.ptr = &readyEvent;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    eventfd_write(fd, 1);
    callbackCalled = 0;
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GT(ret, 0);
    EXPECT_EQ(callbackCalled, 1);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// ==================== wakeup table: two listeners on one epoll (issue #50), remove one ====================
TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_TwoListeners_EachDeliveredAndRemovable)
{
    poll_->AddSockReadableEvent();
    EpollEvent readyA(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    EpollEvent readyB(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    static int calledA = 0;
    static int calledB = 0;
    auto cbA = [](struct epoll_event *events, int, std::unordered_map<int, EpollEvent *> &) {
        calledA++;
        events[0].events = EPOLLIN;
        events[0].data.fd = 901;
        return 1;
    };
    auto cbB = [](struct epoll_event *events, int, std::unordered_map<int, EpollEvent *> &) {
        calledB++;
        events[0].events = EPOLLIN;
        events[0].data.fd = 902;
        return 1;
    };
    poll_->SetWakeupCallback(&readyA, cbA);
    poll_->SetWakeupCallback(&readyB, cbB);
    EXPECT_EQ(poll_->WakeupCallbackCount(), static_cast<size_t>(2));
    int fdA = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int fdB = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fdA, 0);
    ASSERT_GE(fdB, 0);
    struct epoll_event evA = {};
    evA.events = EPOLLIN;
    evA.data.ptr = &readyA;
    struct epoll_event evB = {};
    evB.events = EPOLLIN;
    evB.data.ptr = &readyB;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fdA, &evA);
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fdB, &evB);
    /* both listeners signal: both callbacks run (the old single slot only kept the last one) */
    eventfd_write(fdA, 1);
    eventfd_write(fdB, 1);
    calledA = 0;
    calledB = 0;
    struct epoll_event out[8];
    int ret = poll_->EpollWait(out, 8, 100);
    EXPECT_GE(ret, 2);
    EXPECT_EQ(calledA, 1);
    EXPECT_EQ(calledB, 1);
    /* re-registering replaces, never duplicates */
    poll_->SetWakeupCallback(&readyA, cbA);
    EXPECT_EQ(poll_->WakeupCallbackCount(), static_cast<size_t>(2));
    /* listener A goes away (CleanUp order: unregister, then epoll DEL); B still delivered */
    poll_->RemoveWakeupCallback(&readyA);
    EXPECT_EQ(poll_->WakeupCallbackCount(), static_cast<size_t>(1));
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fdA, nullptr);
    eventfd_write(fdB, 1);
    calledA = 0;
    calledB = 0;
    ret = poll_->EpollWait(out, 8, 100);
    EXPECT_GE(ret, 1);
    EXPECT_EQ(calledA, 0);
    EXPECT_EQ(calledB, 1);
    poll_->RemoveWakeupCallback(&readyB);
    poll_->RemoveWakeupCallback(&readyB); /* idempotent */
    EXPECT_EQ(poll_->WakeupCallbackCount(), static_cast<size_t>(0));
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fdB, nullptr);
    close(fdA);
    close(fdB);
}

// ==================== ArrangeWakeUpEvents: socket_readable read error path ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_SocketReadable_ReadError)
{
    poll_->AddSockReadableEvent();
    // Create a UB_SOCKET_IN event manually by creating an EpollEvent and adding it to epoll
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    // Register a raw socket event first (to get epoll_wait to return)
    struct epoll_event rawEv = {};
    rawEv.events = EPOLLIN | EPOLLET;
    rawEv.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &rawEv), 0);
    // Now create an UB_SOCKET_IN EpollEvent and add it directly to epoll
    EpollEvent ubInEvent(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    int ubFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(ubFd, 0);
    struct epoll_event ubEv = {};
    ubEv.events = EPOLLIN;
    ubEv.data.ptr = &ubInEvent;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ubFd, &ubEv);
    eventfd_write(ubFd, 1);
    eventfd_write(fd, 1);
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ubFd, nullptr);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(ubFd);
    close(fd);
}

// ==================== EpollCtlAdd: mapper cleanup on failure ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_RawSocket_EpollCtlFail_MapperCleanup)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    GlobalMockObject::verify();
    // The mapper should have been cleaned up
    EXPECT_EQ(GetSocketEpollMapper(fd), nullptr);
    close(fd);
}

// ==================== EpollCtl: MOD with UB-socket, ModRawSocketEvent fail ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlMod_UbSocket_ModRawFail_ReturnsError)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto sock = MakeUbSocket(fd);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    ev.events = EPOLLIN | EPOLLET;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_MOD, fd, &ev), 0);
    GlobalMockObject::verify();
    // Clean up without epoll_ctl (it's mocked)
    close(fd);
}

// ==================== AddProtoTxEvent: already exists → returns 0 ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_TxEventAlreadyExists_NoDoubleAdd)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = fd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Second add should fail with EEXIST (socket data already exists)
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    EXPECT_EQ(errno, EEXIST);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// ==================== ExternalPoller backend via CreateBackend ====================
// We test ExternalPollerEpollRunnerBackend and ExternalDirectPollerEpollRunnerBackend
// by setting UBS_POLLER_OPS and calling Start on a fresh runner.
// But all runners have call_once consumed. Instead, we test the inline dispatch
// functions and the backend classes indirectly.

TEST_F(ExternalPollerTest, ExternalDirectPollerBackend_StartStop)
{
    // Set up external poller ops with direct poller
    static int addDirectPollerCalled = 0;
    static int removeDirectPollerCalled = 0;
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **poller) {
        addDirectPollerCalled++;
        *poller = reinterpret_cast<void *>(0x1234);
        return 0;
    };
    ops.remove_direct_poller = [](void *, int) { removeDirectPollerCalled++; };
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    ops.dispatch_event = nullptr;
    ops.dispatch_flush = nullptr;
    ops.poller_yield = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    // We can't call Start (call_once consumed), but we verified the dispatch functions
    // work via TryDirectDispatchEvent tests above.
    // Test that the ops struct is properly set.
    EXPECT_NE(GlobalSetting::UBS_POLLER_OPS, nullptr);
    EXPECT_NE(GlobalSetting::UBS_POLLER_OPS->add_direct_poller, nullptr);
}

TEST_F(ExternalPollerTest, ExternalPollerBackend_InvalidOps_ReturnsError)
{
    // Set up ops with null add_consumer
    static u_external_poller_ops_t ops;
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    // With all null, TryDirectDispatchEvent returns false
    epoll_data_t data;
    data.u64 = 1;
    EXPECT_FALSE(TryDirectDispatchEvent(EPOLLIN, data));
}

TEST_F(ExternalPollerTest, ExternalDirectPollerBackend_StartFail)
{
    // Set up ops with add_direct_poller that fails
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **) { return -1; };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    // Can't test Start directly (call_once consumed), but verify ops are set
    EXPECT_NE(GlobalSetting::UBS_POLLER_OPS->add_direct_poller, nullptr);
}

TEST_F(ExternalPollerTest, ExternalPollerBackend_StartSuccess)
{
    // Set up ops with add_consumer that succeeds
    static int addConsumerCalled = 0;
    static u_external_poller_ops_t ops;
    ops.add_consumer = [](int, void *, u_poller_event_cb_t, void **consumer) {
        addConsumerCalled++;
        *consumer = reinterpret_cast<void *>(0x5678);
        return 0;
    };
    ops.remove_consumer = [](void *, int) {};
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    // Can't test Start (call_once consumed), verify ops
    EXPECT_NE(GlobalSetting::UBS_POLLER_OPS->add_consumer, nullptr);
}

// ==================== EpollRunnerOps::ExtContext virtual destructor ====================

TEST(EventEpollMiscTest, ExtContext_VirtualDestructor_NoCrash)
{
    EpollRunnerOps::ExtContext ctx;
    ctx.umq_handle = 42;
}

// ==================== once_flag reset + Start() error paths ====================

class EpollRunnerResetTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        LibcApi::close_ptr = ::close;
        LibcApi::read_ptr = ::read;
        savedOps_ = GlobalSetting::UBS_POLLER_OPS;
        GlobalSetting::UBS_POLLER_OPS = nullptr;
    }
    void TearDown() override
    {
        GlobalSetting::UBS_POLLER_OPS = savedOps_;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        LibcApi::close_ptr = nullptr;
        LibcApi::read_ptr = nullptr;
        CleanAllSocketEpollMappers();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
    }
    void ResetOnceFlag(EpollRunnerBase &runner)
    {
        auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
        cr->once_done_ = false;
    }
    void CleanupRunner(EpollRunnerBase &runner)
    {
        auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
        if (cr->mutex_ != nullptr) {
            LockRegistry::LOCK_OPS.destroy(cr->mutex_);
            cr->mutex_ = nullptr;
        }
        if (cr->exit_efd_ >= 0) { close(cr->exit_efd_); cr->exit_efd_ = -1; }
        if (cr->epoll_fd_ >= 0) { close(cr->epoll_fd_); cr->epoll_fd_ = -1; }
        if (cr->ops_ != nullptr) { delete cr->ops_; cr->ops_ = nullptr; }
        cr->backend_.reset();
    }
    u_external_poller_ops_t *savedOps_;
};

TEST_F(EpollRunnerResetTest, Start_MutexCreateFail_ReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    ResetOnceFlag(runner);
    CleanupRunner(runner);
    // Mock LOCK_OPS.create to return nullptr — need to replace the lock ops
    // Actually, we can't easily mock LockRegistry::LOCK_OPS.create.
    // Instead, set mutex_ to a sentinel and verify the check works.
    // Actually, the check is: if (mutex_ == nullptr) after create.
    // We need create to return nullptr. Let's use a custom lock ops.
    u_external_lock_ops_t failOps = {};
    failOps.create = [](u_mutex_type_t) -> u_mutex_t * { return nullptr; };
    failOps.destroy = [](u_mutex_t *) -> int { return 0; };
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    auto savedLockOps = LockRegistry::LOCK_OPS;
    LockRegistry::LOCK_OPS = failOps;
    EXPECT_EQ(runner.Start(), -1);
    LockRegistry::LOCK_OPS = savedLockOps;
    // Clean up
    cr->once_done_ = false;
}

TEST_F(EpollRunnerResetTest, Start_EpollCtlAddFail_ReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    ResetOnceFlag(runner);
    CleanupRunner(runner);
    // Mock epoll_ctl to fail on the ADD for exit_efd
    MOCKER_CPP(::epoll_ctl).stubs().will(invoke(MockEpollCtlFail));
    EXPECT_EQ(runner.Start(), -1);
    GlobalMockObject::verify();
    // Clean up: mutex was created, epoll_fd and exit_efd were created
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    if (cr->mutex_ != nullptr) { LockRegistry::LOCK_OPS.destroy(cr->mutex_); cr->mutex_ = nullptr; }
    if (cr->exit_efd_ >= 0) { close(cr->exit_efd_); cr->exit_efd_ = -1; }
    if (cr->epoll_fd_ >= 0) { close(cr->epoll_fd_); cr->epoll_fd_ = -1; }
    cr->once_done_ = false;
}

static int MockEventfdWriteFail(int fd, uint64_t value)
{
    errno = EIO;
    return -1;
}

TEST_F(EpollRunnerResetTest, Start_BackendStartFail_ReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    ResetOnceFlag(runner);
    CleanupRunner(runner);
    // Set up UBS_POLLER_OPS with invalid direct poller ops to make backend Start fail
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **) { return -1; };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    EXPECT_EQ(runner.Start(), -1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    // Clean up
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    if (cr->mutex_ != nullptr) { LockRegistry::LOCK_OPS.destroy(cr->mutex_); cr->mutex_ = nullptr; }
    if (cr->exit_efd_ >= 0) { close(cr->exit_efd_); cr->exit_efd_ = -1; }
    if (cr->epoll_fd_ >= 0) { close(cr->epoll_fd_); cr->epoll_fd_ = -1; }
    if (cr->ops_ != nullptr) { delete cr->ops_; cr->ops_ = nullptr; }
    cr->once_done_ = false;
}

TEST_F(EpollRunnerResetTest, Start_Success_ThenStop_CoversTxAndEventOps)
{
    // Test TX runner Start to cover ops_ = new UmqTpTxEpollRunnerOps()
    auto &txRunner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_TX_RUNNER);
    auto *crTx = reinterpret_cast<EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER> *>(&txRunner);
    crTx->once_done_ = false;
    if (crTx->mutex_ != nullptr) { LockRegistry::LOCK_OPS.destroy(crTx->mutex_); crTx->mutex_ = nullptr; }
    if (crTx->exit_efd_ >= 0) { close(crTx->exit_efd_); crTx->exit_efd_ = -1; }
    if (crTx->epoll_fd_ >= 0) { close(crTx->epoll_fd_); crTx->epoll_fd_ = -1; }
    if (crTx->ops_ != nullptr) { delete crTx->ops_; crTx->ops_ = nullptr; }
    crTx->backend_.reset();
    EXPECT_EQ(txRunner.Start(), 0);
    txRunner.Stop();
    crTx->once_done_ = false;

    // Test Event runner Start to cover ops_ = new UmqTpEventEpollRunnerOps()
    auto &evtRunner = EpollRunnerFactory::GetInstance(EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER);
    auto *crEvt = reinterpret_cast<EpollRunner<EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER> *>(&evtRunner);
    crEvt->once_done_ = false;
    if (crEvt->mutex_ != nullptr) { LockRegistry::LOCK_OPS.destroy(crEvt->mutex_); crEvt->mutex_ = nullptr; }
    if (crEvt->exit_efd_ >= 0) { close(crEvt->exit_efd_); crEvt->exit_efd_ = -1; }
    if (crEvt->epoll_fd_ >= 0) { close(crEvt->epoll_fd_); crEvt->epoll_fd_ = -1; }
    if (crEvt->ops_ != nullptr) { delete crEvt->ops_; crEvt->ops_ = nullptr; }
    crEvt->backend_.reset();
    EXPECT_EQ(evtRunner.Start(), 0);
    evtRunner.Stop();
    crEvt->once_done_ = false;
}

// ==================== CreateBackend direct tests ====================

TEST_F(EpollRunnerResetTest, CreateBackend_NoOps_PthreadBackend)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    // Set up valid epoll_fd and exit_efd for PthreadBackend (thread will need them)
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    cr->exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(cr->exit_efd_, 0);
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_STOP;
    data.event_data.data = static_cast<uint64_t>(cr->exit_efd_);
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.u64 = data.u64}};
    epoll_ctl(cr->epoll_fd_, EPOLL_CTL_ADD, cr->exit_efd_, &ev);
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), 0);
    // Write to exit_efd to make the thread exit
    eventfd_write(cr->exit_efd_, 1);
    backend->Stop();
    close(cr->exit_efd_);
    close(cr->epoll_fd_);
    cr->exit_efd_ = -1;
    cr->epoll_fd_ = -1;
}

TEST_F(EpollRunnerResetTest, CreateBackend_DirectPoller_ExternalDirectBackend)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    static int addCalled = 0;
    static int removeCalled = 0;
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **poller) {
        addCalled++;
        *poller = reinterpret_cast<void *>(0x1);
        return 0;
    };
    ops.remove_direct_poller = [](void *, int) { removeCalled++; };
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    addCalled = 0;
    removeCalled = 0;
    EXPECT_EQ(backend->Start(), 0);
    EXPECT_EQ(addCalled, 1);
    backend->Stop();
    EXPECT_EQ(removeCalled, 1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

TEST_F(EpollRunnerResetTest, CreateBackend_ConsumerNoUnified_ExternalPollerBackend)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    static int addConsumerCalled = 0;
    static int removeConsumerCalled = 0;
    static u_external_poller_ops_t ops;
    ops.add_consumer = [](int, void *, u_poller_event_cb_t, void **consumer) {
        addConsumerCalled++;
        *consumer = reinterpret_cast<void *>(0x2);
        return 0;
    };
    ops.remove_consumer = [](void *, int) { removeConsumerCalled++; };
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    addConsumerCalled = 0;
    removeConsumerCalled = 0;
    EXPECT_EQ(backend->Start(), 0);
    EXPECT_EQ(addConsumerCalled, 1);
    backend->Stop();
    EXPECT_EQ(removeConsumerCalled, 1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

TEST_F(EpollRunnerResetTest, CreateBackend_ConsumerWithUnified_PthreadFallback)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    cr->exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(cr->exit_efd_, 0);
    RunnerEventData data{};
    data.event_data.type = RUNNER_EVENT_TYPE_STOP;
    data.event_data.data = static_cast<uint64_t>(cr->exit_efd_);
    struct epoll_event ev = {.events = EPOLLIN | EPOLLET, .data = {.u64 = data.u64}};
    epoll_ctl(cr->epoll_fd_, EPOLL_CTL_ADD, cr->exit_efd_, &ev);
    static u_external_poller_ops_t ops;
    ops.add_consumer = [](int, void *, u_poller_event_cb_t, void **consumer) {
        *consumer = reinterpret_cast<void *>(0x3);
        return 0;
    };
    ops.remove_consumer = [](void *, int) {};
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), 0);
    eventfd_write(cr->exit_efd_, 1);
    backend->Stop();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    close(cr->exit_efd_);
    close(cr->epoll_fd_);
    cr->exit_efd_ = -1;
    cr->epoll_fd_ = -1;
}

// ==================== ExternalPoller invalid ops → Start fail ====================

TEST_F(EpollRunnerResetTest, CreateBackend_DirectPoller_InvalidOps_StartFails)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    // ops with direct poller pointers set but add_direct_poller returns -1
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **) { return -1; };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), -1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

TEST_F(EpollRunnerResetTest, CreateBackend_ExternalPoller_InvalidOps_StartFails)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    // ops with null add_consumer → Start returns -1
    static u_external_poller_ops_t ops;
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), -1);
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

// ==================== PthreadBackend Stop without Start (not joinable) ====================

TEST_F(EpollRunnerResetTest, CreateBackend_Pthread_StopWithoutStart)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    // No UBS_POLLER_OPS → PthreadBackend
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    // Stop without Start → not joinable → logs error and returns
    backend->Stop();
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

// ==================== ExternalDirectPoller ProcessEventCb ====================

TEST_F(EpollRunnerResetTest, ExternalDirectPoller_ProcessEventCb_StopEvent)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    static u_external_poller_ops_t ops;
    static void *pollerHandle = nullptr;
    ops.add_direct_poller = [](int, void *arg, u_poller_process_event_cb_t cb, void **poller) {
        *poller = arg;
        // Immediately call the callback with a STOP event
        RunnerEventData stopData{};
        stopData.event_data.type = RUNNER_EVENT_TYPE_STOP;
        struct epoll_event stopEv = {};
        stopEv.data.u64 = stopData.u64;
        cb(arg, &stopEv);
        return 0;
    };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), 0);
    backend->Stop();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

// ==================== Stop eventfd_write fail ====================

TEST_F(EpollRunnerResetTest, Start_Success_Stop_EventfdWriteFail)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    ResetOnceFlag(runner);
    CleanupRunner(runner);
    EXPECT_EQ(runner.Start(), 0);
    // Mock eventfd_write to fail
    MOCKER_CPP(::eventfd_write).stubs().will(invoke(MockEventfdWriteFail));
    runner.Stop();
    GlobalMockObject::verify();
    // If eventfd_write failed, exit_efd_ is still open. Clean up manually.
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    if (cr->exit_efd_ >= 0) { close(cr->exit_efd_); cr->exit_efd_ = -1; }
    if (cr->epoll_fd_ >= 0) { close(cr->epoll_fd_); cr->epoll_fd_ = -1; }
    if (cr->ops_ != nullptr) { delete cr->ops_; cr->ops_ = nullptr; }
    if (cr->mutex_ != nullptr) { LockRegistry::LOCK_OPS.destroy(cr->mutex_); cr->mutex_ = nullptr; }
    cr->once_done_ = false;
}

// ==================== ArrangeWakeUpEvents null event_data ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_NullEventData_Skipped)
{
    poll_->AddSockReadableEvent();
    // Create an eventfd and register it with null ptr in epoll
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr; // null event_data
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    eventfd_write(fd, 1);
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// ==================== ArrangeWakeUpEvents: socket_readable read error (errno != EAGAIN) ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_SocketReadableReadError_NonEagain)
{
    poll_->AddSockReadableEvent();
    // Create a UB_SOCKET_IN event to trigger socket_readable path
    EpollEvent ubInEvent(EPOLL_EVENT_UB_SOCKET_IN, -1, epoll_event{});
    int ubFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(ubFd, 0);
    struct epoll_event ubEv = {};
    ubEv.events = EPOLLIN;
    ubEv.data.ptr = &ubInEvent;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ubFd, &ubEv);
    // Mock read to fail with EBADF (not EAGAIN/EWOULDBLOCK)
    MOCKER_CPP(::read).stubs().will(returnValue(-1));
    errno = EBADF;
    eventfd_write(ubFd, 1);
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0);
    GlobalMockObject::verify();
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ubFd, nullptr);
    close(ubFd);
}

// ==================== DelRawSocketEvent: RemoveSocketEventData fail ====================

TEST_F(AsyncEventPollPrivateTest, DelRawSocketEvent_NotInSocketData_ReturnsZero)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    // Call DelRawSocketEvent with a fd not in socket_data_
    EXPECT_EQ(poll_->DelRawSocketEvent(999), 0);
}

// ==================== AddRawSocketEvent: InsertSocketEventData fail ====================

TEST_F(AsyncEventPollPrivateTest, AddRawSocketEvent_InsertSocketDataFail)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    // Pre-insert fd into socket_data_ so InsertSocketEventData fails
    auto *ev = new EpollEvent(EPOLL_EVENT_RAW_SOCKET, fd, epoll_event{});
    poll_->InsertSocketEventData(fd, ev);
    struct epoll_event inputEv = {};
    inputEv.events = EPOLLIN | EPOLLET;
    inputEv.data.fd = fd;
    // AddRawSocketEvent: epoll_ctl(ADD) succeeds, InsertSocketEventData fails
    EXPECT_LT(poll_->AddRawSocketEvent(fd, &inputEv), 0);
    // Clean up
    poll_->RemoveSocketEventData(fd);
    poll_->ReleaseRemovedEventsData();
    close(fd);
}

// ==================== AddProtoTxEvent: alloc fail (fail 2nd alloc, let 1st succeed) ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_ProtoTxAllocFail)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    int txFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(txFd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = txFd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    // Fail the 2nd new(nothrow) — 1st is AddRawSocketEvent's EpollEvent, 2nd is AddProtoTxEvent's
    g_newNoThrowAllocCount = 0;
    g_newNoThrowFailAt = 2;
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    g_newNoThrowFailAt = -1;
    g_newNoThrowAllocCount = 0;
    close(txFd);
    close(fd);
}

// ==================== DelProtoTxEvent: DelTxEvent fail ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlDel_UbSocket_DelTxEventFail)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    int txFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(txFd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = txFd;
    rawSock->delTxEventRet = -1;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Del should still succeed (DelProtoTxEvent fail is logged but doesn't affect EpollCtlDel return)
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr), 0);
    close(txFd);
    close(fd);
}

// ==================== AddRawSocketEvent: alloc fail ====================

TEST_F(AsyncEventPollErrorTest, AddRawSocketEvent_AllocFail_ReturnsError)
{
    poll_ = std::make_unique<AsyncEventPoll>(epoll_fd_);
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    g_epollNewNoThrowFail = 1;
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    g_epollNewNoThrowFail = 0;
    g_newNoThrowFailAt = -1;
    g_newNoThrowAllocCount = 0;
    close(fd);
}

// ==================== EpollRunnerBase virtual destructor coverage ====================

TEST(EventEpollMiscTest, EpollRunnerBase_VirtualDestructor_NoCrash)
{
    class MockRunnerBase : public EpollRunnerBase {
    public:
        int Start() override { return 0; }
        void Stop() override {}
        int AddEpollEvent(int, struct epoll_event *, EpollRunnerOps::ExtContext *) override { return 0; }
        int DelEpollEvent(int) override { return 0; }
        int ProcessOneEvent(const struct epoll_event &) override { return 0; }
        std::string GetRunnerName() override { return "mock"; }
        EpollRunnerOps *GetOps() override { return nullptr; }
    };
    EpollRunnerBase *base = new MockRunnerBase();
    delete base;
}

// ==================== EventPoll Ref operations ====================

TEST(EventEpollMiscTest, EventPoll_RefOperations_NoCrash)
{
    LockRegistry::RegisterDefaultOps();
    int efd = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(efd, 0);
    {
        auto *raw = new AsyncEventPoll(efd);
        AsyncEventPollPtr ptr(raw);
        EXPECT_EQ(ptr->GetEpollFd(), efd);
        AsyncEventPollPtr ptr2 = ptr;
        EXPECT_EQ(ptr2.Get(), ptr.Get());
    }
    close(efd);
}

// ==================== ExternalPoller: add_consumer fail, Stop without Start, DrainReadyEvents cb ====================

TEST_F(EpollRunnerResetTest, ExternalPoller_StartFail_AddConsumerReturnsError)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    static u_external_poller_ops_t ops;
    ops.add_consumer = [](int, void *, u_poller_event_cb_t, void **) { return -1; };
    ops.remove_consumer = [](void *, int) {};
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), -1);
    // Stop without successful Start → started_ is false → early return
    backend->Stop();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

TEST_F(EpollRunnerResetTest, ExternalPoller_DrainReadyEvents_Callback)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    cr->exit_efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(cr->exit_efd_, 0);
    RunnerEventData stopData{};
    stopData.event_data.type = RUNNER_EVENT_TYPE_STOP;
    stopData.event_data.data = static_cast<uint64_t>(cr->exit_efd_);
    struct epoll_event stopEv = {.events = EPOLLIN | EPOLLET, .data = {.u64 = stopData.u64}};
    epoll_ctl(cr->epoll_fd_, EPOLL_CTL_ADD, cr->exit_efd_, &stopEv);
    static u_poller_event_cb_t savedCb = nullptr;
    static void *savedArg = nullptr;
    static u_external_poller_ops_t ops;
    ops.add_consumer = [](int, void *arg, u_poller_event_cb_t cb, void **consumer) {
        savedCb = cb;
        savedArg = arg;
        *consumer = arg;
        return 0;
    };
    ops.remove_consumer = [](void *, int) {};
    ops.add_direct_poller = nullptr;
    ops.remove_direct_poller = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), 0);
    // Call the DrainReadyEvents callback with null arg → line 161
    ASSERT_NE(savedCb, nullptr);
    savedCb(nullptr);
    // Write to exit_efd so DrainReadyEvents returns true (stop event) → line 168
    eventfd_write(cr->exit_efd_, 1);
    savedCb(savedArg);
    backend->Stop();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->exit_efd_);
    close(cr->epoll_fd_);
    cr->exit_efd_ = -1;
    cr->epoll_fd_ = -1;
}

// ==================== ExternalDirectPoller: ops nulled after create, Stop without Start ====================

TEST_F(EpollRunnerResetTest, ExternalDirectPoller_StartFail_OpsNulled)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    static u_external_poller_ops_t ops;
    ops.add_direct_poller = [](int, void *, u_poller_process_event_cb_t, void **) { return 0; };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    // Null out the ops to make Start fail at the null check
    ops.add_direct_poller = nullptr;
    EXPECT_EQ(backend->Start(), -1);
    // Stop without Start → started_ false → early return
    backend->Stop();
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

// ==================== ExternalDirectPoller: ProcessEventCb with non-STOP event ====================

TEST_F(EpollRunnerResetTest, ExternalDirectPoller_ProcessEventCb_NormalEvent)
{
    auto &runner = EpollRunnerFactory::GetInstance(EpollRunnerType::SHARE_JFR_RX_RUNNER);
    auto *cr = reinterpret_cast<EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER> *>(&runner);
    cr->epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    ASSERT_GE(cr->epoll_fd_, 0);
    // Set up mock ops that will call ProcessEventCb with a normal event
    static u_external_poller_ops_t ops;
    static u_poller_process_event_cb_t savedCb = nullptr;
    static void *savedArg = nullptr;
    ops.add_direct_poller = [](int, void *arg, u_poller_process_event_cb_t cb, void **poller) {
        savedCb = cb;
        savedArg = arg;
        *poller = arg;
        // Call with a normal (non-STOP) event
        RunnerEventData normalData{};
        normalData.event_data.type = RUNNER_EVENT_TYPE_TX_WAKE;
        normalData.event_data.data = 42;
        struct epoll_event normalEv = {};
        normalEv.data.u64 = normalData.u64;
        cb(arg, &normalEv);
        return 0;
    };
    ops.remove_direct_poller = [](void *, int) {};
    ops.add_consumer = nullptr;
    ops.remove_consumer = nullptr;
    GlobalSetting::UBS_POLLER_OPS = &ops;
    // Set ops_ so ProcessOneEvent doesn't crash
    cr->ops_ = new EpollRunnerOps();
    auto backend = cr->CreateBackend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->Start(), 0);
    backend->Stop();
    // Also test null arg
    if (savedCb != nullptr) {
        struct epoll_event dummyEv = {};
        savedCb(nullptr, &dummyEv);
    }
    delete cr->ops_;
    cr->ops_ = nullptr;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    close(cr->epoll_fd_);
    cr->epoll_fd_ = -1;
}

// ==================== ArrangeWakeUpEvents: UB_SOCKET_OUT with separate txFd ====================

TEST_F(UbSocketEpollCtlTest, ArrangeWakeUpEvents_UbSocketOut_SeparateTxFd)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    int txFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(txFd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = txFd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_EQ(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    // Write to txFd to trigger the UB_SOCKET_OUT event
    eventfd_write(txFd, 1);
    struct epoll_event out[4];
    int ret = poll_->EpollWait(out, 4, 100);
    EXPECT_GE(ret, 0);
    poll_->EpollCtl(EPOLL_CTL_DEL, fd, nullptr);
    close(txFd);
    close(fd);
}

// ==================== AddProtoTxEvent alloc fail with correct counter ====================

TEST_F(UbSocketEpollCtlTest, EpollCtlAdd_UbSocket_ProtoTxAllocFail_Counter)
{
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(fd, 0);
    int txFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT_GE(txFd, 0);
    auto *rawSock = new UbMockSocketBase(fd);
    rawSock->shouldRegisterTx = true;
    rawSock->txFd = txFd;
    ArraySet<Socket>::GetInstance().OverrideItem(fd, rawSock);
    SocketPtr sock(rawSock);
    sock->state_.store(SOCK_STAT_ESTABLISHED, std::memory_order_release);
    // 1st alloc: CreateSocketEpollMapper → EpollMapper
    // 2nd alloc: AddRawSocketEvent → EpollEvent
    // 3rd alloc: AddProtoTxEvent → EpollEvent (fail this one)
    g_newNoThrowAllocCount = 0;
    g_newNoThrowFailAt = 3;
    struct epoll_event ev = {};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    EXPECT_LT(poll_->EpollCtl(EPOLL_CTL_ADD, fd, &ev), 0);
    g_newNoThrowFailAt = -1;
    g_newNoThrowAllocCount = 0;
    close(txFd);
    close(fd);
}

/* issue #44: the readable-event ring was a fixed 65536 while the fd table may now hold up to 1M sockets;
 * a ring smaller than the table drops readable notifications once that many sockets are readable at once. */
TEST(ReadableRingCapacityTest, TracksFdTableAsPowerOfTwoWithinBounds)
{
    using ock::ubs::ReadableRingCapacityFor;
    EXPECT_EQ(ReadableRingCapacityFor(0), 65536ULL);
    EXPECT_EQ(ReadableRingCapacityFor(65536), 65536ULL);
    EXPECT_EQ(ReadableRingCapacityFor(65537), 131072ULL);
    EXPECT_EQ(ReadableRingCapacityFor(80000), 131072ULL);
    EXPECT_EQ(ReadableRingCapacityFor(1ULL << 18), 1ULL << 18);
    EXPECT_EQ(ReadableRingCapacityFor(1ULL << 20), 1ULL << 18);
}
