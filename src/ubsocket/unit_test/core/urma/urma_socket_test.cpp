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
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <string>
#include <mockcpp/mockcpp.hpp>
#include <thread>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_logger.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_backend.h"
#include "core/urma/urma_data_rx_ops.h"
#include "core/urma/urma_data_tx_ops.h"
#include "core/urma/urma_setting.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_buf.h"
#include "core/urma/urma_socket_types.h"
#include "core/urma/urma_wrapper.h"
#include "iobuf/ubsocket_zcopy_adapter.h"
#include "under_api/dl_libc_api.h"
#include "under_api/urma/dl_urma_api.h"

#include "ubsocket_sock.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static char g_mock_buf[256 * 1024];
static UrmaJetty *dummy_jetty_ = nullptr; /* 文件级: 供匿名 namespace 助手函数共享 */

static urma_status_t MockUrmaPostSendWrSuccess(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_SUCCESS;
}
static urma_status_t MockUrmaPostRecvWrSuccess(urma_jetty_t *, urma_jfr_wr_t *, urma_jfr_wr_t **)
{
    return URMA_SUCCESS;
}
static void *MockDirectAllocate(size_t)
{
    return g_mock_buf;
}
static void MockDirectRelease(void *) {}
} // namespace

class UrmaSocketTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        UrmaSetting::Init();

        UrmaApi::urma_post_jetty_send_wr_ptr = MockUrmaPostSendWrSuccess;
        UrmaApi::urma_post_jetty_recv_wr_ptr = MockUrmaPostRecvWrSuccess;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();

        UrmaApi::urma_post_jetty_send_wr_ptr = nullptr;
        UrmaApi::urma_post_jetty_recv_wr_ptr = nullptr;
    }
};

TEST_F(UrmaSocketTest, Construct_Destruct_NoCrash)
{
    UrmaSocket sock(TEST_FD);
    EXPECT_EQ(TEST_FD, sock.Fd());
    EXPECT_EQ(SocketType::SOCK_TYPE_URMA, sock.Type());
}

TEST_F(UrmaSocketTest, Initialize_EventfdFail_ReturnsErrno)
{
    MOCKER(eventfd).stubs().will(returnValue(-1));
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_NE(nullptr, sock.Get());
    ock::ubs::Result ret = sock->Initialize();
    EXPECT_NE(UBS_OK, ret);
    GlobalMockObject::verify();
}

TEST_F(UrmaSocketTest, Initialize_Success_ReturnsOk)
{
    MOCKER_CPP(&UrmaIoBufferPool::direct_allocate).stubs().will(invoke(MockDirectAllocate));
    MOCKER_CPP(&UrmaIoBufferPool::direct_release).stubs().will(invoke(MockDirectRelease));
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    ASSERT_NE(nullptr, sock.Get());
    ock::ubs::Result ret = sock->Initialize();
    EXPECT_EQ(UBS_OK, ret);
    sock->UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UrmaSocketTest, IsBindRemote_DefaultFalse)
{
    UrmaSocket sock(TEST_FD);
    EXPECT_FALSE(sock.IsBindRemote());
}

TEST_F(UrmaSocketTest, SetBindRemoteFlag_IsBindRemoteReturnsTrue)
{
    UrmaSocket sock(TEST_FD);
    sock.SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    EXPECT_TRUE(sock.IsBindRemote());
}

TEST_F(UrmaSocketTest, WriteV_NullIov_ReturnsEinval)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    errno = 0;
    int ret = sock->WriteV(RefConvert<UrmaSocket, Socket>(sock), nullptr, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
    sock->UnInitialize();
}

TEST_F(UrmaSocketTest, WriteV_NotBound_ReturnsEnotconn)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    sock->Initialize();
    struct iovec iov;
    iov.iov_base = const_cast<char *>("test");
    iov.iov_len = 4;
    errno = 0;
    int ret = sock->WriteV(RefConvert<UrmaSocket, Socket>(sock), &iov, 1);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOTCONN, errno);
    sock->UnInitialize();
}

TEST_F(UrmaSocketTest, ReadV_NoRecvBuf_ReturnsEnotconn)
{
    auto sock = MakeRef<UrmaSocket>(TEST_FD);
    struct iovec iov;
    iov.iov_base = const_cast<char *>("");
    iov.iov_len = 4;
    errno = 0;
    int ret = sock->ReadV(RefConvert<UrmaSocket, Socket>(sock), &iov, 1);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(ENOTCONN, errno);
}

TEST_F(UrmaSocketTest, PeerVersion_SetAndGet)
{
    UrmaSocket sock(TEST_FD);
    sock.SetPeerVersion(0x040506);
    EXPECT_EQ(0x040506u, sock.GetPeerVersion());
}

TEST_F(UrmaSocketTest, FetchAndReachEpollEvents_ClearsOnlyGivenBits)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    urma_sock->SetReachEpollEvents(EPOLLIN | EPOLLOUT);

    auto old = urma_sock->FetchAndReachEpollEvents(EPOLLIN);

    EXPECT_NE(0u, old & EPOLLIN);
    EXPECT_NE(0u, old & EPOLLOUT);
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents() & EPOLLIN);
    EXPECT_NE(0u, urma_sock->GetReachEpollEvents() & EPOLLOUT);
}

TEST_F(UrmaSocketTest, FetchAndReachEpollEvents_CleanBit_ReturnsZeroAndNoop)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    urma_sock->SetReachEpollEvents(0);

    auto old = urma_sock->FetchAndReachEpollEvents(EPOLLIN);

    EXPECT_EQ(0u, old);
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents());
}

namespace {
/* counts eventfd_write calls so tests can assert NotifyClosed woke the app */
static int g_eventfd_write_count = 0;

static int MockEventfdWriteCounting(int fd, uint64_t value)
{
    (void)fd;
    (void)value;
    g_eventfd_write_count++;
    return 0;
}

static urma_cr_t MakeErrorCr(urma_cr_status_t status, uint64_t user_ctx, uint8_t s_r)
{
    urma_cr_t cr = {};
    cr.status = status;
    cr.user_ctx = user_ctx;
    cr.flag.bs.s_r = s_r;
    cr.opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    return cr;
}

/* 捕获 ERROR 级日志通道, 断言"硬件假 CQE"不污染该通道, 而真实错误仍会落盘 */
static int g_errLogCount = 0;
static std::string g_lastErrLog;

static void CaptureErrLog(int level, const char *msg, const char *filename, int line)
{
    (void)filename;
    (void)line;
    if (level >= LEVEL_ERR) {
        g_errLogCount++;
        g_lastErrLog = (msg != nullptr) ? msg : "";
    }
}

class ScopedErrLogCapture {
public:
    ScopedErrLogCapture()
    {
        g_errLogCount = 0;
        g_lastErrLog.clear();
        Logger::Instance().SetExternalLogFunction(CaptureErrLog);
    }

    ~ScopedErrLogCapture()
    {
        Logger::Instance().SetExternalLogFunction(nullptr);
    }

    ScopedErrLogCapture(const ScopedErrLogCapture &) = delete;
    ScopedErrLogCapture &operator=(const ScopedErrLogCapture &) = delete;
};
} // namespace

/* Step1 CQE error fallback (close-path gap G-C3): connection-level errors must
 * reach NotifyClosed so the app sees EPOLLHUP|EPOLLRDHUP; retriable errors stay
 * log-only. Receiver-side error CQEs carry SenderCRContext{CTRL_DATA_RECEiVE, 0, fd}
 * in user_ctx (as posted by empty_recv_wr_, HandleErrorCqe decodes the fd bitfield). */
