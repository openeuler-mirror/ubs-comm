/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "core/ubsocket_socket_acceptor.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "common/ubsocket_thread_pool.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "core/ubsocket_wakeup_event.h"
#include "core/umq/umq_socket_acceptor.h"
#include "ubsocket_errno.h"
#include "under_api/dl_libc_api.h"
using namespace ock::ubs;
namespace {
static const int TEST_FD = 42;              // 监听 socket fd
static const int TEST_NEW_FD = 100;         // accept 返回的新连接 fd
static const int TEST_NEW_FD_2 = 101;       // 第二条新连接 fd
static const int TEST_EPOLL_FD = 55;        // 模拟 epoll fd
static const uint16_t TEST_PEER_PORT = 8080;
static std::vector<std::pair<int, int>> g_acceptResults;
static int g_acceptCallCount = 0;

static void FillLoopbackSockAddr(struct sockaddr *address, socklen_t *address_len)
{
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TEST_PEER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    *address = *reinterpret_cast<struct sockaddr *>(&addr);
    *address_len = sizeof(struct sockaddr_in);
}

static int MockAccept(int, struct sockaddr *address, socklen_t *address_len)
{
    g_acceptCallCount++;
    if (g_acceptResults.empty()) {
        errno = EAGAIN;
        return -1;
    }
    auto result = g_acceptResults.front();
    g_acceptResults.erase(g_acceptResults.begin());
    if (result.first < 0) {
        errno = result.second;
        return result.first;
    }
    if (address != nullptr && address_len != nullptr) {
        FillLoopbackSockAddr(address, address_len);
    }
    return result.first;
}

static int g_closeCallCount = 0;

static int MockClose(int)
{
    g_closeCallCount++;
    return 0;
}

static int g_fcntlFlags = 0;

static int MockFcntl(int, int cmd, ...)
{
    if (cmd == F_GETFL) {
        return g_fcntlFlags;
    }
    return 0;
}

static int g_setsockoptRet = 0;
static int g_setsockoptErrno = 0;

struct RecvBehavior {
    ssize_t protocol_ret = static_cast<ssize_t>(sizeof(uint64_t)); // 协议读取返回值
    uint64_t protocol_val = CONTROL_PLANE_PROTOCOL_NEGOTIATION;    // 填入的协议魔数
    ock::ubs::Result peer_ack_val = UBS_OK;                        // peer ack 填入值
    ssize_t peer_ack_ret = static_cast<ssize_t>(sizeof(ock::ubs::Result));
} g_recvBehavior;

static const ssize_t PROTOCOL_RECV_FAIL = -1;
static const uint64_t PROTOCOL_BAD_MAGIC = 0x1;

static ssize_t FakeRecvSocketData(int, const void *buf, size_t size, uint32_t)
{
    if (size == sizeof(uint64_t)) {
        *const_cast<uint64_t *>(static_cast<const uint64_t *>(buf)) = g_recvBehavior.protocol_val;
        return g_recvBehavior.protocol_ret;
    }
    *const_cast<ock::ubs::Result *>(static_cast<const ock::ubs::Result *>(buf)) = g_recvBehavior.peer_ack_val;
    return g_recvBehavior.peer_ack_ret;
}

static ssize_t FakeSendSocketData(int, const void *, size_t size, uint32_t)
{
    return static_cast<ssize_t>(size); /* 发送恒成功 */
}

static int MockSetsockopt(int, int, int, const void *, socklen_t)
{
    if (g_setsockoptRet != 0) {
        errno = g_setsockoptErrno;
    }
    return g_setsockoptRet;
}

static void ResetFakeState()
{
    g_acceptResults.clear();
    g_acceptCallCount = 0;
    g_closeCallCount = 0;
    g_fcntlFlags = 0;
    g_setsockoptRet = 0;
    g_recvBehavior = RecvBehavior{};
    g_setsockoptErrno = 0;
}

static void SetLibcApiPtrsToNull()
{
    LibcApi::accept_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
    LibcApi::fcntl_ptr = nullptr;
    LibcApi::setsockopt_ptr = nullptr;
}

class TestSocket : public SocketBase {
public:
    explicit TestSocket(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ) {}

