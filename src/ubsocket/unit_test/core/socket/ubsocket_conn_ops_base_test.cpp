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
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_conn_ops_base.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_socket.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

namespace {
static const int TEST_FD = 100;
static const int TEST_EPOLL_FD = 500;

/* send 行为序列：>0 表示本次发送的字节数(消耗等量输入)，0 表示返回0，-1 表示错误(err) */
struct SendStep {
    ssize_t ret = -1;
    int err = EAGAIN;
};

static std::vector<SendStep> g_send_steps;
static size_t g_send_idx = 0;
static std::vector<uint8_t> g_sent_bytes;

static ssize_t MockSendSeq(int, const void *buf, size_t len, int)
{
    if (g_send_idx >= g_send_steps.size()) {
        errno = EAGAIN;
        return -1;
    }
    const SendStep &step = g_send_steps[g_send_idx++];
    if (step.ret == 0) {
        return 0;
    }
    if (step.ret < 0) {
        errno = step.err;
        return -1;
    }
    size_t n = static_cast<size_t>(step.ret) < len ? static_cast<size_t>(step.ret) : len;
    const uint8_t *bytes = static_cast<const uint8_t *>(buf);
    g_sent_bytes.insert(g_sent_bytes.end(), bytes, bytes + n);
    return static_cast<ssize_t>(n);
}

static void PushSendOk(ssize_t bytes = -1)
{
    SendStep step;
    step.ret = bytes;
    step.err = 0;
    g_send_steps.push_back(step);
}

static void PushSendError(int err)
{
    SendStep step;
    step.ret = -1;
    step.err = err;
    g_send_steps.push_back(step);
}

static void ResetSendSeq()
{
    g_send_steps.clear();
    g_send_idx = 0;
    g_sent_bytes.clear();
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

/* 可实例化的 UBConnOpsBase 子类：记录钩子分发并按预置值返回 */
class TestConnOps : public UBConnOpsBase {
public:
    TestConnOps(ConnRole role, int fd) : UBConnOpsBase(role, fd, StorageAddr(), sizeof(struct sockaddr_in)) {}

    static struct sockaddr &StorageAddr()
    {
        static struct sockaddr_in addr = MakeTestAddr();
        return *reinterpret_cast<struct sockaddr *>(&addr);
    }

    ConnStageResult DoWaitTcpConnect() override
    {
        ++tcp_connect_calls;
        return tcp_connect_result;
    }

    ConnStageResult DoWaitNegotiate() override
    {
        ++negotiate_calls;
        return negotiate_result;
    }

    ConnStageResult DoWaitPeerRes() override
    {
        ++peer_res_calls;
        return peer_res_result;
    }

    ConnStageResult DoWaitPeerAck() override
    {
        ++peer_ack_calls;
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

    ConnStageResult tcp_connect_result = ConnStageResult::kFailed;
    ConnStageResult negotiate_result = ConnStageResult::kFailed;
    ConnStageResult peer_res_result = ConnStageResult::kFailed;
    ConnStageResult peer_ack_result = ConnStageResult::kFailed;
    int tcp_connect_calls = 0;
    int negotiate_calls = 0;
    int peer_res_calls = 0;
    int peer_ack_calls = 0;
    int done_calls = 0;
    int failed_calls = 0;
};

/* 最小 EventPoll 子类：EpollCtl 记录调用 */
class TestEventPoll : public EventPoll {
public:
    explicit TestEventPoll(int epoll_fd) : EventPoll(epoll_fd) {}

    int EpollCtl(int op, int fd, struct epoll_event *event) override
    {
        last_op = op;
        last_fd = fd;
        last_event = *event;
        ++ctl_calls;
        return 0;
    }

    int EpollWait(struct epoll_event *, int, int) override
    {
        return 0;
    }

    void WakeUpEpollFd() override {}

    int last_op = -1;
    int last_fd = -1;
    struct epoll_event last_event = {};
    int ctl_calls = 0;
};
} // namespace

class ConnOpsBaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        ResetSendSeq();
        LibcApi::Load();
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        LibcApi::UnLoad();
        LibcApi::Load();
        ResetSendSeq();
    }
};

/* ==================== 构造与基础属性 ==================== */