TEST_F(UrmaSocketTest, HandleCqe_ConnectionError_ReceiverSide_TriggersNotifyClosed)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    auto cr = MakeErrorCr(URMA_CR_REM_ACCESS_ABORT_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, TEST_FD}.value, 1);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_TRUE(urma_sock->IsClosed());
    EXPECT_EQ(static_cast<uint32_t>(EPOLLHUP | EPOLLRDHUP), urma_sock->GetReachEpollEvents());
    EXPECT_EQ(1, g_eventfd_write_count);
    /* blocked readv/writev waiters must also be released (BLOCKING_FLAG_PEER_CLOSED) */
    EXPECT_NE(0u, urma_sock->block_flags_ & BLOCKING_FLAG_PEER_CLOSED);

    GlobalMockObject::verify();
}

/* Sender-side error CQEs carry a SenderCRContext in user_ctx; the fd field locates the socket */
TEST_F(UrmaSocketTest, HandleCqe_ConnectionError_SenderSide_TriggersNotifyClosed)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    SenderCRContext ctx{READ_DATA_REQUEST, 7, TEST_FD};
    auto cr = MakeErrorCr(URMA_CR_WR_FLUSH_ERR, ctx.value, 0);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_TRUE(urma_sock->IsClosed());
    EXPECT_EQ(static_cast<uint32_t>(EPOLLHUP | EPOLLRDHUP), urma_sock->GetReachEpollEvents());
    EXPECT_EQ(1, g_eventfd_write_count);

    GlobalMockObject::verify();
}

/* RNR (remote jfr has no buffer) is retriable: log only, socket stays open */
TEST_F(UrmaSocketTest, HandleCqe_RetriableError_DoesNotNotifyClosed)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    auto cr = MakeErrorCr(URMA_CR_RNR_RETRY_CNT_EXC_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, TEST_FD}.value, 1);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_FALSE(urma_sock->IsClosed());
    EXPECT_EQ(0u, urma_sock->GetReachEpollEvents());
    EXPECT_EQ(0, g_eventfd_write_count);

    GlobalMockObject::verify();
}

/* error CQE for an fd not in the ArraySet must not crash nor notify others */
TEST_F(UrmaSocketTest, HandleCqe_ConnectionError_UnknownFd_NoCrashNoNotify)
{
    static const int UNKNOWN_FD = 9000;
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    auto cr = MakeErrorCr(URMA_CR_ACK_TIMEOUT_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, UNKNOWN_FD}.value, 1);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);

    EXPECT_FALSE(urma_sock->IsClosed());
    EXPECT_EQ(0, g_eventfd_write_count);

    GlobalMockObject::verify();
}

/* a batch mixing connection-level and retriable errors notifies exactly once */
TEST_F(UrmaSocketTest, HandleCqe_MixedErrors_OnlyConnectionLevelNotifiesClosed)
{
    auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
    auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    urma_cr_t crs[2];
    crs[0] = MakeErrorCr(URMA_CR_REM_ACCESS_ABORT_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, TEST_FD}.value, 1);
    crs[1] = MakeErrorCr(URMA_CR_RNR_RETRY_CNT_EXC_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, TEST_FD}.value, 1);
    UrmaBackend::Instance().HandleCqe(0, 2, crs);

    EXPECT_TRUE(urma_sock->IsClosed());
    EXPECT_EQ(1, g_eventfd_write_count);

    GlobalMockObject::verify();
}

/* regression guard: a genuine (non-fake) error CQE must still reach the ERROR channel */
TEST_F(UrmaSocketTest, HandleErrorCqe_RealError_StillLoggedAsError)
{
    static const int UNKNOWN_FD = 9001;
    ScopedErrLogCapture capture;
    auto cr = MakeErrorCr(URMA_CR_ACK_TIMEOUT_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, UNKNOWN_FD}.value, 1);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
    EXPECT_GT(g_errLogCount, 0) << "real error CQE must remain visible";
}

/* ===================== CLOSE opcode (模拟 FIN) ===================== */

namespace {
/* 捕获 post 的 write_imm WR: 校验 CLOSE/SHUTDOWN 通知的 opcode */
static int g_postSendWrCalls = 0;
static uint64_t g_lastNotifyData = 0;
static uint8_t g_lastWrOpcode = 0;

static urma_status_t MockPostSendWrCapture(urma_jetty_t *, urma_jfs_wr_t *wr, urma_jfs_wr_t **)
{
    if (wr != nullptr) {
        g_postSendWrCalls++;
        g_lastNotifyData = wr->rw.notify_data;
        g_lastWrOpcode = static_cast<uint8_t>(wr->opcode);
    }
    return URMA_SUCCESS;
}

static urma_status_t MockPostSendWrFail(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **)
{
    return URMA_FAIL;
}

/* 构造 CLOSE 发送完成 CQE (pending-close hold 释放载体) */
static urma_cr_t MakeCloseSendCqe(uint64_t seq_no, int fd)
{
    urma_cr_t cr = {};
    cr.status = URMA_CR_SUCCESS;
    cr.flag.bs.s_r = 0; /* sender side */
    cr.opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    cr.user_ctx = SenderCRContext{CLOSE_NOTIFY, seq_no, fd}.value;
    return cr;
}

/* 构造 SHUTDOWN 发送完成 CQE: seq_no 字段携带 1KB 单元的 buffer offset (ctrl 占用释放载体) */
static urma_cr_t MakeShutdownSendCqe(uint64_t buf_offset_units, int fd)
{
    urma_cr_t cr = {};
    cr.status = URMA_CR_SUCCESS;
    cr.flag.bs.s_r = 0; /* sender side */
    cr.opcode = URMA_CR_OPC_WRITE_WITH_IMM;
    cr.user_ctx = SenderCRContext{SHUTDOWN_NOTIFY, buf_offset_units, fd}.value;
    return cr;
}

/* 给 socket 挂 dummy jetty (建链完成态) */
static void AttachDummyJetty(const UrmaSocketPtr &sock)
{
    dummy_jetty_ = static_cast<UrmaJetty *>(malloc(sizeof(UrmaJetty)));
    memset(dummy_jetty_, 0, sizeof(UrmaJetty));
    dummy_jetty_->ref_count_ = 1;
    sock->jetty_ = dummy_jetty_;
}

/* 构造完整建链 socket: send_buf/jetty (withJetty=true 时) 全部就位 */
struct CloseTestEnv {
    UrmaSocketPtr sock;
    static constexpr uint32_t SEND_BUF_SIZE = 8192;
};

static CloseTestEnv MakeBoundCloseEnv(bool withJetty = true, int fd = TEST_FD)
{
    CloseTestEnv env;
    env.sock = MakeRef<UrmaSocket>(fd);
    env.sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
    env.sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_mock_buf);
    env.sock->send_buf_size_ = CloseTestEnv::SEND_BUF_SIZE;
    env.sock->send_buf_ = new UrmaSockRingBuf(CloseTestEnv::SEND_BUF_SIZE);
    env.sock->rmt_recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_mock_buf);
    if (withJetty) {
        AttachDummyJetty(env.sock);
    }
    return env;
}

/* 复位 post 钩子与 dummy jetty, 并清空 pending-close 注册表 (map 清空, 无副作用) */
static void DestroyCloseEnv(CloseTestEnv &env)
{
    UrmaApi::urma_post_jetty_send_wr_ptr = MockUrmaPostSendWrSuccess;
    env.sock->jetty_ = nullptr; /* 防 UnInitialize 走 dummy jetty Destroy */
    free(dummy_jetty_);
    dummy_jetty_ = nullptr;
    UrmaBackend::Instance().ClearPendingClose();
}

struct LoopbackTcpPair {
    int conn = -1;
    int peer = -1;
};

