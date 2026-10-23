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
#include <fcntl.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <mockcpp/mockcpp.hpp>
#include <stdexcept>
#include <thread>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "core/ubsocket_conn_ops_base.h"
#include "core/ubsocket_conn_reactor.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

namespace {
static const int TEST_FD = 100;
static const int TEST_EVENT_FD = 600;

static int g_close_calls = 0;
static int MockCloseCount(int)
{
    ++g_close_calls;
    return 0;
}

static int MockPollZero(struct pollfd *, nfds_t, int)
{
    return 0;
}

static ssize_t MockReadEagain(int, void *, size_t)
{
    errno = EAGAIN;
    return -1;
}

static int MockFcntlNonBlocking(int fd, int cmd, ...)
{
    (void)fd;
    if (cmd == F_GETFL) {
        return O_NONBLOCK;
    }
    return 0;
}

static int MockFcntlFail(int fd, int cmd, ...)
{
    (void)fd;
    (void)cmd;
    errno = EINVAL;
    return -1;
}

static struct sockaddr_in MakeTestAddr()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(12345);
    return addr;
}

static const struct sockaddr &TestAddr()
{
    static struct sockaddr_in addr = MakeTestAddr();
    return *reinterpret_cast<const struct sockaddr *>(&addr);
}

class TestConnOps : public UBConnOpsBase {
public:
    TestConnOps(ConnRole role, int fd) : UBConnOpsBase(role, fd, TestAddr(), sizeof(struct sockaddr_in)) {}

    ConnStageResult DoWaitNegotiate() override
    {
        return negotiate_result;
    }

    ConnStageResult DoWaitPeerRes() override
    {
        return peer_res_result;
    }

    ConnStageResult DoWaitPeerAck() override
    {
        return peer_ack_result;
    }

    void OnConnDone() override
    {
        ++done_calls;
    }

    void OnConnFailed() override
    {
        ++failed_calls;
    }

    void SetState(ConnState state)
    {
        state_.store(state, std::memory_order_release);
    }

    ConnStageResult negotiate_result = ConnStageResult::kFailed;
    ConnStageResult peer_res_result = ConnStageResult::kFailed;
    ConnStageResult peer_ack_result = ConnStageResult::kFailed;
    int done_calls = 0;
    int failed_calls = 0;
};

class ThrowingConnOps : public TestConnOps {
public:
    ThrowingConnOps(ConnRole role, int fd) : TestConnOps(role, fd) {}

    ConnStageResult DoWaitNegotiate() override
    {
        throw std::runtime_error("execute boom");
    }
};

static std::atomic<int> g_exec_count{0};

class CountingPendingOps : public TestConnOps {
public:
    CountingPendingOps(ConnRole role, int fd) : TestConnOps(role, fd)
    {
        negotiate_result = ConnStageResult::kPending;
    }

    ConnStageResult DoWaitNegotiate() override
    {
        g_exec_count.fetch_add(1);
        return ConnStageResult::kPending;
    }
};

static std::atomic<int> g_done_count{0};

static ssize_t MockReadZeroCount(int, void *buf, size_t)
{
    *static_cast<uint64_t *>(buf) = 0;
    return static_cast<ssize_t>(sizeof(uint64_t));
}

/* 让下一次 poll 报告指定 fd 可读并返回 1；每次只消费一个信号。
 * 必须先清零全部 revents（真 poll 会重置），否则残留的 revents 会掩盖 revents==0 分支 */
static std::atomic<int> g_signal_fd{-1};
static int MockPollSignalFd(struct pollfd *fds, nfds_t nfds, int)
{
    int target = g_signal_fd.exchange(-1);
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
    }
    if (target < 0) {
        return 0;
    }
    for (nfds_t i = 0; i < nfds; ++i) {
        if (fds[i].fd == target) {
            fds[i].revents = POLLIN;
            return 1;
        }
    }
    return 0;
}