TEST_F(ConnOpsBaseTest, Construct_Client_StateWaitTcpConnectAndCloseOnFailureFalse)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(ConnState::kWaitTcpConnect, ops.State());
    EXPECT_FALSE(ops.CloseOnFailure());
    EXPECT_EQ(TEST_FD, ops.Fd());
    EXPECT_EQ(static_cast<socklen_t>(sizeof(struct sockaddr_in)), ops.AddrLen());
}

TEST_F(ConnOpsBaseTest, Construct_Server_StateWaitNegotiateAndCloseOnFailureTrue)
{
    TestConnOps ops(ConnRole::kServer, TEST_FD);
    EXPECT_EQ(ConnState::kWaitNegotiate, ops.State());
    EXPECT_TRUE(ops.CloseOnFailure());
    EXPECT_EQ(static_cast<uint64_t>(CONN_IDLE_TIMEOUT_MS), ops.IdleTimeoutMs());
}

TEST_F(ConnOpsBaseTest, StateName_ClientWaitNegotiate_ReturnsWaitNegotiateRsp)
{
    TestConnOps client(ConnRole::kClient, TEST_FD);
    EXPECT_STREQ("WaitTcpConnect", client.StateName());
    client.SetState(ConnState::kWaitNegotiate);
    EXPECT_STREQ("WaitNegotiateRsp", client.StateName());

    TestConnOps server(ConnRole::kServer, TEST_FD);
    EXPECT_STREQ("WaitNegotiateReq", server.StateName());
    server.SetState(ConnState::kConnEstablished);
    EXPECT_STREQ("ConnEstablished", server.StateName());
}

TEST_F(ConnOpsBaseTest, AbortAndFailureErrno_BasicFlow)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    EXPECT_FALSE(ops.IsAborted());
    ops.Abort();
    EXPECT_TRUE(ops.IsAborted());

    ops.SetFailureErrno(ETIMEDOUT);
    EXPECT_EQ(ETIMEDOUT, ops.FailureErrno());
}

TEST_F(ConnOpsBaseTest, Execute_DispatchesByState)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    ops.tcp_connect_result = ConnStageResult::kPending;
    ops.negotiate_result = ConnStageResult::kPending;
    ops.peer_res_result = ConnStageResult::kPending;

    EXPECT_EQ(ConnStageResult::kPending, ops.Execute(1000));
    EXPECT_EQ(1, ops.tcp_connect_calls);
    EXPECT_EQ(static_cast<uint64_t>(1000), ops.LastActiveMs());

    ops.SetState(ConnState::kWaitNegotiate);
    EXPECT_EQ(ConnStageResult::kPending, ops.Execute(2000));
    EXPECT_EQ(1, ops.negotiate_calls);

    ops.SetState(ConnState::kWaitPeerRes);
    EXPECT_EQ(ConnStageResult::kPending, ops.Execute(3000));
    EXPECT_EQ(1, ops.peer_res_calls);

    ops.SetState(ConnState::kWaitPeerAck);
    ops.peer_ack_result = ConnStageResult::kDone;
    EXPECT_EQ(ConnStageResult::kDone, ops.Execute(4000));
    EXPECT_EQ(1, ops.peer_ack_calls);
}

TEST_F(ConnOpsBaseTest, Execute_EstablishedOrFailed_ReturnsTerminal)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    ops.SetState(ConnState::kConnEstablished);
    EXPECT_EQ(ConnStageResult::kDone, ops.Execute(1000));
    EXPECT_EQ(0, ops.tcp_connect_calls);

    ops.Abort();
    ops.SetState(ConnState::kWaitNegotiate);
    EXPECT_EQ(ConnStageResult::kFailed, ops.Execute(1000));
    EXPECT_EQ(0, ops.negotiate_calls); /* aborted 直接失败，不再分发 */
}