static LoopbackTcpPair MakeLoopbackTcpPair()
{
    LoopbackTcpPair pair;
    const int lfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (lfd < 0) {
        return pair;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* 内核分配临时端口 */
    if (::bind(lfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(lfd, 1) != 0) {
        ::close(lfd);
        return pair;
    }
    socklen_t addrlen = sizeof(addr);
    if (::getsockname(lfd, reinterpret_cast<struct sockaddr *>(&addr), &addrlen) != 0) {
        ::close(lfd);
        return pair;
    }
    pair.conn = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (pair.conn < 0 ||
        ::connect(pair.conn, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        if (pair.conn >= 0) {
            ::close(pair.conn);
            pair.conn = -1;
        }
        ::close(lfd);
        return pair;
    }
    pair.peer = ::accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC);
    ::close(lfd);
    return pair;
}

/* 用例收尾: 关闭仍存活的 fd (backend 已 close 的 fd 返回 EBADF, 无害;
 * 期间无新 fd 分配, 不存在号复用误关风险) */
static void CloseLoopbackTcpPair(LoopbackTcpPair &pair)
{
    if (pair.conn >= 0) {
        ::close(pair.conn);
        pair.conn = -1;
    }
    if (pair.peer >= 0) {
        ::close(pair.peer);
        pair.peer = -1;
    }
}

/* 记录 libc setsockopt/getsockopt 透传调用, 供 wrapper 拦截测试断言 */
static int g_libcSetsockoptCalls = 0;
static int g_libcGetsockoptCalls = 0;

static int MockLibcSetsockoptPassthrough(int, int, int, const void *, socklen_t)
{
    g_libcSetsockoptCalls++;
    return 0;
}

static int MockLibcGetsockoptPassthrough(int, int, int, void *, socklen_t *)
{
    g_libcGetsockoptCalls++;
    return 0;
}

/* 记录 libc shutdown 透传调用, 供 wrapper 入口拦截测试断言 */
static int g_libcShutdownCalls = 0;
static int MockLibcShutdownPassthrough(int, int)
{
    g_libcShutdownCalls++;
    return 0;
}
} // namespace

/* 对端关闭语义: CLOSE opcode → PeerCloseState::CLOSED + HUP 注入+唤醒阻塞读; 连接级错误 CQE → RESET */
TEST_F(UrmaSocketTest, HandleCqe_PeerCloseSemantics)
{
    /* graceful: 对端 CLOSE opcode 到达 */
    {
        SCOPED_TRACE("case: close_opcode_marks_peer_closed");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);

        g_eventfd_write_count = 0;
        MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

        WriteImmData imm;
        imm.ubsocket.opcode = CLOSE;
        imm.ubsocket.buffer_offset = 1;
        urma_sock->HandleRecvCqe(imm);

        EXPECT_TRUE(urma_sock->IsPeerClosed());
        EXPECT_FALSE(urma_sock->IsPeerReset());
        EXPECT_TRUE(urma_sock->IsClosed()); /* HUP 位持久 (NotifyClosed 注入) */
        EXPECT_EQ(static_cast<uint32_t>(EPOLLHUP | EPOLLRDHUP), urma_sock->GetReachEpollEvents());
        EXPECT_EQ(1, g_eventfd_write_count);
        EXPECT_NE(0u, urma_sock->block_flags_ & BLOCKING_FLAG_PEER_CLOSED);

        GlobalMockObject::verify();
    }
    /* abnormal: 连接级错误 CQE 置 RESET 而非 CLOSED */
    {
        SCOPED_TRACE("case: connection_error_marks_peer_reset");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
        ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

        g_eventfd_write_count = 0;
        MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

        auto cr = MakeErrorCr(URMA_CR_REM_ACCESS_ABORT_ERR, SenderCRContext{CTRL_DATA_RECEiVE, 0, TEST_FD}.value, 1);
        UrmaBackend::Instance().HandleCqe(0, 1, &cr);

        EXPECT_TRUE(urma_sock->IsPeerReset());
        EXPECT_FALSE(urma_sock->IsPeerClosed());
        EXPECT_TRUE(urma_sock->IsClosed());

        GlobalMockObject::verify();
    }
}

/* 本端 close 行为矩阵: linger 配置 × 在途占用 × drain 释放时机 → post 次数/耗时/hold 保留 */
TEST_F(UrmaSocketTest, PostCloseToPeer_Behavior)
{
    struct CloseBehaviorCase {
        const char *name;
        bool use_timeout_arg;    /* true: 显式 timeout_ms 覆盖 linger; false: 走 linger 影子配置 */
        int timeout_ms;
        bool abortive_linger;    /* 设置 linger{1,0} */
        int positive_linger_s;   /* >0: 设置 linger{1,N} */
        uint32_t in_flight_bytes;
        uint32_t release_delay_us; /* >0: 启动 releaser 线程, 延迟后释放占用 */
        bool deliver_close_cqe;    /* releaser 释放后再投递 CLOSE CQE */
        int expect_post_count;
        bool expect_close_imm;     /* 断言 WR opcode 与 imm 携带的 CLOSE opcode */
        int64_t max_elapsed_ms;    /* >0: 断言耗时上限 (不 drain) */
        int expect_hold;           /* -1: 不校验; 0/1: HasPendingClose 期望值 */
    };
    static const CloseBehaviorCase cases[] = {
        /* 已建链+jetty 且无在途占用: 立即 post CLOSE write_imm */
        {"bound_posts_close_imm", true, 0, false, 0, 0, 0, false, 1, true, -1, -1},
        /* 有在途占用: 限时 drain, 对端 ACK 释放后 post */
        {"in_flight_posts_after_drain", true, 200, false, 0, 1024, 10 * 1000, false, 1, true, -1, -1},
        /* 在途占用永不释放: 10ms 超时兜底后仍尽力 post (不无限阻塞 close) */
        {"drain_timeout_posts_anyway", true, 10, false, 0, 1024, 0, false, 1, false, -1, -1},
        /* l_onoff=0 默认: 不等待 drain, 立即 post; hold 由异步 CQE 释放 */
        {"default_linger_posts_immediately", false, 0, false, 0, 1024, 0, false, 1, false, 100, 1},
        /* l_onoff=1,l_linger=0 abortive: 跳过 drain 与 CLOSE post (对齐 TCP RST) */
        {"abortive_linger_skips_post", false, 0, true, 0, 0, 0, false, 0, false, 100, -1},
        /* l_onoff=1,l_linger>0: drain 排空后 post, CLOSE CQE 到达释放 hold */
        {"positive_linger_drains_then_posts", false, 0, false, 1, 1024, 10 * 1000, true, 1, false, -1, 0},
    };

    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        auto env = MakeBoundCloseEnv();
        if (c.abortive_linger) {
            struct linger abortive = {1, 0};
            ASSERT_EQ(0, env.sock->SetLingerOpt(abortive));
        } else if (c.positive_linger_s > 0) {
            struct linger positive = {1, c.positive_linger_s};
            ASSERT_EQ(0, env.sock->SetLingerOpt(positive));
        }

        uint64_t inflight_off = 0;
        UrmaSockRingBuf *send_buf = nullptr;
        if (c.in_flight_bytes > 0) {
            ASSERT_TRUE(env.sock->send_buf_->Allocate(c.in_flight_bytes, inflight_off));
            send_buf = env.sock->send_buf_.Get();
        }
        std::thread releaser;
        if (c.release_delay_us > 0) {
            const uint32_t delay_us = c.release_delay_us;
            const uint32_t bytes = c.in_flight_bytes;
            const bool deliver = c.deliver_close_cqe;
            releaser = std::thread([send_buf, inflight_off, delay_us, bytes, deliver]() {
                usleep(delay_us); /* 对端 ACK 到达, 释放在途占用 */
                send_buf->Release(inflight_off, bytes);
                if (deliver) {
                    usleep(5 * 1000); /* CLOSE WR 上屏, 发送 CQE 到达释放 hold */
                    auto cr = MakeCloseSendCqe(0, TEST_FD);
                    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
                }
            });
        }

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

        const auto start = std::chrono::steady_clock::now();
        auto ret = c.use_timeout_arg ? env.sock->PostCloseToPeer(c.timeout_ms) : env.sock->PostCloseToPeer();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (releaser.joinable()) {
            releaser.join();
        }

        /* post 成功 → dup3 占位 fd 号成功 (dup3 不要求 newfd 已打开) → true;
         * abortive/跳过 → false (fd 仍归调用方) */
        EXPECT_EQ(c.expect_post_count > 0, ret);
        EXPECT_EQ(c.expect_post_count, g_postSendWrCalls);
        if (c.expect_close_imm) {
            EXPECT_EQ(URMA_OPC_WRITE_IMM, static_cast<int>(g_lastWrOpcode));
            WriteImmData imm{g_lastNotifyData};
            EXPECT_EQ(CLOSE, imm.ubsocket.opcode); /* imm 携带 CLOSE opcode */
        }
        if (c.max_elapsed_ms > 0) {
            EXPECT_LT(elapsed_ms, c.max_elapsed_ms);
        }
        if (c.expect_hold >= 0) {
            EXPECT_EQ(c.expect_hold == 1, UrmaBackend::Instance().HasPendingClose(TEST_FD));
        }

        DestroyCloseEnv(env);
    }
}