/* 阻塞直到目标线程 stopped_ 置位，然后返回 -1(EIO)：驱动 RunLoop 的 poll 错误 + stop 后 break 分支 */
static ConnReactorThread *g_reactor_for_poll = nullptr;
static std::atomic<int> g_poll_entered{0};
static int MockPollBlockUntilStoppedThenError(struct pollfd *, nfds_t, int)
{
    g_poll_entered.fetch_add(1);
    while (g_reactor_for_poll != nullptr && !g_reactor_for_poll->stopped_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    errno = EIO;
    return -1;
}

/* 1=EINTR，2=其它错误（EIO），其余返回 0；用于驱动 RunLoop 的 poll 错误分支 */
static std::atomic<int> g_poll_errno_mode{0};
static std::atomic<int> g_poll_errno_calls{0};
static int MockPollErrnoMode(struct pollfd *, nfds_t, int)
{
    g_poll_errno_calls.fetch_add(1);
    int mode = g_poll_errno_mode.load();
    if (mode == 1) {
        errno = EINTR;
        return -1;
    }
    if (mode == 2) {
        errno = EIO;
        return -1;
    }
    return 0;
}

template <typename Pred>
static bool WaitUntil(Pred pred, int timeout_ms = 500)
{
    for (int i = 0; i < timeout_ms && !pred(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}
} // namespace

class ConnReactorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        LibcApi::Load();
        g_close_calls = 0;
        ConnEventFdTable::Instance().Unregister(TEST_EVENT_FD);
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ConnEventFdTable::Instance().Unregister(TEST_EVENT_FD);
        LibcApi::UnLoad();
        LibcApi::Load();
    }
};

/* ==================== ConnEventFdTable ==================== */

TEST_F(ConnReactorTest, EventFdTable_RegisterAndLeastLoaded_BasicFlow)
{
    ConnEventFdTable &table = ConnEventFdTable::Instance();
    EXPECT_EQ(-1, table.GetLeastLoadedIndex()); /* 空表 */

    table.Register(TEST_EVENT_FD, 2);
    EXPECT_EQ(2, table.GetLeastLoadedIndex());

    table.IncLoad(TEST_EVENT_FD);
    table.IncLoad(TEST_EVENT_FD);
    EXPECT_EQ(2, table.GetLeastLoadedIndex());

    table.Register(TEST_EVENT_FD + 1, 5);
    EXPECT_EQ(5, table.GetLeastLoadedIndex()); /* load 0 < load 2 */

    table.IncLoad(TEST_EVENT_FD + 1);
    table.IncLoad(TEST_EVENT_FD + 1);
    table.IncLoad(TEST_EVENT_FD + 1);
    EXPECT_EQ(2, table.GetLeastLoadedIndex());

    table.Unregister(TEST_EVENT_FD);
    table.Unregister(TEST_EVENT_FD + 1);
}

TEST_F(ConnReactorTest, EventFdTable_RegisterDuplicate_UpdatesThreadIndex)
{
    ConnEventFdTable &table = ConnEventFdTable::Instance();
    table.Register(TEST_EVENT_FD, 0);
    table.Register(TEST_EVENT_FD, 3);
    EXPECT_EQ(3, table.GetLeastLoadedIndex());
    table.Unregister(TEST_EVENT_FD);
}

TEST_F(ConnReactorTest, EventFdTable_IncDecLoad_MissingEntryNoOpAndZeroFloor)
{
    ConnEventFdTable &table = ConnEventFdTable::Instance();
    table.IncLoad(TEST_EVENT_FD); /* 未注册：no-op */
    EXPECT_EQ(-1, table.GetLeastLoadedIndex());

    table.Register(TEST_EVENT_FD, 1);
    table.DecLoad(TEST_EVENT_FD); /* load 0 下限，不降到负数 */
    EXPECT_EQ(1, table.GetLeastLoadedIndex());
    table.Unregister(TEST_EVENT_FD);
}

/* ==================== ConnReactorThread ==================== */