/* ==================== 非阻塞发送 ==================== */

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_SendAll_ReturnsOk)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hello";
    LibcApi::send_ptr = MockSendSeq;
    PushSendOk(sizeof(data) - 1);

    size_t sent = 0;
    EXPECT_EQ(UBS_OK, ops.SendControlNonBlocking(data, sizeof(data) - 1, sent));
    EXPECT_EQ(sizeof(data) - 1, sent);
    EXPECT_EQ(0, memcmp(data, g_sent_bytes.data(), g_sent_bytes.size()));
}

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_SplitAcrossSends_ReturnsOk)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hello";
    LibcApi::send_ptr = MockSendSeq;
    PushSendOk(2);
    PushSendOk(3);

    size_t sent = 0;
    EXPECT_EQ(UBS_OK, ops.SendControlNonBlocking(data, 5, sent));
    EXPECT_EQ(static_cast<size_t>(5), sent);
}

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_Eagain_ReturnsWouldBlockWithPartialSent)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hello";
    LibcApi::send_ptr = MockSendSeq;
    PushSendOk(2);
    PushSendError(EAGAIN);

    size_t sent = 0;
    EXPECT_EQ(UBS_WOULD_BLOCK, ops.SendControlNonBlocking(data, 5, sent));
    EXPECT_EQ(static_cast<size_t>(2), sent);
    EXPECT_EQ(0, memcmp("he", g_sent_bytes.data(), 2));
}

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_EintrThenSendAll_ReturnsOk)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hi";
    LibcApi::send_ptr = MockSendSeq;
    PushSendError(EINTR);
    PushSendOk(2);

    size_t sent = 0;
    EXPECT_EQ(UBS_OK, ops.SendControlNonBlocking(data, 2, sent));
    EXPECT_EQ(static_cast<size_t>(2), sent);
}

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_SendZero_ReturnsTcpExchange)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hi";
    LibcApi::send_ptr = MockSendSeq;
    SendStep zero_step;
    zero_step.ret = 0;
    zero_step.err = 0;
    g_send_steps.push_back(zero_step);

    size_t sent = 0;
    EXPECT_EQ(UBS_TCP_EXCHANGE, ops.SendControlNonBlocking(data, 2, sent));
}

TEST_F(ConnOpsBaseTest, SendControlNonBlocking_SendError_ReturnsTcpExchange)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "hi";
    LibcApi::send_ptr = MockSendSeq;
    PushSendError(EPIPE);

    size_t sent = 0;
    EXPECT_EQ(UBS_TCP_EXCHANGE, ops.SendControlNonBlocking(data, 2, sent));
}

/* ==================== 暂存与续发 ==================== */

TEST_F(ConnOpsBaseTest, StageControl_CopiesFrameAndSetsWantsWritable)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "abcd";

    ops.StageControl(data, 4);
    EXPECT_FALSE(ops.pending_send_buf_.empty());
    EXPECT_TRUE(ops.WantsWritable());
    EXPECT_EQ(0, memcmp(data, ops.pending_send_buf_.data(), 4));
}

TEST_F(ConnOpsBaseTest, FlushPendingSend_Empty_ReturnsOk)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    EXPECT_EQ(UBS_OK, ops.FlushPendingSend());
}

TEST_F(ConnOpsBaseTest, FlushPendingSend_SendAll_ClearsPending)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "abcd";
    ops.StageControl(data, 4);
    LibcApi::send_ptr = MockSendSeq;
    PushSendOk(4);

    EXPECT_EQ(UBS_OK, ops.FlushPendingSend());
    EXPECT_TRUE(ops.pending_send_buf_.empty());
    EXPECT_FALSE(ops.WantsWritable());
}

TEST_F(ConnOpsBaseTest, FlushPendingSend_Eagain_KeepsOffsetAndWantsWritable)
{
    TestConnOps ops(ConnRole::kClient, TEST_FD);
    const char data[] = "abcd";
    ops.StageControl(data, 4);
    LibcApi::send_ptr = MockSendSeq;
    PushSendOk(1);
    PushSendError(EAGAIN);

    EXPECT_EQ(UBS_WOULD_BLOCK, ops.FlushPendingSend());
    EXPECT_FALSE(ops.pending_send_buf_.empty());
    EXPECT_TRUE(ops.WantsWritable());
    EXPECT_EQ(static_cast<size_t>(1), ops.pending_send_off_);

    /* 可写后重入续发剩余字节 */
    PushSendOk(3);
    EXPECT_EQ(UBS_OK, ops.FlushPendingSend());
    EXPECT_TRUE(ops.pending_send_buf_.empty());
    EXPECT_FALSE(ops.WantsWritable());
    EXPECT_EQ(0, memcmp("abcd", g_sent_bytes.data(), 4));
}

/* ==================== SocketBase 延迟 epoll 注册 ==================== */

class SocketPendingEpollTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        sock_ = MakeRef<urma::UrmaSocket>(TEST_FD);
        sock_->recv_buf_addr_ = 0;
        sock_->send_buf_addr_ = 0;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sock_ = nullptr;
    }

    urma::UrmaSocketPtr sock_;
};