/* 跳过条件: 未建链 / 无 jetty / 对端已关 — 均不 post, 幂等安全 */
TEST_F(UrmaSocketTest, PostCloseToPeer_SkipConditions_NoPost)
{
    g_postSendWrCalls = 0;
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

    /* case 1: 未建链 (URMA_SOCKET_MASK_BIND_REMOTE 默认未置位) */
    {
        SCOPED_TRACE("case: not_bound");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        EXPECT_FALSE(urma_sock->PostCloseToPeer(0));
    }

    /* case 2: 已建链但 jetty 为空 (对端先关/资源已销毁) */
    {
        SCOPED_TRACE("case: bound_without_jetty");
        auto env = MakeBoundCloseEnv(false);
        EXPECT_FALSE(env.sock->PostCloseToPeer(0));
    }

    /* case 3: 对端已发送 CLOSE (无需再通知) */
    {
        SCOPED_TRACE("case: peer_already_closed");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->SetSocketFlags(URMA_SOCKET_MASK_BIND_REMOTE);
        urma_sock->send_buf_ = new UrmaSockRingBuf(8192);
        WriteImmData imm;
        imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(imm);
        EXPECT_FALSE(urma_sock->PostCloseToPeer(0));
    }

    EXPECT_EQ(0, g_postSendWrCalls);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockUrmaPostSendWrSuccess;
}

/* reset 边界修复: 连接级错误置 RESET 后, PostCloseToPeer 应立即跳过 —
 * 不 drain 空等 500ms (在途占用无人释放), 也不向已断链的对端 post CLOSE */
TEST_F(UrmaSocketTest, PostCloseToPeer_PeerReset_SkipsImmediatelyWithoutDrain)
{
    auto env = MakeBoundCloseEnv();

    /* 在途占用永不释放: 未修复时会空等满 timeout (500ms) */
    uint64_t inflight_off = 0;
    ASSERT_TRUE(env.sock->send_buf_->Allocate(1024, inflight_off));

    g_postSendWrCalls = 0;
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
    MOCKER(::eventfd_write).stubs().will(returnValue(0)); /* OnPeerReset → NotifyClosed 路径 */

    env.sock->OnPeerReset(); /* 模拟连接级错误 CQE */

    const auto start = std::chrono::steady_clock::now();
    auto ret = env.sock->PostCloseToPeer(500);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_FALSE(ret); /* 跳过视为成功 (fd 仍归调用方) */
    EXPECT_EQ(0, g_postSendWrCalls);              /* 不向已断链对端 post */
    EXPECT_LT(elapsed_ms, 200);                   /* 立即返回, 不 drain 空等 */

    DestroyCloseEnv(env);
}

/* ===================== SO_LINGER: URMA 关闭行为配置 ===================== */

/* linger 语义: 私有静态换算 (abortive / drain 毫秒) + SetLingerOpt/GetLingerOpt 虚接口 roundtrip */
TEST_F(UrmaSocketTest, Linger_MappingAndRoundtrip)
{
    /* 私有静态换算: abortive 判定 与 drain 超时毫秒 */
    {
        SCOPED_TRACE("case: abortive_and_drain_timeout_mapping");
        struct linger off = {0, 7};
        struct linger abortive = {1, 0};
        struct linger positive = {1, 2};

        EXPECT_FALSE(UrmaSocket::IsAbortiveLinger(off));
        EXPECT_TRUE(UrmaSocket::IsAbortiveLinger(abortive));
        EXPECT_FALSE(UrmaSocket::IsAbortiveLinger(positive));

        EXPECT_EQ(0U, UrmaSocket::LingerToDrainTimeoutMs(off));         /* l_onoff=0: 不等待 */
        EXPECT_EQ(0U, UrmaSocket::LingerToDrainTimeoutMs(abortive));    /* abortive 不走 drain */
        EXPECT_EQ(2000U, UrmaSocket::LingerToDrainTimeoutMs(positive)); /* l_linger 秒 → 毫秒 */
    }
    /* 经基类虚接口 roundtrip (URMA 覆写返回 0) */
    {
        SCOPED_TRACE("case: set_get_roundtrip_via_virtual");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        Socket *base = urma_sock.Get();

        struct linger in = {1, 7};
        EXPECT_EQ(0, base->SetLingerOpt(in));

        struct linger out = {0, 0};
        EXPECT_EQ(0, base->GetLingerOpt(out));
        EXPECT_EQ(1, out.l_onoff);
        EXPECT_EQ(7, out.l_linger);
    }
}