TEST_F(ConnReactorTest, Init_EventfdFail_ReturnsError)
{
    MOCKER(eventfd).stubs().will(returnValue(-1));
    ConnReactorThread thread;
    EXPECT_EQ(UBS_ERROR, thread.Init(0));
    EXPECT_FALSE(thread.started_.load());
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Init_Stop_RealThreadLifecycle)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    LibcApi::poll_ptr = MockPollZero;
    LibcApi::close_ptr = MockCloseCount;

    ConnReactorThread thread;
    EXPECT_EQ(UBS_OK, thread.Init(0));
    EXPECT_TRUE(thread.started_.load());
    EXPECT_EQ(TEST_EVENT_FD, thread.event_fd_);
    EXPECT_EQ(0, ConnEventFdTable::Instance().GetLeastLoadedIndex());

    thread.Stop();
    EXPECT_FALSE(thread.started_.load());
    EXPECT_EQ(-1, thread.event_fd_);
    EXPECT_GE(g_close_calls, 1); /* event fd 被关闭 */
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, AddConnection_NotStarted_ReturnsError)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, thread.AddConnection(TEST_FD, ops, ConnReactorThread::DoneCallback()));
}

TEST_F(ConnReactorTest, AddConnection_DuplicateFd_ReturnsError)
{
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    std::shared_ptr<UBConnOpsBase> dup = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, thread.AddConnection(TEST_FD, dup, ConnReactorThread::DoneCallback()));
}

TEST_F(ConnReactorTest, AddConnection_SetNonBlockingFail_ReturnsError)
{
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::fcntl_ptr = MockFcntlFail;

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, thread.AddConnection(TEST_FD, ops, ConnReactorThread::DoneCallback()));
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, AddConnection_Success_InsertsMapAndWakesEventFd)
{
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_OK, thread.AddConnection(TEST_FD, ops, ConnReactorThread::DoneCallback()));
    ASSERT_TRUE(thread.conn_ops_map_.find(TEST_FD) != thread.conn_ops_map_.end());
    EXPECT_EQ(ops, thread.conn_ops_map_[TEST_FD].ops);
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, HandleEventFd_PopsQueuedFdAndDrivesToDone)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::read_ptr = MockReadEagain;

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    ops->SetState(ConnState::kConnEstablished);
    thread.conn_ops_map_[TEST_FD].ops = ops;
    thread.event_queue_.Push(TEST_FD);

    thread.HandleEventFd(1000);
    EXPECT_EQ(1, ops->done_calls);
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, HandleSocketFd_PendingWantsWritable_AddsPollInOut)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    ops->SetState(ConnState::kWaitNegotiate);
    ops->negotiate_result = ConnStageResult::kPending;
    ops->wants_writable_ = true;
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.HandleSocketFd(TEST_FD, 1000);
    ASSERT_FALSE(thread.poll_fds_.empty());
    EXPECT_EQ(TEST_FD, thread.poll_fds_.back().fd);
    EXPECT_EQ(static_cast<short>(POLLIN | POLLOUT), thread.poll_fds_.back().events);
    EXPECT_FALSE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, HandleSocketFd_PendingNoWritable_AddsPollInOnly)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    ops->SetState(ConnState::kWaitNegotiate);
    ops->negotiate_result = ConnStageResult::kPending;
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.HandleSocketFd(TEST_FD, 1000);
    ASSERT_FALSE(thread.poll_fds_.empty());
    EXPECT_EQ(TEST_FD, thread.poll_fds_.back().fd);
    EXPECT_EQ(static_cast<short>(POLLIN), thread.poll_fds_.back().events);
}

TEST_F(ConnReactorTest, FinishConnection_Done_CallsOnConnDoneAndCallback)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    int done_fd = -1;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;
    thread.conn_ops_map_[TEST_FD].done_cb = [&done_fd](int fd, const struct sockaddr &, socklen_t) {
        done_fd = fd;
    };

    thread.FinishConnection(TEST_FD, ConnStageResult::kDone);
    EXPECT_EQ(1, ops->done_calls);
    EXPECT_EQ(TEST_FD, done_fd);
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, FinishConnection_FailedServer_AbortsAndClosesFd)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::close_ptr = MockCloseCount;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.FinishConnection(TEST_FD, ConnStageResult::kFailed);
    EXPECT_TRUE(ops->IsAborted());
    EXPECT_EQ(1, ops->failed_calls);
    EXPECT_EQ(1, g_close_calls); /* server: fd 自 accept 所得，由 reactor close */
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, FinishConnection_FailedClient_DoesNotCloseFd)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::close_ptr = MockCloseCount;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.FinishConnection(TEST_FD, ConnStageResult::kFailed);
    EXPECT_EQ(1, ops->failed_calls);
    EXPECT_EQ(0, g_close_calls); /* client: fd 归应用，不 close */
}