TEST_F(SocketPendingEpollTest, DeferEpollAdd_Connecting_OnlyRecordsPending)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    sock_->state_ = SOCK_STAT_CONNECTING;
    epoll_data_t data;
    data.u64 = 0x1234;

    sock_->DeferEpollAdd(&ep, EPOLLIN | EPOLLOUT, data);
    EXPECT_EQ(0, ep.ctl_calls); /* 未注册内核 */
    EXPECT_EQ(EPOLLIN | EPOLLOUT, sock_->pending_epoll_events_);
    EXPECT_EQ(static_cast<EventPoll *>(&ep), sock_->pending_epoll_);
}

TEST_F(SocketPendingEpollTest, DeferEpollAdd_NotConnecting_RegistersImmediately)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    sock_->state_ = SOCK_STAT_INIT; /* 已切终态：双重检查自行消费 pending */
    epoll_data_t data;
    data.u64 = 0x5678;

    sock_->DeferEpollAdd(&ep, EPOLLIN, data);
    EXPECT_EQ(1, ep.ctl_calls);
    EXPECT_EQ(EPOLL_CTL_ADD, ep.last_op);
    EXPECT_EQ(TEST_FD, ep.last_fd);
    EXPECT_EQ(EPOLLIN, ep.last_event.events);
    EXPECT_EQ(static_cast<EventPoll *>(nullptr), sock_->pending_epoll_);
}

TEST_F(SocketPendingEpollTest, RegisterPendingEpollAdd_NoPending_NoOp)
{
    sock_->RegisterPendingEpollAdd(); /* pending_epoll_ == nullptr 直接返回 */
}

TEST_F(SocketPendingEpollTest, RegisterPendingEpollAdd_WithPending_RegistersAndClears)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    epoll_data_t data;
    data.u32 = 77;
    sock_->pending_epoll_ = &ep;
    sock_->pending_epoll_events_ = EPOLLIN | EPOLLET;
    sock_->pending_epoll_data_ = data;

    sock_->RegisterPendingEpollAdd();
    EXPECT_EQ(1, ep.ctl_calls);
    EXPECT_EQ(EPOLL_CTL_ADD, ep.last_op);
    EXPECT_EQ(TEST_FD, ep.last_fd);
    EXPECT_EQ(EPOLLIN | EPOLLET, static_cast<int>(ep.last_event.events));
    EXPECT_EQ(static_cast<EventPoll *>(nullptr), sock_->pending_epoll_);
    EXPECT_EQ(0u, sock_->pending_epoll_events_);
}

TEST_F(SocketPendingEpollTest, UpdatePendingEpoll_WithPending_UpdatesEvents)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    epoll_data_t data;
    data.u64 = 1;
    sock_->pending_epoll_ = &ep;
    sock_->pending_epoll_events_ = EPOLLIN;
    sock_->pending_epoll_data_ = data;

    epoll_data_t new_data;
    new_data.u64 = 2;
    sock_->UpdatePendingEpoll(EPOLLOUT, new_data);
    EXPECT_EQ(EPOLLOUT, sock_->pending_epoll_events_);
    EXPECT_EQ(static_cast<uint64_t>(2), sock_->pending_epoll_data_.u64);
}

TEST_F(SocketPendingEpollTest, ClearPendingEpoll_ResetsAll)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    epoll_data_t data;
    data.u64 = 3;
    sock_->pending_epoll_ = &ep;
    sock_->pending_epoll_events_ = EPOLLIN;
    sock_->pending_epoll_data_ = data;

    sock_->ClearPendingEpoll();
    EXPECT_EQ(static_cast<EventPoll *>(nullptr), sock_->pending_epoll_);
    EXPECT_EQ(0u, sock_->pending_epoll_events_);
}

TEST_F(SocketPendingEpollTest, NotifyConnectFailed_MergesEpollErrAndRegisters)
{
    TestEventPoll ep(TEST_EPOLL_FD);
    epoll_data_t data;
    data.u64 = 9;
    sock_->pending_epoll_ = &ep;
    sock_->pending_epoll_events_ = EPOLLIN | EPOLLOUT;
    sock_->pending_epoll_data_ = data;

    sock_->NotifyConnectFailed();
    EXPECT_EQ(1, ep.ctl_calls);
    EXPECT_EQ(EPOLLIN | EPOLLOUT | EPOLLERR, static_cast<int>(ep.last_event.events));
}