/* setsockopt/getsockopt wrapper: SO_LINGER 影子存取 / optlen 校验 / 非管理 fd 与 native TCP 透传 */
TEST_F(UrmaSocketTest, SetSockOpt_SO_LINGER)
{
    struct SockOptLingerCase {
        const char *name;
        bool unmanaged_fd;
        bool native_tcp;
        socklen_t optlen;
        int expect_set_rc;
        int expect_set_errno;      /* -1: 不校验 errno */
        int expect_libc_set_calls; /* libc setsockopt 透传次数 */
        bool do_getsockopt;
        int expect_get_rc;
        int expect_get_errno;     /* -1: 不校验 errno */
        bool expect_shadow_value; /* 断言回读影子值 {1,7} 与 outlen */
    };
    static const SockOptLingerCase cases[] = {
        /* 管理 fd: 拦截影子存取并回读 */
        {"urma_socket_stored_and_returned", false, false, sizeof(struct linger), 0, -1, 0, true, 0, -1, true},
        /* optlen 不足 sizeof(struct linger) → EINVAL, 不落 libc */
        {"invalid_optlen_einval", false, false, 2, -1, EINVAL, 0, false, 0, -1, false},
        /* 非管理 fd: setsockopt 透传 libc; getsockopt 对非管理 fd 返回 EBADF (既有行为) */
        {"unmanaged_fd_passthrough_libc", true, false, sizeof(struct linger), 0, -1, 1, true, -1, EBADF, false},
        /* native TCP 模式: 不拦截, 直接透传 libc */
        {"native_tcp_mode_passthrough_libc", false, true, sizeof(struct linger), 0, -1, 1, false, 0, -1, false},
    };

    (void)LibcApi::Load(); /* 确保 ptr 非 null, 再替换为记录型 mock */
    auto *orig_set = LibcApi::setsockopt_ptr;
    auto *orig_get = LibcApi::getsockopt_ptr;
    LibcApi::setsockopt_ptr = MockLibcSetsockoptPassthrough;
    LibcApi::getsockopt_ptr = MockLibcGetsockoptPassthrough;

    static const int UNMANAGED_FD = 9100;
    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        const int fd = c.unmanaged_fd ? UNMANAGED_FD : TEST_FD;
        GlobalSetting::UBS_NATIVE_TCP_MODE = c.native_tcp;

        UrmaSocketPtr urma_sock;
        if (!c.unmanaged_fd) {
            urma_sock = MakeRef<UrmaSocket>(TEST_FD);
            auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
            ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());
        }

        struct linger in = {1, 7};
        g_libcSetsockoptCalls = 0;
        g_libcGetsockoptCalls = 0;
        errno = 0;
        EXPECT_EQ(c.expect_set_rc, ubsocket_setsockopt(fd, SOL_SOCKET, SO_LINGER, &in, c.optlen));
        if (c.expect_set_errno >= 0) {
            EXPECT_EQ(c.expect_set_errno, errno);
        }
        EXPECT_EQ(c.expect_libc_set_calls, g_libcSetsockoptCalls);

        if (c.do_getsockopt) {
            struct linger out = {};
            socklen_t outlen = sizeof(out);
            errno = 0;
            EXPECT_EQ(c.expect_get_rc, ubsocket_getsockopt(fd, SOL_SOCKET, SO_LINGER, &out, &outlen));
            if (c.expect_get_errno >= 0) {
                EXPECT_EQ(c.expect_get_errno, errno);
            }
            if (c.expect_shadow_value) {
                EXPECT_EQ(1, out.l_onoff);
                EXPECT_EQ(7, out.l_linger);
                EXPECT_EQ(sizeof(struct linger), static_cast<size_t>(outlen));
            }
        }

        if (!c.unmanaged_fd) {
            ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, nullptr);
        }
        GlobalSetting::UBS_NATIVE_TCP_MODE = false;
    }

    LibcApi::setsockopt_ptr = orig_set;
    LibcApi::getsockopt_ptr = orig_get;
}

/* ============ pending-close hold: CLOSE WR 上屏前 hold 住 socket ============ */

/* hold 释放路径矩阵: CLOSE 成功 CQE / 错误 CQE / socket 仍被 ArraySet 持有时 CQE 释放 */
TEST_F(UrmaSocketTest, PendingCloseHold_ReleasePaths)
{
    struct HoldReleaseCase {
        const char *name;
        bool error_cqe;     /* true: 错误 CQE 释放; false: CLOSE 成功 CQE 释放 */
        bool array_set_hit; /* true: socket 仍在 ArraySet 中 */
    };
    static const HoldReleaseCase cases[] = {
        {"close_cqe_releases_hold", false, false},
        {"error_cqe_releases_hold", true, false},
        {"array_set_hit_cqe_still_releases_hold", false, true},
    };

    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        auto env = MakeBoundCloseEnv();
        if (c.array_set_hit) {
            auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
            ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());
        }

        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        g_postSendWrCalls = 0;

        ASSERT_TRUE(env.sock->PostCloseToPeer()); /* post 成功, fd 号已被占位接管 */
        EXPECT_EQ(1, g_postSendWrCalls);
        /* 注册表持有: fd 仍可从 pending 表查到 */
        EXPECT_TRUE(UrmaBackend::Instance().HasPendingClose(TEST_FD));

        if (c.error_cqe) {
            /* CLOSE WR 错误 CQE (flush): 连接已断, 直接释放 */
            auto cr = MakeErrorCr(URMA_CR_WR_FLUSH_ERR, SenderCRContext{CLOSE_NOTIFY, 0, TEST_FD}.value, 0);
            UrmaBackend::Instance().HandleCqe(0, 1, &cr);
        } else {
            auto cr = MakeCloseSendCqe(0, TEST_FD);
            UrmaBackend::Instance().HandleCqe(0, 1, &cr);
        }
        EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(TEST_FD));

        if (c.array_set_hit) {
            ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, nullptr);
        }
        DestroyCloseEnv(env);
    }
}

/* linger{1,N} 阻塞 close: 等到 CLOSE CQE 上屏才放行; CQE 丢失则超时返回, 注册表项由清扫兜底 */
TEST_F(UrmaSocketTest, PendingCloseHold_CqeWaitAndTimeout)
{
    /* 等到了 CQE 才返回 (≥10ms), 且已释放 */
    {
        SCOPED_TRACE("case: waits_for_close_cqe_with_linger");
        auto env = MakeBoundCloseEnv();
        struct linger positive = {1, 1};
        ASSERT_EQ(0, env.sock->SetLingerOpt(positive));

        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        g_postSendWrCalls = 0;

        /* 异步投递 CLOSE CQE: 10ms 后到达 (模拟 WR 上屏) */
        std::thread cqe_deliverer([]() {
            usleep(10 * 1000);
            auto cr = MakeCloseSendCqe(0, TEST_FD);
            UrmaBackend::Instance().HandleCqe(0, 1, &cr);
        });

        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(env.sock->PostCloseToPeer()); /* post 成功, fd 号已被占位接管 */
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        cqe_deliverer.join();

        EXPECT_GE(elapsed_ms, 10);
        EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(TEST_FD));

        DestroyCloseEnv(env);
    }
    /* 不投递 CQE (模拟丢失); 显式 200ms 等待上限避免用例超时 */
    {
        SCOPED_TRACE("case: timeout_releases_hold");
        auto env = MakeBoundCloseEnv();

        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        g_postSendWrCalls = 0;

        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(env.sock->PostCloseToPeer(200)); /* post 成功, fd 号已被占位接管 */
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        /* 超时返回但仍在注册表 (持有待清扫), ClearPendingClose 模拟清扫/UnInit 兜底 */
        EXPECT_GE(elapsed_ms, 180);
        EXPECT_LT(elapsed_ms, 1000);
        EXPECT_TRUE(UrmaBackend::Instance().HasPendingClose(TEST_FD));
        UrmaBackend::Instance().ClearPendingClose();
        EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(TEST_FD));

        DestroyCloseEnv(env);
    }
}

/* ============ fd placeholder (dup3 /dev/null 占位): hold 窗口内 fd 号不被复用 ============ */

/* 占位核心契约: ubsocket_close 返回后 fd 号不进内核复用池 — 已被换成 /dev/null 字符设备,
 * 直至 CLOSE 发送 CQE 释放 hold 才 close(fd) 归还内核。防止陈旧 CQE 误路由到复用 fd。 */
TEST_F(UrmaSocketTest, Close_HoldsFdNumberUntilCloseCqe)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);
    auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
    ArraySet<Socket>::GetInstance().OverrideItem(pair.conn, sock.Get());

    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
    g_postSendWrCalls = 0;

    ASSERT_EQ(0, ubsocket_close(pair.conn));

    /* CLOSE WR 已 post, hold 挂起 (发送 CQE 未到): fd 号必须仍被占用 */
    EXPECT_EQ(1, g_postSendWrCalls);
    EXPECT_TRUE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    struct stat st = {};
    errno = 0;
    const int fstatRet = fstat(pair.conn, &st);
    char byte = 0;
    errno = 0;
    const ssize_t readRet = read(pair.conn, &byte, 1); /* /dev/null: 立即 EOF(0), 不阻塞 */

    EXPECT_EQ(0, fstatRet) << "fd number must stay open during the hold window";
    EXPECT_TRUE(S_ISCHR(st.st_mode)) << "fd must be swapped to /dev/null placeholder, not closed";
    EXPECT_EQ(0, readRet);

    DestroyCloseEnv(env); /* ClearPendingClose 兜底: 释放 hold 并归还 fd 号 (UnInit 路径) */
    CloseLoopbackTcpPair(pair);
}