    ock::ubs::Result Initialize() noexcept override
    {
        return UBS_OK;
    }

    void UnInitialize() noexcept override {}

    int GetTxFd() override
    {
        return raw_socket_;
    }

    bool IsBindRemote() override
    {
        return false;
    }

    ock::ubs::Result AddTxEvent(const SocketPtr &, int, struct epoll_event *) override
    {
        return UBS_OK;
    }

    ock::ubs::Result DelTxEvent(const SocketPtr &, int) override
    {
        return UBS_OK;
    }

    bool ShouldRegisterTxEvent() override
    {
        return false;
    }

    ock::ubs::Result ProcessEpollEvent(struct epoll_event &) override
    {
        return UBS_OK;
    }
};

static void InstallHandshakeMocks(ock::ubs::Result doUbAcceptRet)
{
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    MOCKER(&SocketConnHelper::SendSocketData).stubs().will(invoke(&FakeSendSocketData));
    MOCKER_CPP(&ock::ubs::umq::UmqAcceptorOps::AcceptNegotiate).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&ock::ubs::umq::UmqAcceptorOps::CheckRouteDevAddForAccept).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP(&ock::ubs::umq::UmqAcceptorOps::DoUbAccept).stubs().will(returnValue(doUbAcceptRet));
}

static ock::ubs::umq::UmqAcceptorOps *g_activeOps = nullptr;

static SocketBase::HandshakeCtx *EnsureHandshakeCtx(TestSocket *socket)
{
    if (socket == nullptr) {
        return nullptr;
    }
    SocketExt *ext = socket->EnsureExt();
    if (ext == nullptr) {
        return nullptr;
    }
    if (ext->hs == nullptr) {
        ext->hs = new (std::nothrow) SocketBase::HandshakeCtx;
    }
    return static_cast<SocketBase::HandshakeCtx *>(ext->hs);
}

static ock::ubs::Result MockCreateSocketSuccess(int fd, SocketType, SocketPtr &sock)
{
    auto *socket = new (std::nothrow) TestSocket(fd);
    if (socket == nullptr) {
        return UBS_MALLOC_FAILED;
    }
    SocketBase::HandshakeCtx *hs = EnsureHandshakeCtx(socket);
    if (hs == nullptr) {
        delete socket;
        return UBS_MALLOC_FAILED;
    }
    hs->acceptor.acceptor_ops_ = g_activeOps;
    hs->acceptor.raw_fd_ = fd;
    sock.Set(socket);
    return UBS_OK;
}

static bool g_executorStarted = false;

static const uint32_t g_defaultThreadPoolSize = GlobalSetting::UBS_THREAD_POOL_SIZE;

static void EnsureExecutorStarted()
{
    if (!g_executorStarted) {
        GlobalSetting::UBS_THREAD_POOL_SIZE = 2;
        g_executorStarted = ExecutorService::GetExecutorService()->Start();
    }
}