TEST_F(ConnReactorTest, FinishConnection_UnknownFd_NoOp)
{
    ConnReactorThread thread;
    thread.FinishConnection(TEST_FD, ConnStageResult::kDone); /* map 无 entry 直接返回 */
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_IdleExceedsTimeout_FinishesFailed)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::close_ptr = MockCloseCount;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    ops->SetState(ConnState::kWaitNegotiate);
    ops->last_active_ms_ = 5;
    thread.conn_ops_map_[TEST_FD].ops = ops;
    /* 有效时钟 = 墙钟 - busy_ms_，构造 20s 空闲 */
    thread.busy_ms_ = SocketConnHelper::GetTimeMs() - 20000;

    thread.ScanTimeoutConnections();
    EXPECT_EQ(1, ops->failed_calls);
    EXPECT_EQ(ETIMEDOUT, ops->FailureErrno());
    EXPECT_EQ(1, g_close_calls);
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_NotYetActive_Skips)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    ops->last_active_ms_ = 0; /* 刚提交尚未首帧驱动：跳过防御误杀 */
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.ScanTimeoutConnections();
    EXPECT_EQ(0, ops->failed_calls);
    EXPECT_FALSE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, HandleCancelQueue_ErasesConnection)
{
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.Cancel(TEST_FD); /* 同步置 Abort + 异步投递 cancel_queue_ */
    EXPECT_TRUE(ops->IsAborted());

    thread.HandleCancelQueue(); /* poll 线程消费：移出监听 + 清映射 */
    EXPECT_TRUE(thread.conn_ops_map_.empty());
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Cancel_AbortsAndQueues)
{
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.Cancel(TEST_FD);
    EXPECT_TRUE(ops->IsAborted());
    int fd = -1;
    EXPECT_TRUE(thread.cancel_queue_.Pop(fd));
    EXPECT_EQ(TEST_FD, fd);
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, AddPollFd_ReuseEmptySlot)
{
    ConnReactorThread thread;
    thread.AddPollFd(TEST_FD, POLLIN);
    thread.AddPollFd(TEST_FD + 1, POLLOUT);
    EXPECT_EQ(static_cast<size_t>(2), thread.poll_fds_.size());

    thread.RemovePollFd(TEST_FD); /* 惰性删除置 fd=-1 */
    EXPECT_EQ(-1, thread.poll_fds_[0].fd);

    thread.AddPollFd(TEST_FD + 2, POLLIN | POLLOUT);
    EXPECT_EQ(static_cast<size_t>(2), thread.poll_fds_.size()); /* 复用槽位 */
    EXPECT_EQ(TEST_FD + 2, thread.poll_fds_[0].fd);
}

TEST_F(ConnReactorTest, AddPollFd_ExistingFd_UpdatesEvents)
{
    ConnReactorThread thread;
    thread.AddPollFd(TEST_FD, POLLIN);
    thread.AddPollFd(TEST_FD, POLLIN | POLLOUT);
    EXPECT_EQ(static_cast<size_t>(1), thread.poll_fds_.size());
    EXPECT_EQ(static_cast<short>(POLLIN | POLLOUT), thread.poll_fds_[0].events);
}

/* ==================== ConnDispatcher ==================== */

TEST_F(ConnReactorTest, Dispatcher_StartZeroThreads_ReturnsInvalidParam)
{
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    EXPECT_EQ(UBS_INVALID_PARAM, dispatcher.Start(0));
    EXPECT_FALSE(dispatcher.started_.load());
}