/* CLOSE 发送 CQE 到达 → 释放 hold 并 close(fd) 归还 fd 号: 占位生命周期闭合 */
TEST_F(UrmaSocketTest, CloseCqe_ReleasesPlaceholder)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

    ASSERT_TRUE(env.sock->PostCloseToPeer(0)); /* post 成功 + fd 被占位接管 */
    EXPECT_TRUE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    struct stat st = {};
    errno = 0;
    EXPECT_EQ(0, fstat(pair.conn, &st));
    EXPECT_TRUE(S_ISCHR(st.st_mode)) << "hold window must keep fd occupied by /dev/null placeholder";

    auto cr = MakeCloseSendCqe(0, pair.conn);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
    EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    errno = 0;
    EXPECT_EQ(-1, fcntl(pair.conn, F_GETFD));
    EXPECT_EQ(EBADF, errno) << "release must close(fd) so the number returns to the kernel pool";

    DestroyCloseEnv(env);
    CloseLoopbackTcpPair(pair);
}

/* UnInit 兜底路径 (UT 中 UrmaBackend::UnInit 因 inited_=false 为 no-op, 以其调用的
 * ClearPendingClose 等价驱动): 清扫释放 hold 时必须连带 close 占位 fd, 不泄漏 fd 号 */
TEST_F(UrmaSocketTest, UnInit_ClosesAllPlaceholders)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

    ASSERT_TRUE(env.sock->PostCloseToPeer(0));
    EXPECT_TRUE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    UrmaBackend::Instance().ClearPendingClose();
    EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    errno = 0;
    EXPECT_EQ(-1, fcntl(pair.conn, F_GETFD));
    EXPECT_EQ(EBADF, errno) << "sweep/uninit must close placeholder fd, not just drop the map entry";

    DestroyCloseEnv(env);
    CloseLoopbackTcpPair(pair);
}

/* 守卫: post 失败回滚 hold — 不占位, fd 保留原生 TCP socket, 调用方 close 语义不受影响 */
TEST_F(UrmaSocketTest, PostFailure_NoPlaceholder)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrFail;

    EXPECT_FALSE(env.sock->PostCloseToPeer(0)); /* post 失败: fd 仍归调用方 */
    EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(pair.conn)); /* post 失败已回滚 */

    struct stat st = {};
    errno = 0;
    EXPECT_EQ(0, fstat(pair.conn, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode)) << "failed post must leave the TCP socket untouched";

    DestroyCloseEnv(env);
    CloseLoopbackTcpPair(pair);
}

/* 守卫: abortive linger{1,0} (RST 语义) 跳过 post 与 hold — 自然不占位 */
TEST_F(UrmaSocketTest, AbortiveLinger_NoHold)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);
    struct linger abortive = {1, 0};
    ASSERT_EQ(0, env.sock->SetLingerOpt(abortive));

    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
    g_postSendWrCalls = 0;

    ASSERT_FALSE(env.sock->PostCloseToPeer()); /* abortive: fd 仍归调用方 */
    EXPECT_EQ(0, g_postSendWrCalls);
    EXPECT_FALSE(UrmaBackend::Instance().HasPendingClose(pair.conn));

    struct stat st = {};
    errno = 0;
    EXPECT_EQ(0, fstat(pair.conn, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode)) << "abortive close must not take the fd placeholder";

    DestroyCloseEnv(env);
    CloseLoopbackTcpPair(pair);
}

/* RDHUP 表征: 对端直接关闭底层 TCP (kill -9 / 绕过 ubsocket close) 不产生用户态事件 —
 * 死亡监控全在 URMA 平面 (CLOSE opcode / 错误 CQE), TCP fd 不在 epoll 监控路径。
 * 该性质保证占位 (dup3 换 /dev/null) 不改变 RDHUP 语义: FIN 由 dup3 内核 close 发出。 */
TEST_F(UrmaSocketTest, PeerTcpClose_AloneDoesNotMarkPeerClosed)
{
    auto pair = MakeLoopbackTcpPair();
    ASSERT_GE(pair.conn, 0);
    ASSERT_GE(pair.peer, 0);

    auto env = MakeBoundCloseEnv(true, pair.conn);

    g_eventfd_write_count = 0;
    MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

    ::close(pair.peer); /* 模拟对端异常退出: TCP FIN 到达本端内核, 但无 URMA 平面通知 */
    pair.peer = -1;

    EXPECT_FALSE(env.sock->IsPeerClosed());
    EXPECT_EQ(0u, env.sock->GetReachEpollEvents()); /* 无 HUP/RDHUP 置位, 无唤醒 */
    EXPECT_EQ(0, g_eventfd_write_count);

    /* 对照: URMA 平面 CLOSE opcode 才推进对端关闭状态并注入 HUP|RDHUP */
    WriteImmData imm;
    imm.ubsocket.opcode = CLOSE;
    imm.ubsocket.buffer_offset = 1;
    env.sock->HandleRecvCqe(imm);
    EXPECT_TRUE(env.sock->IsPeerClosed());
    EXPECT_EQ(static_cast<uint32_t>(EPOLLHUP | EPOLLRDHUP), env.sock->GetReachEpollEvents());
    EXPECT_EQ(1, g_eventfd_write_count);

    GlobalMockObject::verify();
    DestroyCloseEnv(env);
    CloseLoopbackTcpPair(pair);
}

/* ===================== shutdown(SHUT_WR/SHUT_RD/SHUT_RDWR) 半关闭 ===================== */

/* shutdown(how) 参数与标志矩阵: 非法 how / 未建链 / SHUT_WR post 与幂等 / SHUT_RDWR 双标志 */
TEST_F(UrmaSocketTest, Shutdown_EntryAndFlags)
{
    struct ShutdownFlagCase {
        const char *name;
        int how;
        bool bound;
        bool with_jetty;
        bool call_twice;
        int expect_rc;
        int expect_errno;      /* -1: 不校验 errno */
        int expect_post_count;
        bool expect_wr_opcode; /* 校验 WR opcode == URMA_OPC_WRITE_IMM */
        bool expect_imm_shutdown;
        bool expect_write_flag;
        bool expect_read_flag;
    };
    static const ShutdownFlagCase cases[] = {
        /* how 非法 → EINVAL, 不 post、不置 flag */
        {"invalid_how_returns_einval", 42, true, true, false, -1, EINVAL, 0, false, false, false, false},
        /* 未建链 SHUT_WR → ENOTCONN (对齐 TCP shutdown 语义) */
        {"wr_not_bound_returns_enotconn", SHUT_WR, false, false, false, -1, ENOTCONN, 0, false, false, false, false},
        /* SHUT_WR: post SHUTDOWN imm, 置写标志、读侧不受影响, 且重复调用幂等 */
        {"wr_posts_shutdown_imm_idempotent", SHUT_WR, true, true, true, 0, -1, 1, true, true, true, false},
        /* SHUT_RDWR: 两个 flag 同时生效, 仅一条 SHUTDOWN post */
        {"rdwr_both_flags_one_post", SHUT_RDWR, true, true, false, 0, -1, 1, false, true, true, true},
    };

    for (const auto &c : cases) {
        SCOPED_TRACE(c.name);
        CloseTestEnv env;
        UrmaSocketPtr sock;
        if (c.bound) {
            env = MakeBoundCloseEnv(c.with_jetty);
            sock = env.sock;
        } else {
            sock = MakeRef<UrmaSocket>(TEST_FD);
        }

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

        errno = 0;
        EXPECT_EQ(c.expect_rc, sock->Shutdown(c.how));
        if (c.expect_errno >= 0) {
            EXPECT_EQ(c.expect_errno, errno);
        }
        EXPECT_EQ(c.expect_post_count, g_postSendWrCalls);
        if (c.expect_wr_opcode) {
            EXPECT_EQ(URMA_OPC_WRITE_IMM, static_cast<int>(g_lastWrOpcode));
        }
        if (c.expect_imm_shutdown) {
            WriteImmData imm{g_lastNotifyData};
            EXPECT_EQ(SHUTDOWN, imm.ubsocket.opcode); /* imm 携带 SHUTDOWN opcode, RDMA 队列保序 */
        }
        EXPECT_EQ(c.expect_write_flag, sock->IsWriteShutdown());
        EXPECT_EQ(c.expect_read_flag, sock->IsReadShutdown());
        EXPECT_FALSE(sock->IsPeerWriteShutdown()); /* 本端发起, 与对端状态无关 */

        if (c.call_twice) {
            EXPECT_EQ(0, sock->Shutdown(c.how));
            EXPECT_EQ(c.expect_post_count, g_postSendWrCalls); /* 幂等: 不再 post */
        }
        if (c.bound) {
            DestroyCloseEnv(env);
        }
    }
}

