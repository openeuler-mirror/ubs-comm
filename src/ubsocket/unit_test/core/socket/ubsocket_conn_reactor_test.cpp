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
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
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
    thread.conn_ops_map_[TEST_FD].done_cb = [&done_fd](int fd, const struct sockaddr &, socklen_t) { done_fd = fd; };

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