TEST_F(ConnReactorTest, Dispatcher_SubmitNotStarted_ReturnsError)
{
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, dispatcher.Submit(TEST_FD, TestAddr(), sizeof(struct sockaddr_in),
                                           ConnReactorThread::DoneCallback(), ops));
}

TEST_F(ConnReactorTest, Dispatcher_SubmitNullOps_ReturnsInvalidParam)
{
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    dispatcher.started_.store(true);
    EXPECT_EQ(UBS_INVALID_PARAM, dispatcher.Submit(TEST_FD, TestAddr(), sizeof(struct sockaddr_in),
                                                   ConnReactorThread::DoneCallback(), nullptr));
    dispatcher.started_.store(false);
}

TEST_F(ConnReactorTest, Dispatcher_SubmitNoThreadRegistered_ReturnsError)
{
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    dispatcher.started_.store(true); /* threads_ 空 + table 空 → index -1 */
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, dispatcher.Submit(TEST_FD, TestAddr(), sizeof(struct sockaddr_in),
                                           ConnReactorThread::DoneCallback(), ops));
    dispatcher.started_.store(false);
}

TEST_F(ConnReactorTest, Dispatcher_CancelNotStarted_NoOp)
{
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    dispatcher.Cancel(TEST_FD); /* 不崩溃即可 */
}

TEST_F(ConnReactorTest, Dispatcher_StartStop_RealThreads)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    LibcApi::poll_ptr = MockPollZero;
    LibcApi::close_ptr = MockCloseCount;

    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    EXPECT_EQ(UBS_OK, dispatcher.Start(1));
    EXPECT_TRUE(dispatcher.started_.load());

    dispatcher.Stop();
    EXPECT_FALSE(dispatcher.started_.load());
    GlobalMockObject::verify();
}

/* ==================== 补充：未覆盖分支/路径 ==================== */

TEST_F(ConnReactorTest, EventFdTable_DecLoad_DecrementsActiveLoad)
{
    ConnEventFdTable &table = ConnEventFdTable::Instance();
    table.Register(TEST_EVENT_FD, 1);
    table.Register(TEST_EVENT_FD + 1, 2);
    table.IncLoad(TEST_EVENT_FD);
    table.IncLoad(TEST_EVENT_FD + 1); /* 两者 load 均为 1 */
    table.DecLoad(TEST_EVENT_FD);     /* f1 load 1->0，成为最轻者 */
    EXPECT_EQ(1, table.GetLeastLoadedIndex());
    table.Unregister(TEST_EVENT_FD);
    table.Unregister(TEST_EVENT_FD + 1);
}

TEST_F(ConnReactorTest, Init_SecondCall_Idempotent)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    LibcApi::poll_ptr = MockPollZero;
    LibcApi::close_ptr = MockCloseCount;

    ConnReactorThread thread;
    EXPECT_EQ(UBS_OK, thread.Init(0));
    EXPECT_EQ(UBS_OK, thread.Init(1)); /* started_ 已置位：幂等直接返回，不再建线程 */
    thread.Stop();
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, AddConnection_EventQueueFull_ReturnsError)
{
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;
    for (uint64_t i = 0; i < ConnReactorThread::kEventQueueCapacity; ++i) {
        ASSERT_TRUE(thread.event_queue_.Push(static_cast<int>(i)));
    }

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_ERROR, thread.AddConnection(TEST_FD, ops, ConnReactorThread::DoneCallback()));
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, AddConnection_EventfdWriteFail_ReturnsOk)
{
    MOCKER(eventfd_write).stubs().will(returnValue(-1));
    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_OK, thread.AddConnection(TEST_FD, ops, ConnReactorThread::DoneCallback()));
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Cancel_StoppedThread_EarlyReturn)
{
    ConnReactorThread thread;
    thread.stopped_.store(true);
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.Cancel(TEST_FD);
    EXPECT_FALSE(ops->IsAborted());
}