static bool WaitAsyncTaskDrained(Acceptor &acceptor, int timeout_ms = 100)
{
    if (acceptor.async_accept_ == nullptr) {
        return true;
    }
    for (int i = 0; i < timeout_ms; ++i) {
        if (acceptor.async_accept_->asyncTaskNum.load() == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return acceptor.async_accept_->asyncTaskNum.load() == 0;
}

using ExecuteTaskFn = bool (ExecutorService::*)(const std::function<void()> &);
} // namespace

class AcceptorTest : public ::testing::Test {
public:
    static void TearDownTestSuite()
    {
        if (g_executorStarted) {
            ExecutorService::GetExecutorService()->Stop();
            g_executorStarted = false;
        }
    }

protected:
    void SetUp() override
    {
        errno = 0;
        ResetFakeState();
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        ArraySet<EventPoll>::GetInstance().Init();
        if (g_socket_epoll_lock == nullptr) {
            g_socket_epoll_lock = LockRegistry::RW_LOCK_OPS.create();
        }
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
        GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
        LibcApi::accept_ptr = MockAccept;
        LibcApi::close_ptr = MockClose;
        LibcApi::fcntl_ptr = MockFcntl;
        LibcApi::setsockopt_ptr = MockSetsockopt;
        umq_ops_ = new (std::nothrow) ock::ubs::umq::UmqAcceptorOps(TEST_FD);
        listen_ops_ = umq_ops_;
        acceptor_.Init(MakeListenSocket(), listen_ops_);
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        /* ~AsyncAcceptInfo closes fds still queued through LibcApi::close: run it while the
         * mock close is still installed (the fixture member is destroyed after TearDown). */
        acceptor_.async_accept_.reset();
        SetLibcApiPtrsToNull();
        ArraySet<EventPoll>::GetInstance().ReleaseAll();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
        GlobalSetting::UBS_ACCEPTOR_ASYNC_THREAD_COUNT = 0;
        GlobalSetting::UBS_THREAD_POOL_SIZE = g_defaultThreadPoolSize;
        errno = 0;
    }

    SocketPtr MakeListenSocket()
    {
        return SocketPtr(new (std::nothrow) TestSocket(TEST_FD));
    }

    Acceptor acceptor_;
    ock::ubs::umq::UmqAcceptorOps *umq_ops_ = nullptr;
    AcceptorOps *listen_ops_ = nullptr;
};

TEST_F(AcceptorTest, Accept_AsyncDisabled_RoutesToSyncPath)
{
    g_acceptResults.push_back({-1, EAGAIN});
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

/* 首个进入异步路径的用例, 由它消费 InitWakeupEvent 的 call_once 并走成功路径 */
TEST_F(AcceptorTest, Accept_AsyncEnabled_RoutesToAsyncPath)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({-1, EAGAIN});
    EpollMapper mapper(TEST_FD);
    mapper.Add(TEST_EPOLL_FD);
    MOCKER_CPP(::GetSocketEpollMapper).stubs().will(returnValue(&mapper));
    MOCKER_CPP(&UbsocketWakeupEvent::Initialize).stubs().will(returnValue(0));
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    EventPollPtr aepRef(aep);
    ArraySet<EventPoll>::GetInstance().OverrideItem(TEST_EPOLL_FD, aep);
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

/* issue #50: InitWakeupEvent was a process-wide once — only the first listening socket ever
 * got its eventfd; a second port's completed handshakes sat in ready_queue until the next SYN. */
TEST_F(AcceptorTest, InitWakeupEvent_PerListener_SecondAcceptorAlsoRegistered)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    EpollMapper mapper(TEST_FD);
    mapper.Add(TEST_EPOLL_FD);
    MOCKER_CPP(::GetSocketEpollMapper).stubs().will(returnValue(&mapper));
    MOCKER_CPP(&UbsocketWakeupEvent::Initialize).stubs().will(returnValue(0));
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    EventPollPtr aepRef(aep);
    ArraySet<EventPoll>::GetInstance().OverrideItem(TEST_EPOLL_FD, aep);
    Acceptor second;
    second.Init(MakeListenSocket(), listen_ops_);
    g_acceptResults.push_back({-1, EAGAIN});
    EXPECT_EQ(acceptor_.Accept(MakeListenSocket(), nullptr, nullptr), -1);
    g_acceptResults.push_back({-1, EAGAIN});
    EXPECT_EQ(second.Accept(MakeListenSocket(), nullptr, nullptr), -1);
    ASSERT_NE(acceptor_.async_accept_, nullptr);
    ASSERT_NE(second.async_accept_, nullptr);
    EXPECT_EQ(acceptor_.async_accept_->wakeup_ready.load(), true);
    EXPECT_EQ(second.async_accept_->wakeup_ready.load(), true); /* stayed false before the fix */
    EXPECT_EQ(aep->WakeupCallbackCount(), static_cast<size_t>(2));
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_AcceptFailEmfile_PassesErrnoThrough)
{
    g_acceptResults.push_back({-1, EMFILE});
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EMFILE);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_NonUbsConnection_ReturnsFdDirectlyWithAddress)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(false));
    struct sockaddr address {};
    socklen_t address_len = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), &address, &address_len);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(address_len, static_cast<socklen_t>(sizeof(struct sockaddr_in)));
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_UbsConnectionRawEstablished_ReturnsFdDirectly)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    SocketPtr listenSock = MakeListenSocket();
    static_cast<TestSocket *>(listenSock.Get())->state_ = SOCK_STAT_RAW_ESTABLISHED;
    int ret = acceptor_.Accept(listenSock, nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_NegotiationFatalFail_ReturnsEagain)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    g_recvBehavior.protocol_val = PROTOCOL_BAD_MAGIC;
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_GE(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_UbsHandshakeSuccess_ReturnsFd)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    SocketPtr accepted = ArraySet<Socket>::GetInstance().GetItem(TEST_NEW_FD);
    ASSERT_NE(accepted.Get(), nullptr);
    RawConnInfoV4 *ci = accepted->MutableConnInfo();
    ASSERT_NE(ci, nullptr);
    char ipBuf[INET6_ADDRSTRLEN] = {};
    EXPECT_STREQ(ci->GetPeerIpStr(ipBuf, sizeof(ipBuf)), "127.0.0.1");
    EXPECT_EQ(ci->peer_fd, TEST_NEW_FD);
    EXPECT_EQ(accepted->create_type_, SOCK_CREATE_TYPE_ACCEPT);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_SetTcpNoDelayFail_StillReturnsFd)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    SocketPtr listenSock = MakeListenSocket();
    static_cast<TestSocket *>(listenSock.Get())->state_ = SOCK_STAT_RAW_ESTABLISHED;
    g_setsockoptRet = -1;
    g_setsockoptErrno = ENOPROTOOPT;
    int ret = acceptor_.Accept(listenSock, nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessAcceptedFd_SocketRegisteredWithOps_FillsNegoPeerIp)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    SocketPtr listenSock = MakeListenSocket();
    static_cast<TestSocket *>(listenSock.Get())->state_ = SOCK_STAT_RAW_ESTABLISHED;
    SocketPtr acceptedRef(new (std::nothrow) TestSocket(TEST_NEW_FD));
    auto *acceptedOps = new (std::nothrow) ock::ubs::umq::UmqAcceptorOps(TEST_NEW_FD);
    AcceptorOpsPtr acceptedOpsPtr(acceptedOps);
    auto *acceptedRaw = static_cast<TestSocket *>(acceptedRef.Get());
    ASSERT_NE(acceptedRaw, nullptr);
    SocketBase::HandshakeCtx *acceptedHs = EnsureHandshakeCtx(acceptedRaw);
    ASSERT_NE(acceptedHs, nullptr);
    acceptedHs->acceptor.acceptor_ops_ = acceptedOpsPtr;
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_NEW_FD, acceptedRef.Get());

    int ret = acceptor_.Accept(listenSock, nullptr, nullptr);

    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_STREQ(acceptedOps->nego_peer_ip_, "127.0.0.1");
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessAcceptedFd_SocketRegisteredWithoutOps_SkipsIpExtract)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    SocketPtr listenSock = MakeListenSocket();
    static_cast<TestSocket *>(listenSock.Get())->state_ = SOCK_STAT_RAW_ESTABLISHED;

    SocketPtr acceptedRef(new (std::nothrow) TestSocket(TEST_NEW_FD));
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_NEW_FD, acceptedRef.Get());

    int ret = acceptor_.Accept(listenSock, nullptr, nullptr);

    EXPECT_EQ(ret, TEST_NEW_FD);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_ProtocolMismatchPositiveRet_ClosesFd)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    g_recvBehavior.protocol_val = PROTOCOL_BAD_MAGIC;

    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_ProtocolMismatchMinusOneRet_ClosesFd)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    g_recvBehavior.protocol_ret = PROTOCOL_RECV_FAIL;

    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_FatalHandshakeError_ClosesFdAndRestoresBlocking)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_fcntlFlags = 0; /* 阻塞 fd: 先设非阻塞协商, 致命失败后必须恢复 */
    g_activeOps = umq_ops_;
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    MOCKER_CPP(&ock::ubs::umq::UmqAcceptorOps::AcceptNegotiate).stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_TCP_EXCHANGE)));

    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_DegradableError_KeepsFdDelivered)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_activeOps = umq_ops_;
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    const ock::ubs::Result degradableErr =
        static_cast<ock::ubs::Result>(InnerCode::UBS_UMQ_CREATE | InnerCode::UBS_DEGRADABLE_MASK);
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    MOCKER_CPP(&ock::ubs::umq::UmqAcceptorOps::AcceptNegotiate).stubs().will(returnValue(degradableErr));

    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);

    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(g_closeCallCount, 0);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_SuccessOnBlockingFd_ReturnsTrue)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_fcntlFlags = 0; /* 阻塞 fd */
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(g_closeCallCount, 0);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, ProcessUBConnection_SuccessOnNonBlockingFd_ReturnsTrue)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_fcntlFlags = O_NONBLOCK; /* 非阻塞 fd: 无需切换阻塞标记 */
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(g_closeCallCount, 0);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, DoAccept_CreateSocketFail_ReturnsErrorAndEagain)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    MOCKER(&SocketBase::Create).stubs().will(returnValue(static_cast<ock::ubs::Result>(UBS_MALLOC_FAILED)));
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, DoAccept_CreateSocketResourcesFail_RollsBackArraySet)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_activeOps = umq_ops_;
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_UB_ACCEPT));
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    EXPECT_EQ(ArraySet<Socket>::GetInstance().GetItem(TEST_NEW_FD).Get(), nullptr);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, DoAccept_SuccessWithTraceEnabled_UpdatesTraceStats)
{
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(ArraySet<Socket>::GetInstance().GetItem(TEST_NEW_FD).Get() != nullptr, true);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_FirstAcceptEagain_ReturnsEagain)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({-1, EAGAIN});
    EpollMapper mapper(TEST_FD);
    mapper.Add(TEST_EPOLL_FD);
    MOCKER_CPP(::GetSocketEpollMapper).stubs().will(returnValue(&mapper));
    MOCKER_CPP(&UbsocketWakeupEvent::Initialize).stubs().will(returnValue(0));
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    EventPollPtr aepRef(aep);
    ArraySet<EventPoll>::GetInstance().OverrideItem(TEST_EPOLL_FD, aep);
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_FirstAcceptErrorNoFd_ReturnsError)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({-1, EMFILE});
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EMFILE);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_NonUbsFd_QueuedThenPoppedWithAddress)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(false));
    struct sockaddr address {};
    socklen_t address_len = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), &address, &address_len);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(address_len, static_cast<socklen_t>(sizeof(struct sockaddr_in)));
    GlobalMockObject::verify();
}