/* shutdown 与本端 IO 门控: SHUT_WR 后 writev 返回 EPIPE; SHUT_RD 后 readv 一次性返回 EOF 且不 post */
TEST_F(UrmaSocketTest, Shutdown_IOGating)
{
    /* SHUT_WR 后 writev 门控: -1/EPIPE (对齐 TCP send-after-shutdown) */
    {
        SCOPED_TRACE("case: wr_gates_writev_returns_epipe");
        auto env = MakeBoundCloseEnv();
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        ASSERT_EQ(0, env.sock->Shutdown(SHUT_WR));

        UrmaTxOps tx_ops(TEST_FD);
        auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
        struct iovec iov;
        iov.iov_base = const_cast<char *>("test");
        iov.iov_len = 4;
        errno = 0;
        EXPECT_EQ(-1, tx_ops.WriteVOnce(sock, &iov, 1, 0));
        EXPECT_EQ(EPIPE, errno);

        DestroyCloseEnv(env);
    }
    /* SHUT_RD: readv 立即返回 0/EOF (Linux 语义), 写侧不受影响、不 post */
    {
        SCOPED_TRACE("case: rd_readv_once_returns_eof_no_post");
        auto env = MakeBoundCloseEnv();

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

        errno = 0;
        EXPECT_EQ(0, env.sock->Shutdown(SHUT_RD));
        EXPECT_TRUE(env.sock->IsReadShutdown());
        EXPECT_FALSE(env.sock->IsWriteShutdown());
        EXPECT_EQ(0, g_postSendWrCalls);

        UrmaRxOps rx_ops(TEST_FD);
        auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
        char buf[16];
        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        errno = 0;
        EXPECT_EQ(0, rx_ops.ReadVOnce(sock, &iov, 1, 0)); /* EOF */

        DestroyCloseEnv(env);
    }
}

/* SHUTDOWN 发送 CQE: 释放 1KB ctrl 占用 (socket 存活, 不释放会导致后续 linger close drain 空等) */
TEST_F(UrmaSocketTest, Shutdown_Wr_SendCqe_ReleasesCtrlBuf)
{
    auto env = MakeBoundCloseEnv();
    auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get()); /* 发送 CQE 按 fd 路由 */

    g_postSendWrCalls = 0;
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
    ASSERT_EQ(0, env.sock->Shutdown(SHUT_WR));

    WriteImmData imm{g_lastNotifyData};
    const auto buf_offset_units = static_cast<uint64_t>(imm.ubsocket.buffer_offset);
    EXPECT_EQ(static_cast<uint64_t>(URMA_CTRL_ONLY_BUF_SIZE), env.sock->send_buf_->Used()); /* ctrl 占用 */

    /* WR 上屏: 发送 CQE 到达 → 释放 ctrl 占用 */
    auto cr = MakeShutdownSendCqe(buf_offset_units, TEST_FD);
    UrmaBackend::Instance().HandleCqe(0, 1, &cr);
    EXPECT_EQ(0U, env.sock->send_buf_->Used());

    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, nullptr);
    DestroyCloseEnv(env);
}

/* 对端已关/reset: SHUT_WR 不 post (通知不可达), 本端仍置 flag 维持半关闭语义 */
TEST_F(UrmaSocketTest, Shutdown_Wr_PeerClosedOrReset_SkipsPost)
{
    /* 对端 graceful CLOSE (对端 jetty 已销毁, post 只会收到 flush 错误 CQE) */
    {
        SCOPED_TRACE("case: peer_closed_skips_post");
        auto env = MakeBoundCloseEnv();
        WriteImmData imm;
        imm.ubsocket.opcode = CLOSE;
        env.sock->HandleRecvCqe(imm); /* 置 PeerCloseState::CLOSED */

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        EXPECT_EQ(0, env.sock->Shutdown(SHUT_WR));
        EXPECT_EQ(0, g_postSendWrCalls);
        EXPECT_TRUE(env.sock->IsWriteShutdown());
        DestroyCloseEnv(env);
    }
    /* 连接 reset (对端不可达, post 无意义) */
    {
        SCOPED_TRACE("case: peer_reset_skips_post");
        auto env = MakeBoundCloseEnv();
        MOCKER(::eventfd_write).stubs().will(returnValue(0)); /* OnPeerReset → NotifyClosed 路径 */
        env.sock->OnPeerReset();
        GlobalMockObject::verify();

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        EXPECT_EQ(0, env.sock->Shutdown(SHUT_WR));
        EXPECT_EQ(0, g_postSendWrCalls);
        EXPECT_TRUE(env.sock->IsWriteShutdown());
        DestroyCloseEnv(env);
    }
}

/* 对端半关闭 (SHUTDOWN opcode): 置 PeerCloseState::SHUT_WR + RDHUP 唤醒, readv 排空后 EOF,
 * 但不抑制本端 close 的 CLOSE post (只有 CLOSED 才抑制) */
TEST_F(UrmaSocketTest, PeerShutdownWrite_Behavior)
{
    /* 入口语义: SHUTDOWN opcode ≠ CLOSE, 仅注入 RDHUP (不含 HUP) 并唤醒阻塞读 */
    {
        SCOPED_TRACE("case: shutdown_opcode_marks_peer_write_shutdown");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);

        g_eventfd_write_count = 0;
        MOCKER(::eventfd_write).stubs().will(invoke(MockEventfdWriteCounting));

        WriteImmData imm;
        imm.ubsocket.opcode = SHUTDOWN;
        imm.ubsocket.buffer_offset = 1;
        urma_sock->HandleRecvCqe(imm);

        EXPECT_TRUE(urma_sock->IsPeerWriteShutdown());
        EXPECT_FALSE(urma_sock->IsPeerClosed()); /* 半关闭 ≠ 全关闭 */
        EXPECT_FALSE(urma_sock->IsPeerReset());
        EXPECT_EQ(static_cast<uint32_t>(EPOLLRDHUP), urma_sock->GetReachEpollEvents()); /* 仅 RDHUP */
        EXPECT_EQ(1, g_eventfd_write_count);
        /* 唤醒目标必须是读侧: 对端 SHUT_WR 只关闭"对端→本端"方向, 本端 writev 无门控 */
        EXPECT_EQ(0u, urma_sock->block_flags_ & BLOCKING_FLAG_PEER_CLOSED);
        EXPECT_NE(0u, urma_sock->block_flags_ & BLOCKING_FLAG_READABLE); /* 唤醒阻塞读 */

        GlobalMockObject::verify();
    }
    /* 对端半关闭后 readv: 数据排尽后返回 0/EOF (FIN 语义) */
    {
        SCOPED_TRACE("case: peer_write_shutdown_readv_once_returns_eof");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        MOCKER(::eventfd_write).stubs().will(returnValue(0));
        WriteImmData imm;
        imm.ubsocket.opcode = SHUTDOWN;
        urma_sock->HandleRecvCqe(imm);
        GlobalMockObject::verify();

        UrmaRxOps rx_ops(TEST_FD);
        auto sock = RefConvert<UrmaSocket, Socket>(urma_sock);
        char buf[16];
        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        errno = 0;
        EXPECT_EQ(0, rx_ops.ReadVOnce(sock, &iov, 1, 0)); /* 无数据 + 对端半关 → EOF */
    }
    /* 对端半关闭不抑制本端 close 的 CLOSE post — 对端仍在收, 必须通知 */
    {
        SCOPED_TRACE("case: peer_write_shutdown_does_not_suppress_close_post");
        auto env = MakeBoundCloseEnv();

        MOCKER(::eventfd_write).stubs().will(returnValue(0));
        WriteImmData imm;
        imm.ubsocket.opcode = SHUTDOWN;
        env.sock->HandleRecvCqe(imm);
        GlobalMockObject::verify();

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;
        EXPECT_TRUE(env.sock->PostCloseToPeer(0)); /* post 成功, fd 号已被占位接管 */
        EXPECT_EQ(1, g_postSendWrCalls);
        WriteImmData close_imm{g_lastNotifyData};
        EXPECT_EQ(CLOSE, close_imm.ubsocket.opcode);

        DestroyCloseEnv(env);
    }
}