TEST_F(ConnReactorTest, Cancel_EventfdWriteFail_StillQueues)
{
    MOCKER(eventfd_write).stubs().will(returnValue(-1));
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.Cancel(TEST_FD);
    EXPECT_TRUE(ops->IsAborted());
    int fd = -1;
    EXPECT_TRUE(thread.cancel_queue_.Pop(fd));
    EXPECT_EQ(TEST_FD, fd);
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, HandleEventFd_ReadReturnsZeroCount_Breaks)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    LibcApi::read_ptr = MockReadZeroCount;

    thread.HandleEventFd(1000); /* read 返回 8 字节但 count==0 → 立即 break */
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, HandleSocketFd_UnknownFd_NoOp)
{
    ConnReactorThread thread;
    thread.HandleSocketFd(TEST_FD, 1000); /* 映射中无该 fd，直接返回 */
    EXPECT_TRUE(thread.poll_fds_.empty());
}

TEST_F(ConnReactorTest, HandleSocketFd_ExecuteThrows_FinishesFailed)
{
    LibcApi::close_ptr = MockCloseCount;
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<ThrowingConnOps>(ConnRole::kServer, TEST_FD);
    ops->SetState(ConnState::kWaitNegotiate);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.HandleSocketFd(TEST_FD, 1000); /* Execute 抛异常 → 视为 kFailed 收尾 */
    EXPECT_TRUE(ops->IsAborted());
    EXPECT_EQ(1, ops->failed_calls);
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_EmptyMap_NoOp)
{
    ConnReactorThread thread;
    thread.ScanTimeoutConnections(); /* 空映射直接返回 */
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_ActiveNotIdle_Keeps)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    ops->SetState(ConnState::kWaitNegotiate);
    ops->last_active_ms_ = SocketConnHelper::GetTimeMs(); /* 刚活跃，未超时 */
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.ScanTimeoutConnections();
    EXPECT_EQ(0, ops->failed_calls);
    EXPECT_FALSE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_NearDeadline_ShrinksPollWait)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    /* 已空闲 (10s - 90ms)：最近 deadline 剩余约 90ms，返回值应小于默认 100ms 心跳以按点唤醒 */
    ops->last_active_ms_ = SocketConnHelper::GetTimeMs() - (CONN_IDLE_TIMEOUT_MS - 90);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    int wait_ms = thread.ScanTimeoutConnections();
    EXPECT_GT(wait_ms, 0);
    EXPECT_LT(wait_ms, 100);
    EXPECT_EQ(0, ops->failed_calls); /* 未超时：不误杀 */
    EXPECT_FALSE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, ScanTimeoutConnections_BusyClockAhead_Skips)
{
    ConnReactorThread thread;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    ops->last_active_ms_ = 0; /* 未首帧驱动：跳过 */
    thread.conn_ops_map_[TEST_FD].ops = ops;
    thread.busy_ms_ = UINT64_MAX; /* 有效时钟回绕保护分支 */

    thread.ScanTimeoutConnections();
    EXPECT_EQ(0, ops->failed_calls);
    EXPECT_FALSE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, FinishConnection_DoneWithoutCallback_OnlyOnConnDone)
{
    ConnReactorThread thread;
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops; /* 未设置 done_cb */

    thread.FinishConnection(TEST_FD, ConnStageResult::kDone);
    EXPECT_EQ(1, ops->done_calls);
    EXPECT_TRUE(thread.conn_ops_map_.empty());
}

TEST_F(ConnReactorTest, RemovePollFd_NotFound_NoOp)
{
    ConnReactorThread thread;
    thread.AddPollFd(TEST_FD, POLLIN);
    thread.RemovePollFd(TEST_FD + 1); /* 未命中 */
    ASSERT_EQ(static_cast<size_t>(1), thread.poll_fds_.size());
    EXPECT_EQ(TEST_FD, thread.poll_fds_[0].fd);
}