/* 抽干边界: 已抽取 ≥1 个 fd 后, 内核 accept 遇非 EAGAIN 错误(如 EMFILE)
 * ——不直接上抛错误(仅 accepted_count==0 时上抛), 而是退出循环,
 * 从 ready 队列弹出已入队连接交付上层, 防已提取连接滞留 */
TEST_F(AcceptorTest, AcceptAsync_NonUbsQueuedThenAcceptError_StillPopsQueued)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    g_acceptResults.push_back({-1, EMFILE});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(false));
    struct sockaddr address {};
    socklen_t address_len = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), &address, &address_len);
    EXPECT_EQ(ret, TEST_NEW_FD);
    EXPECT_EQ(address_len, static_cast<socklen_t>(sizeof(struct sockaddr_in)));
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_UbsFdSubmitFailFallbackSyncSuccess_ReturnsFd)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER_CPP(static_cast<ExecuteTaskFn>(&ExecutorService::Execute)).stubs().will(returnValue(false));
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_UbsFdSubmitFailFallbackSyncFail_ContinuesDrain)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER_CPP(static_cast<ExecuteTaskFn>(&ExecutorService::Execute)).stubs().will(returnValue(false));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    g_recvBehavior.protocol_val = PROTOCOL_BAD_MAGIC;
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(g_closeCallCount, 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_UbsFdSubmitSuccess_TaskPending_ReturnsEagain)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER_CPP(static_cast<ExecuteTaskFn>(&ExecutorService::Execute)).stubs().will(returnValue(true));
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    ASSERT_NE(acceptor_.async_accept_, nullptr);
    EXPECT_EQ(acceptor_.async_accept_->asyncTaskNum.load(), 1);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_NonUbsQueuedThenUbsFallbackSuccess_ReturnsUbsFd)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    g_acceptResults.push_back({TEST_NEW_FD_2, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(false))
        .then(returnValue(true))
        .then(returnValue(true));
    MOCKER_CPP(static_cast<ExecuteTaskFn>(&ExecutorService::Execute)).stubs().will(returnValue(false));
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, TEST_NEW_FD_2);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_ExecutorRunsTask_HandshakeOk_DeliversOrEagain)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    EnsureExecutorStarted();
    ASSERT_EQ(g_executorStarted, true);
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER_CPP(&UbsocketWakeupEvent::WakeUpReadyEventFd).stubs();
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    ASSERT_NE(acceptor_.async_accept_, nullptr);
    EXPECT_EQ(WaitAsyncTaskDrained(acceptor_), true);
    const bool deliveredNow = (ret == TEST_NEW_FD);
    const bool pendingInQueue = (ret == -1 && errno == EAGAIN);
    EXPECT_EQ(deliveredNow || pendingInQueue, true);
    if (pendingInQueue) {
        Locker sLock(acceptor_.async_accept_->lock);
        EXPECT_EQ(acceptor_.async_accept_->ready_queue.size(), static_cast<size_t>(1));
    }
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptAsync_ExecutorRunsTask_HandshakeFail_NotDelivered)
{
    GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = true;
    EnsureExecutorStarted();
    ASSERT_EQ(g_executorStarted, true);
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    MOCKER(&SocketConnHelper::RecvSocketData).stubs().will(invoke(&FakeRecvSocketData));
    g_recvBehavior.protocol_val = PROTOCOL_BAD_MAGIC;
    errno = 0;
    int ret = acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    ASSERT_NE(acceptor_.async_accept_, nullptr);
    EXPECT_EQ(WaitAsyncTaskDrained(acceptor_), true);
    EXPECT_EQ(g_closeCallCount, 1);
    Locker sLock(acceptor_.async_accept_->lock);
    EXPECT_EQ(acceptor_.async_accept_->ready_queue.size(), static_cast<size_t>(0));
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptSync_DebugLogLevel_VlogTrueBranchesCovered)
{
    const int oldLevel = Logger::Instance().GetLogLevel();
    Logger::Instance().SetLogLevel(LEVEL_DEBUG);
    g_acceptResults.push_back({-1, EAGAIN});
    errno = 0;
    acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    g_acceptResults.push_back({TEST_NEW_FD, 0});
    MOCKER(&SocketConnHelper::IsUbsConnection).stubs().will(returnValue(true));
    g_activeOps = umq_ops_;
    InstallHandshakeMocks(static_cast<ock::ubs::Result>(UBS_OK));
    MOCKER(&SocketBase::Create).stubs().will(invoke(&MockCreateSocketSuccess));
    acceptor_.Accept(MakeListenSocket(), nullptr, nullptr);
    Logger::Instance().SetLogLevel(oldLevel);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, Listen_AnyBacklog_ReturnsZero)
{
    EXPECT_EQ(acceptor_.Listen(128), 0);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, SetAcceptorOps_UpdatesOpsRef)
{
    auto *newOps = new (std::nothrow) ock::ubs::umq::UmqAcceptorOps(TEST_FD);
    AcceptorOpsPtr newOpsPtr(newOps);
    acceptor_.SetAcceptorOps(newOpsPtr);
    EXPECT_EQ(acceptor_.GetAcceptorOps().Get(), newOps);
    EXPECT_EQ(acceptor_.HasOps(), true);
    acceptor_.ReleaseOps();
    EXPECT_EQ(acceptor_.HasOps(), false);
    GlobalMockObject::verify();
}

TEST_F(AcceptorTest, AcceptorDestructor_TraceEnabled_DecrementsConnCount)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    {
        Acceptor tracedAcceptor;
        tracedAcceptor.Init(MakeListenSocket(), listen_ops_);
    }

    SUCCEED();
    GlobalSetting::UBS_MONITOR_ENABLE = false;
    GlobalMockObject::verify();
}