/* shutdown 入口拦截: URMA socket 走 Shutdown 虚方法 (含 post SHUTDOWN); 非管理 fd 透传 libc */
TEST_F(UrmaSocketTest, ShutdownEntry_Dispatch)
{
    /* URMA socket: 拦截并走虚方法 */
    {
        SCOPED_TRACE("case: urma_socket_intercepted");
        auto env = MakeBoundCloseEnv();
        auto sock = RefConvert<UrmaSocket, Socket>(env.sock);
        ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());

        g_postSendWrCalls = 0;
        UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

        errno = 0;
        EXPECT_EQ(0, ubsocket_shutdown(TEST_FD, SHUT_WR));
        EXPECT_EQ(1, g_postSendWrCalls);
        EXPECT_TRUE(env.sock->IsWriteShutdown());

        ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, nullptr);
        DestroyCloseEnv(env);
    }
    /* 非 ubsocket 管理的 fd: 透传 libc (保持纯 TCP 兼容语义) */
    {
        SCOPED_TRACE("case: unmanaged_fd_passthrough_libc");
        (void)LibcApi::Load(); /* 确保 ptr 非 null, 再替换为记录型 mock */
        auto *orig_shutdown = LibcApi::shutdown_ptr;
        LibcApi::shutdown_ptr = MockLibcShutdownPassthrough;

        static const int UNMANAGED_FD = 9101;
        g_libcShutdownCalls = 0;
        errno = 0;
        EXPECT_EQ(0, ubsocket_shutdown(UNMANAGED_FD, SHUT_WR));
        EXPECT_EQ(1, g_libcShutdownCalls);

        LibcApi::shutdown_ptr = orig_shutdown;
    }
}

/* 对端状态是单向偏序 (NONE → SHUT_WR → CLOSED), CLOSED/RESET 为吸收态:
 * 终态后晚到的 SHUTDOWN/CLOSE/RESET 一律忽略 —— 既消除旧实现里"两个标志同时为真"的矛盾态,
 * 也保证已经上报过的读语义 (EOF / ECONNRESET) 不被后续事件翻转 */
TEST_F(UrmaSocketTest, PeerCloseState_AbsorbingTerminalStates)
{
    MOCKER(::eventfd_write).stubs().will(returnValue(0));

    /* SHUT_WR → CLOSED: 前进 (终态覆写半关闭态) */
    {
        SCOPED_TRACE("case: shut_wr_then_close_advances");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        WriteImmData shut_imm;
        shut_imm.ubsocket.opcode = SHUTDOWN;
        urma_sock->HandleRecvCqe(shut_imm);
        ASSERT_TRUE(urma_sock->IsPeerWriteShutdown());

        WriteImmData close_imm;
        close_imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(close_imm);
        EXPECT_TRUE(urma_sock->IsPeerClosed());
        EXPECT_FALSE(urma_sock->IsPeerWriteShutdown());
        EXPECT_FALSE(urma_sock->IsPeerReset());
    }
    /* CLOSED 后晚到 SHUTDOWN: 吸收, 不得降级 */
    {
        SCOPED_TRACE("case: shutdown_after_closed_is_ignored");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        WriteImmData close_imm;
        close_imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(close_imm);
        ASSERT_TRUE(urma_sock->IsPeerClosed());

        WriteImmData shut_imm;
        shut_imm.ubsocket.opcode = SHUTDOWN;
        urma_sock->HandleRecvCqe(shut_imm);
        EXPECT_TRUE(urma_sock->IsPeerClosed());
        EXPECT_FALSE(urma_sock->IsPeerWriteShutdown());
    }
    /* CLOSED 后晚到 reset: 吸收 —— 对端已有序关闭, 之后本端写失败产生的错误 CQE
     * 不应把已经上报的 EOF 翻转成 ECONNRESET */
    {
        SCOPED_TRACE("case: reset_after_closed_is_ignored");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        WriteImmData close_imm;
        close_imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(close_imm);
        ASSERT_TRUE(urma_sock->IsPeerClosed());

        urma_sock->OnPeerReset();
        EXPECT_TRUE(urma_sock->IsPeerClosed());
        EXPECT_FALSE(urma_sock->IsPeerReset());
    }
    /* RESET 后晚到 CLOSE/SHUTDOWN: 吸收 (readv 仍以 ECONNRESET 优先) */
    {
        SCOPED_TRACE("case: reset_absorbs_late_close_and_shutdown");
        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->OnPeerReset();
        ASSERT_TRUE(urma_sock->IsPeerReset());

        WriteImmData close_imm;
        close_imm.ubsocket.opcode = CLOSE;
        urma_sock->HandleRecvCqe(close_imm);
        WriteImmData shut_imm;
        shut_imm.ubsocket.opcode = SHUTDOWN;
        urma_sock->HandleRecvCqe(shut_imm);
        EXPECT_TRUE(urma_sock->IsPeerReset());
        EXPECT_FALSE(urma_sock->IsPeerClosed());
        EXPECT_FALSE(urma_sock->IsPeerWriteShutdown());
    }
}

/* 本端半关闭是位标志: SHUT_WR 之后再 SHUT_RD 必须合并为 SHUT_RDWR (POSIX 允许的调用序),
 * 反向同理; 重复调用同一方向幂等 (不再 post) */
TEST_F(UrmaSocketTest, Shutdown_WrThenRd_MergesLocalState)
{
    auto env = MakeBoundCloseEnv();
    g_postSendWrCalls = 0;
    UrmaApi::urma_post_jetty_send_wr_ptr = MockPostSendWrCapture;

    ASSERT_EQ(0, env.sock->Shutdown(SHUT_WR));
    EXPECT_TRUE(env.sock->IsWriteShutdown());
    EXPECT_FALSE(env.sock->IsReadShutdown());

    ASSERT_EQ(0, env.sock->Shutdown(SHUT_RD)); /* 合并: 不能把写标志冲掉 */
    EXPECT_TRUE(env.sock->IsWriteShutdown());
    EXPECT_TRUE(env.sock->IsReadShutdown());
    EXPECT_EQ(1, g_postSendWrCalls); /* SHUT_RD 不产生新 post */

    ASSERT_EQ(0, env.sock->Shutdown(SHUT_WR)); /* 幂等 */
    EXPECT_TRUE(env.sock->IsWriteShutdown());
    EXPECT_EQ(1, g_postSendWrCalls);

    DestroyCloseEnv(env);
}