TEST_F(ConnReactorTest, Stop_WithPendingServerConnection_AbortsAndCloses)
{
    LibcApi::close_ptr = MockCloseCount;
    MOCKER(eventfd_write).stubs().will(returnValue(0));

    ConnReactorThread thread;
    thread.started_.store(true);
    thread.event_fd_ = TEST_EVENT_FD;
    auto ops = std::make_shared<TestConnOps>(ConnRole::kServer, TEST_FD);
    thread.conn_ops_map_[TEST_FD].ops = ops;

    thread.Stop();
    EXPECT_TRUE(ops->IsAborted());
    EXPECT_EQ(1, ops->failed_calls);
    EXPECT_EQ(2, g_close_calls); /* 待建链 server fd + 线程 event fd */
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Dispatcher_StartInitThreadFail_ReturnsError)
{
    MOCKER(eventfd).stubs().will(returnValue(-1)); /* 首个线程 Init 失败 */
    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    EXPECT_EQ(UBS_ERROR, dispatcher.Start(1));
    EXPECT_FALSE(dispatcher.started_.load());
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Dispatcher_StartTwice_Idempotent)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    LibcApi::poll_ptr = MockPollZero;
    LibcApi::close_ptr = MockCloseCount;

    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    EXPECT_EQ(UBS_OK, dispatcher.Start(1));
    EXPECT_EQ(UBS_OK, dispatcher.Start(1)); /* 幂等：不再创建线程池 */
    dispatcher.Stop();
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Dispatcher_SubmitAndCancel_RealThread)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    LibcApi::poll_ptr = MockPollZero;
    LibcApi::close_ptr = MockCloseCount;
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;

    ConnDispatcher &dispatcher = ConnDispatcher::Instance();
    dispatcher.Stop();
    ASSERT_EQ(UBS_OK, dispatcher.Start(1));

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_OK, dispatcher.Submit(TEST_FD, TestAddr(), sizeof(struct sockaddr_in),
                                        ConnReactorThread::DoneCallback(), ops));
    dispatcher.Cancel(TEST_FD); /* 已启动：遍历线程转发 Cancel，命中者置 Abort */
    EXPECT_TRUE(ops->IsAborted());

    dispatcher.Stop();
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, RunLoop_ProcessesReadyFds)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    LibcApi::read_ptr = MockReadEagain;
    LibcApi::close_ptr = MockCloseCount;
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;
    LibcApi::poll_ptr = MockPollSignalFd;

    g_done_count.store(0);
    g_exec_count.store(0);
    g_signal_fd.store(-1);

    ConnReactorThread thread;
    ASSERT_EQ(UBS_OK, thread.Init(0));

    /* 连接 A：一次 Execute 即建链完成 */
    auto done_ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    done_ops->SetState(ConnState::kConnEstablished);
    ASSERT_EQ(UBS_OK, thread.AddConnection(TEST_FD, done_ops,
                                           [](int, const struct sockaddr &, socklen_t) { g_done_count.fetch_add(1); }));

    /* 连接 B：半包挂起，保留监听等可读 */
    auto pending_ops = std::make_shared<CountingPendingOps>(ConnRole::kClient, TEST_FD + 1);
    pending_ops->SetState(ConnState::kWaitNegotiate);
    ASSERT_EQ(UBS_OK, thread.AddConnection(TEST_FD + 1, pending_ops, ConnReactorThread::DoneCallback()));

    /* 唤醒 event fd：RunLoop 就绪分支 -> HandleEventFd 取出两个任务 */
    g_signal_fd.store(TEST_EVENT_FD);
    EXPECT_TRUE(WaitUntil([] { return g_done_count.load() > 0 && g_exec_count.load() > 0; }));

    /* B 已加入监听数组：再次唤醒 RunLoop 走 socket fd（else）分支 */
    g_signal_fd.store(TEST_FD + 1);
    EXPECT_TRUE(WaitUntil([] { return g_exec_count.load() >= 2; }));

    thread.Stop();
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, RunLoop_ScanAndPollErrorPaths)
{
    int saved_level = Logger::Instance().GetLogLevel();
    Logger::Instance().SetLogLevel(LEVEL_COUNT); /* 静默错误日志，避免刷屏 */

    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    LibcApi::poll_ptr = MockPollErrnoMode;
    LibcApi::close_ptr = MockCloseCount;

    g_poll_errno_mode.store(1); /* EINTR */
    g_poll_errno_calls.store(0);
    ConnReactorThread thread;
    ASSERT_EQ(UBS_OK, thread.Init(0)); /* 每轮 poll 前扫描（空映射安全返回） */
    EXPECT_TRUE(WaitUntil([] { return g_poll_errno_calls.load() >= 1; })); /* EINTR 分支 */

    g_poll_errno_mode.store(2); /* 非 EINTR 错误：记录日志后继续 */
    EXPECT_TRUE(WaitUntil([] { return g_poll_errno_calls.load() >= 3; }));

    thread.Stop(); /* stopped 后 poll 仍失败 -> break 退出循环 */
    g_poll_errno_mode.store(0);
    Logger::Instance().SetLogLevel(saved_level);
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, RunLoop_NonEintrPollErrorAfterStop_Breaks)
{
    MOCKER(eventfd).stubs().will(returnValue(TEST_EVENT_FD));
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    LibcApi::poll_ptr = MockPollBlockUntilStoppedThenError;
    LibcApi::close_ptr = MockCloseCount;

    g_reactor_for_poll = nullptr;
    g_poll_entered.store(0);

    ConnReactorThread thread;
    g_reactor_for_poll = &thread; /* 先绑定，确保 RunLoop 一进入 poll 即被 mock 阻塞 */
    ASSERT_EQ(UBS_OK, thread.Init(0));

    /* 确保线程已进入 poll（被 mock 阻塞），再置 stopped_：poll 返回 -1(EIO) 且非 EINTR -> break */
    EXPECT_TRUE(WaitUntil([] { return g_poll_entered.load() > 0; }));
    thread.Stop();
    g_reactor_for_poll = nullptr;
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, AddConnection_StoppedWhileLockHeld_ReturnsError)
{
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    LibcApi::close_ptr = MockCloseCount;

    ConnReactorThread thread;
    thread.started_.store(true);
    thread.stopped_.store(false);

    auto ops = std::make_shared<TestConnOps>(ConnRole::kClient, TEST_FD);
    ConnReactorThread::DoneCallback cb;

    /* 主线程先持锁，worker 通过首道 started_/stopped_ 检查后阻塞在锁上；随后置 stopped_ 再放锁，
     * 命中锁内二次校验（stopped_ 已变真）分支 */
    std::atomic<int> worker_go{0};
    ock::ubs::Result res = UBS_OK;
    thread.conn_map_mutex_.lock();
    std::thread worker([&] {
        worker_go.store(1);
        res = thread.AddConnection(TEST_FD, ops, cb);
    });
    EXPECT_TRUE(WaitUntil([&] { return worker_go.load() > 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread.stopped_.store(true);
    thread.conn_map_mutex_.unlock();
    worker.join();
    EXPECT_EQ(UBS_ERROR, res);

    thread.stopped_.store(false);
    thread.started_.store(false);
    GlobalMockObject::verify();
}

TEST_F(ConnReactorTest, Cancel_CancelQueueFullThenStopped_Returns)
{
    MOCKER(eventfd_write).stubs().will(returnValue(0));
    Logger::Instance().SetLogLevel(LEVEL_COUNT);

    ConnReactorThread thread; /* 未 Init：stopped_ 默认 false，Cancel 正常入队 */
    const uint64_t cap = static_cast<uint64_t>(ConnReactorThread::kEventQueueCapacity);
    for (uint64_t i = 0; i < cap; ++i) {
        thread.Cancel(static_cast<int>(i + 1)); /* 灌满 cancel_queue_，无 RunLoop 消费 */
    }

    /* 队列已满：再 Cancel 进入 while(!Push) 自旋；另起线程置 stopped_ 后应从 stopped_ 分支返回 */
    std::atomic<int> spinner_go{0};
    std::thread spinner([&] {
        spinner_go.store(1);
        thread.Cancel(TEST_FD); /* Push 失败 -> 自旋 -> stopped_ 置位后 return */
    });
    EXPECT_TRUE(WaitUntil([&] { return spinner_go.load() > 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    thread.stopped_.store(true);
    spinner.join();

    thread.stopped_.store(false);
    GlobalMockObject::verify();
}