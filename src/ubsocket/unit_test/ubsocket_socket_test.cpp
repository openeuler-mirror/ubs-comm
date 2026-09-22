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

#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_acceptor.h"
#include "core/ubsocket_socket_connector.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "core/umq/umq_data_plane.h"
#include "core/umq/umq_socket.h"
#include "core/umq/umq_socket_acceptor.h"
#include "core/umq/umq_socket_connector.h"

#include <cstring>
#include <sys/socket.h>
#include <thread>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "ubsocket_def.h"
#include "ubsocket_errno.h"

using namespace ock::ubs;

namespace {

static const int TEST_FD = 42;
/* 测试用新建连接 fd（Accept/Connector 返回值注入） */
static const int TEST_NEW_FD = 84;
static const uint32_t TEST_RPC_TIMEOUT_MS = 500;
/* 未知 SOL_UB optname 注入值（无对应枚举成员） */
static const int TEST_OPT_INVALID = 99;
/* 测试用 epoll fd（未绑定真实 OS 资源，AsyncEventPoll 构造仅做 reserve） */
static const int TEST_EPOLL_FD = 7;

static int g_writevCallCount = 0;
static int g_readvCallCount = 0;

/* LibcApi::writev/readv 的 mock（RAW_ESTABLISHED 直通路径用），返回 iovcnt 以便断言命中 */
static ssize_t MockWritev(int fd, const struct iovec *iov, int iovcnt)
{
    g_writevCallCount++;
    return static_cast<ssize_t>(iovcnt);
}

static ssize_t MockReadv(int fd, const struct iovec *iov, int iovcnt)
{
    g_readvCallCount++;
    return static_cast<ssize_t>(iovcnt);
}

} // namespace

/* SocketBase 建链/选项函数 UT：Create / GenerateSocketCommOps /
 * CreateAcceptorOps / CreateConnectorOps / EnsureConnector / GetSockOpt /
 * SetSockOpt。这些函数内部依赖 RefConvert<..., UmqSocket> 的 dynamic_cast，
 * 因此以真实 UmqSocket 为被测对象（轻量：Initialize 恒 UBS_OK、无 UMQ 句柄）。
 * TxCqePoller::Start 是 Create 的唯一真实副作用，用 mockcpp 拦截返回值。 */
class SocketBaseTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        errno = 0;
    }

    /* 构造一个真实的 UMQ 类型 UmqSocket（fd = TEST_FD，未注册到 ArraySet） */
    umq::UmqSocketPtr NewUmqSocket()
    {
        return MakeRef<umq::UmqSocket>(TEST_FD);
    }

    SocketPtr ToSocketPtr(const umq::UmqSocketPtr &sock)
    {
        return RefConvert<umq::UmqSocket, Socket>(sock);
    }
};

/* ---------------- SocketBase::Create ---------------- */

TEST_F(SocketBaseTest, Create_TypeNotUmq_ReturnsInvalidParam)
{
    SocketPtr outSocket;
    ock::ubs::Result ret = SocketBase::Create(TEST_FD, SocketType::SOCK_TYPE_TCP, outSocket);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(outSocket.Get(), nullptr);
}

TEST_F(SocketBaseTest, Create_StartPollerFail_ReturnsPollerError)
{
    MOCKER_CPP(&TxCqePoller::Start).stubs().will(returnValue(-1));

    SocketPtr outSocket;
    ock::ubs::Result ret = SocketBase::Create(TEST_FD, SocketType::SOCK_TYPE_UMQ, outSocket);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(outSocket.Get(), nullptr);
}

TEST_F(SocketBaseTest, Create_AcceptorOpsFail_ReturnsError)
{
    MOCKER_CPP(&TxCqePoller::Start).stubs().will(returnValue(0));
    /* 拦截工厂：Create 内 CreateAcceptorOps 返回非 OK → 提前返回该错误码
     * （类静态方法 → 按 ut-gen §5 约定用 MOCKER，与 MOCKER_CPP 展开等价） */
    MOCKER(&SocketBase::CreateAcceptorOps)
        .stubs()
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_MALLOC_FAILED)));

    SocketPtr outSocket;
    ock::ubs::Result ret = SocketBase::Create(TEST_FD, SocketType::SOCK_TYPE_UMQ, outSocket);

    EXPECT_EQ(ret, UBS_MALLOC_FAILED);
    EXPECT_EQ(outSocket.Get(), nullptr);
}

TEST_F(SocketBaseTest, Create_Success_ReturnsOkAndAssemblesHandshake)
{
    MOCKER_CPP(&TxCqePoller::Start).stubs().will(returnValue(0));

    SocketPtr outSocket;
    ock::ubs::Result ret = SocketBase::Create(TEST_FD, SocketType::SOCK_TYPE_UMQ, outSocket);

    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(outSocket.Get(), nullptr);
    /* 输出 socket 应为真实 UmqSocket（Create 内部 MakeRef<UmqSocket>） */
    EXPECT_NE(dynamic_cast<umq::UmqSocket *>(outSocket.Get()), nullptr);

    SocketBase *sockBase = dynamic_cast<SocketBase *>(outSocket.Get());
    ASSERT_NE(sockBase, nullptr);
    /* 握手上下文已装配：hs 存在且 acceptor 已挂上 UmqAcceptorOps */
    SocketBase::HandshakeCtx *hs = sockBase->Hs();
    ASSERT_NE(hs, nullptr);
    EXPECT_TRUE(hs->acceptor.HasOps());
    EXPECT_EQ(hs->connector, nullptr); /* connector 惰性，Create 不创建 */
}

/* ---------------- SocketBase::GenerateSocketCommOps ---------------- */

TEST_F(SocketBaseTest, GenerateSocketCommOps_NullSock_ReturnsInvalidParam)
{
    SocketPtr nullSock;
    ock::ubs::Result ret = SocketBase::GenerateSocketCommOps(nullSock);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
}

TEST_F(SocketBaseTest, GenerateSocketCommOps_NonUmqType_ReturnsInvalidParam)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    sock->type_ = SocketType::SOCK_TYPE_TCP; /* type_ 为 Socket 公有成员 */

    ock::ubs::Result ret = SocketBase::GenerateSocketCommOps(ToSocketPtr(sock));

    EXPECT_EQ(ret, UBS_INVALID_PARAM);
}

TEST_F(SocketBaseTest, GenerateSocketCommOps_SlotForFail_ReturnsMallocFailed)
{
    /* ReinitTxOps 内部依赖 DataPlaneTable::SlotFor；页分配失败时返回 nullptr */
    MOCKER_CPP(&umq::DataPlaneTable::SlotFor)
        .stubs()
        .will(returnValue(static_cast<umq::DataPlaneEntry *>(nullptr)));

    umq::UmqSocketPtr sock = NewUmqSocket();
    ock::ubs::Result ret = SocketBase::GenerateSocketCommOps(ToSocketPtr(sock));

    EXPECT_EQ(ret, UBS_MALLOC_FAILED);
}

TEST_F(SocketBaseTest, GenerateSocketCommOps_Success_AssemblesDataPlane)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketPtr base = ToSocketPtr(sock);

    ock::ubs::Result ret = SocketBase::GenerateSocketCommOps(base);

    EXPECT_EQ(ret, UBS_OK);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(base.Get());
    ASSERT_NE(sockBase, nullptr);
    /* 数据面条目已装配：壳与 ops 均可经条目读到 */
    EXPECT_NE(sockBase->GetTx(), nullptr);
    EXPECT_NE(sockBase->GetRx(), nullptr);
    EXPECT_NE(sockBase->GetTxOps(), nullptr);
    EXPECT_NE(sockBase->GetRxOps(), nullptr);
}

/* ---------------- SocketBase::CreateAcceptorOps ---------------- */

TEST_F(SocketBaseTest, CreateAcceptorOps_NullSock_ReturnsInvalidParam)
{
    AcceptorOps *acceptorOps = nullptr;
    SocketPtr nullSock;
    ock::ubs::Result ret = SocketBase::CreateAcceptorOps(SocketType::SOCK_TYPE_UMQ, nullSock, acceptorOps);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(acceptorOps, nullptr);
}

TEST_F(SocketBaseTest, CreateAcceptorOps_NonUmqType_ReturnsInvalidParam)
{
    AcceptorOps *acceptorOps = nullptr;
    umq::UmqSocketPtr sock = NewUmqSocket();
    ock::ubs::Result ret = SocketBase::CreateAcceptorOps(SocketType::SOCK_TYPE_TCP, ToSocketPtr(sock), acceptorOps);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(acceptorOps, nullptr);
}

TEST_F(SocketBaseTest, CreateAcceptorOps_UmqType_CreatesUmqAcceptorOps)
{
    AcceptorOps *acceptorOps = nullptr;
    umq::UmqSocketPtr sock = NewUmqSocket();
    ock::ubs::Result ret = SocketBase::CreateAcceptorOps(SocketType::SOCK_TYPE_UMQ, ToSocketPtr(sock), acceptorOps);
    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(acceptorOps, nullptr);
    EXPECT_NE(dynamic_cast<umq::UmqAcceptorOps *>(acceptorOps), nullptr);
    delete acceptorOps; /* 返回裸指针（未入 Ref），测试侧负责释放 */
}

/* ---------------- SocketBase::CreateConnectorOps ---------------- */

TEST_F(SocketBaseTest, CreateConnectorOps_NullSock_ReturnsInvalidParam)
{
    ConnectorOps *connectorOps = nullptr;
    SocketPtr nullSock;
    ock::ubs::Result ret = SocketBase::CreateConnectorOps(SocketType::SOCK_TYPE_UMQ, nullSock, connectorOps);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(connectorOps, nullptr);
}

TEST_F(SocketBaseTest, CreateConnectorOps_NonUmqType_ReturnsInvalidParam)
{
    ConnectorOps *connectorOps = nullptr;
    umq::UmqSocketPtr sock = NewUmqSocket();
    ock::ubs::Result ret = SocketBase::CreateConnectorOps(SocketType::SOCK_TYPE_TCP, ToSocketPtr(sock), connectorOps);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(connectorOps, nullptr);
}

TEST_F(SocketBaseTest, CreateConnectorOps_UmqType_CreatesUmqConnectorOps)
{
    ConnectorOps *connectorOps = nullptr;
    umq::UmqSocketPtr sock = NewUmqSocket();
    ock::ubs::Result ret = SocketBase::CreateConnectorOps(SocketType::SOCK_TYPE_UMQ, ToSocketPtr(sock), connectorOps);
    EXPECT_EQ(ret, UBS_OK);
    ASSERT_NE(connectorOps, nullptr);
    EXPECT_NE(dynamic_cast<umq::UmqConnectorOps *>(connectorOps), nullptr);
    delete connectorOps; /* 返回裸指针（未入 Ref），测试侧负责释放 */
}

/* ---------------- SocketBase::EnsureConnector ---------------- */

TEST_F(SocketBaseTest, EnsureConnector_CreateOpsFail_ReturnsInvalidParam)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    sock->type_ = SocketType::SOCK_TYPE_TCP; /* type_ 非 UMQ → CreateConnectorOps 失败 */
    SocketPtr base = ToSocketPtr(sock);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(base.Get());
    ASSERT_NE(sockBase, nullptr);

    ock::ubs::Result ret = sockBase->EnsureConnector(base);

    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    EXPECT_EQ(sockBase->Hs(), nullptr); /* 失败路径不产生握手上下文 */
}

TEST_F(SocketBaseTest, EnsureConnector_FreshSocket_CreatesHsAndConnector)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketPtr base = ToSocketPtr(sock);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(base.Get());
    ASSERT_NE(sockBase, nullptr);

    /* 正常路径：ext->hs 为 nullptr，走防御分支新建 HandshakeCtx */
    ock::ubs::Result ret = sockBase->EnsureConnector(base);

    EXPECT_EQ(ret, UBS_OK);
    SocketBase::HandshakeCtx *hs = sockBase->Hs();
    ASSERT_NE(hs, nullptr);
    EXPECT_NE(hs->connector, nullptr); /* 惰性 connector 已创建 */
}

TEST_F(SocketBaseTest, EnsureConnector_ExistingHs_SkipsAllocAndCreatesConnector)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketPtr base = ToSocketPtr(sock);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(base.Get());
    ASSERT_NE(sockBase, nullptr);

    /* 预置 hs（模拟 Create 已装配的握手上下文），走"已有 hs"分支 */
    sockBase->EnsureExt()->hs = new SocketBase::HandshakeCtx;

    ock::ubs::Result ret = sockBase->EnsureConnector(base);

    EXPECT_EQ(ret, UBS_OK);
    SocketBase::HandshakeCtx *hs = sockBase->Hs();
    ASSERT_NE(hs, nullptr);
    EXPECT_NE(hs->connector, nullptr);
}

/* ---------------- SocketBase::GetSockOpt ---------------- */

TEST_F(SocketBaseTest, GetSockOpt_NullArgs_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    int optval = 0;
    socklen_t optlen = sizeof(optval);

    int ret = sockBase->GetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_PROTOCOL), nullptr, &optlen);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);

    ret = sockBase->GetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                               static_cast<int>(UbSocketOpt::UBS_OPT_PROTOCOL), &optval, nullptr);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, GetSockOpt_SolUbProtocol_ReturnsSocketType)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    int optval = 0;
    socklen_t optlen = sizeof(optval);

    int ret = sockBase->GetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_PROTOCOL), &optval, &optlen);

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(optval, static_cast<int>(SocketType::SOCK_TYPE_UMQ));
    EXPECT_EQ(optlen, static_cast<socklen_t>(sizeof(int)));
}

TEST_F(SocketBaseTest, GetSockOpt_UnknownSolUbOpt_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    int optval = 0;
    socklen_t optlen = sizeof(optval);

    int ret = sockBase->GetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), &optval, &optlen);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, GetSockOpt_NonSolUbLevel_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    int optval = 0;
    socklen_t optlen = sizeof(optval);

    int ret = sockBase->GetSockOpt(TEST_FD, SOL_SOCKET, static_cast<int>(UbSocketOpt::UBS_OPT_PROTOCOL), &optval,
                                   &optlen);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

/* ---------------- SocketBase::SetSockOpt ---------------- */

TEST_F(SocketBaseTest, SetSockOpt_NullArgs_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    int ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), nullptr,
                                   sizeof(timeoutMs));
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);

    ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                               static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), &timeoutMs, 0);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, SetSockOpt_RpcTimeoutOptlenTooSmall_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    /* optlen < sizeof(uint32_t) → EINVAL */
    int ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), &timeoutMs,
                                   sizeof(uint8_t));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, SetSockOpt_RpcTimeoutFdNotUmqSocket_ReturnsEnotsock)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    /* ArraySet 中 TEST_FD 无条目 → GetItem 返回空 → 非 umq socket → ENOTSOCK */
    int ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), &timeoutMs,
                                   sizeof(timeoutMs));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, ENOTSOCK);
}

TEST_F(SocketBaseTest, SetSockOpt_RpcTimeoutSuccess_SetsLocalTimeout)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, sock.Get());
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    int ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS), &timeoutMs,
                                   sizeof(timeoutMs));

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(sock->GetLocalRpcTimeoutMs(), TEST_RPC_TIMEOUT_MS);
}

TEST_F(SocketBaseTest, SetSockOpt_UnknownSolUbOpt_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    int ret = sockBase->SetSockOpt(TEST_FD, static_cast<int>(UbsocketLevel::SOL_UB),
                                   static_cast<int>(TEST_OPT_INVALID), &timeoutMs, sizeof(timeoutMs));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, SetSockOpt_NonSolUbLevel_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    uint32_t timeoutMs = TEST_RPC_TIMEOUT_MS;

    int ret = sockBase->SetSockOpt(TEST_FD, SOL_SOCKET, static_cast<int>(UbSocketOpt::UBS_OPT_RPC_TIMEOUT_MS),
                                   &timeoutMs, sizeof(timeoutMs));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
}

/* ---------------- SocketBase::Accept ---------------- */

TEST_F(SocketBaseTest, Accept_NoHandshakeCtx_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);

    int ret = sockBase->Accept(base, nullptr, nullptr);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, Accept_HandshakeCtxWithoutOps_ReturnsEinval)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    /* 预置空握手上下文：acceptor 未挂 ops → HasOps() 为 false */
    sockBase->EnsureExt()->hs = new SocketBase::HandshakeCtx;
    SocketPtr base = ToSocketPtr(sock);

    int ret = sockBase->Accept(base, nullptr, nullptr);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(SocketBaseTest, Accept_WithAcceptorOps_DelegatesToAcceptor)
{
    /* 复用 Create：hs 已装配且挂上 UmqAcceptorOps */
    MOCKER_CPP(&TxCqePoller::Start).stubs().will(returnValue(0));
    SocketPtr outSocket;
    ASSERT_EQ(SocketBase::Create(TEST_FD, SocketType::SOCK_TYPE_UMQ, outSocket), UBS_OK);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(outSocket.Get());
    ASSERT_NE(sockBase, nullptr);
    /* 拦截真实 Acceptor::Accept，避免在无效 fd 上做真实 accept */
    MOCKER_CPP(&Acceptor::Accept).stubs().will(returnValue(TEST_NEW_FD));

    struct sockaddr addr {};
    socklen_t addrLen = sizeof(addr);
    int ret = sockBase->Accept(outSocket, &addr, &addrLen);

    EXPECT_EQ(ret, TEST_NEW_FD);
}

/* ---------------- SocketBase::Connect ---------------- */

TEST_F(SocketBaseTest, Connect_EnsureConnectorFail_ReturnsEnomem)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    sock->type_ = SocketType::SOCK_TYPE_TCP; /* 非 UMQ → CreateConnectorOps 失败 → EnsureConnector 非 OK */
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    struct sockaddr addr {};

    int ret = sockBase->Connect(base, reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, ENOMEM);
}

TEST_F(SocketBaseTest, Connect_WithConnector_DelegatesToConnector)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    /* 先惰性创建 connector，再拦截真实 Connector::Connect */
    ASSERT_EQ(sockBase->EnsureConnector(base), UBS_OK);
    MOCKER_CPP(&Connector::Connect).stubs().will(returnValue(0));
    struct sockaddr addr {};

    int ret = sockBase->Connect(base, reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, 0);
}

/* ---------------- SocketBase::WriteV / ReadV ---------------- */

TEST_F(SocketBaseTest, WriteV_RawEstablished_ShortCircuitsToLibcWritev)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    sock->state_ = SOCK_STAT_RAW_ESTABLISHED; /* TCP 直通短路分支 */
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    struct iovec iov[1] = {};
    g_writevCallCount = 0;
    LibcApi::writev_ptr = MockWritev;

    ssize_t ret = sockBase->WriteV(base, iov, 1);

    EXPECT_EQ(ret, 1);
    EXPECT_EQ(g_writevCallCount, 1);
    LibcApi::writev_ptr = nullptr;
}

TEST_F(SocketBaseTest, WriteV_NoDataPlaneEntry_ReturnsEnotconn)
{
    /* 未装配数据面条目 → DataPlaneTable::Live 返回 nullptr */
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    struct iovec iov[1] = {};

    int ret = sockBase->WriteV(base, iov, 1);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, ENOTCONN);
}

TEST_F(SocketBaseTest, ReadV_RawEstablished_ShortCircuitsToLibcReadv)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    sock->state_ = SOCK_STAT_RAW_ESTABLISHED; /* TCP 直通短路分支 */
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    struct iovec iov[1] = {};
    g_readvCallCount = 0;
    LibcApi::readv_ptr = MockReadv;

    ssize_t ret = sockBase->ReadV(base, iov, 1);

    EXPECT_EQ(ret, 1);
    EXPECT_EQ(g_readvCallCount, 1);
    LibcApi::readv_ptr = nullptr;
}

TEST_F(SocketBaseTest, ReadV_NoDataPlaneEntry_ReturnsEnotconn)
{
    /* 未装配数据面条目 → DataPlaneTable::Live 返回 nullptr */
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);
    struct iovec iov[1] = {};

    int ret = sockBase->ReadV(base, iov, 1);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, ENOTCONN);
}

/* ---------------- SocketBase::SetEvents/GetEvents/GetVersionedWritableReady ---------------- */

TEST_F(SocketBaseTest, SetEvents_GetEvents_RoundTrip)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    sockBase->SetEvents(EPOLLIN | EPOLLOUT);

    EXPECT_EQ(sockBase->GetEvents(), static_cast<uint32_t>(EPOLLIN | EPOLLOUT));
}

TEST_F(SocketBaseTest, GetVersionedWritableReady_InitialReady)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    /* 初始 versioned_writable_ready_ = 0x1：bit0=1（可写）、版本号 0 */
    EXPECT_EQ(sockBase->GetVersionedWritableReady(), 0x1U);
}

/* ---------------- SocketBase::NotifyReadable / NotifyWritable ---------------- */

TEST_F(SocketBaseTest, NotifyReadable_EpolloutNotWatched_ReturnsZero)
{
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    MOCKER_CPP(&AsyncEventPoll::AddReadableEvent).stubs().will(returnValue(0));
    MOCKER_CPP(&AsyncEventPoll::SetReadableEventFd).stubs().will(returnValue(0));

    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data); /* events_=0，未关注 EPOLLOUT */

    int ret = sockBase->NotifyReadable(true); /* epollout=true → 命中 debug 日志分支 */

    EXPECT_EQ(ret, 0);
    delete aep;
}

TEST_F(SocketBaseTest, NotifyReadable_DirectDispatch_ReturnsZero)
{
    static u_external_poller_ops_t ops = {};
    ops.dispatch_event = [](uint64_t, uint32_t) {};
    GlobalSetting::UBS_POLLER_OPS = &ops;

    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    int ret = sockBase->NotifyReadable(false);

    EXPECT_EQ(ret, 0);
    delete aep;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(SocketBaseTest, NotifyReadable_QueueFull_ReturnsError)
{
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    MOCKER_CPP(&AsyncEventPoll::AddReadableEvent).stubs().will(returnValue(-1));

    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    int ret = sockBase->NotifyReadable(false);

    EXPECT_EQ(ret, -1);
    delete aep;
}

TEST_F(SocketBaseTest, NotifyWritable_EpolloutWatched_DirectDispatchSucceeds)
{
    static u_external_poller_ops_t ops = {};
    ops.dispatch_event = [](uint64_t, uint32_t) {};
    GlobalSetting::UBS_POLLER_OPS = &ops;

    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    sockBase->SetEvents(EPOLLOUT); /* 上层已关注 EPOLLOUT → 直走 DoNotifyWritable */
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, 0);
    delete aep;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

TEST_F(SocketBaseTest, NotifyWritable_QueueFull_ReturnsError)
{
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    MOCKER_CPP(&AsyncEventPoll::AddReadableEvent).stubs().will(returnValue(-1));

    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    sockBase->SetEvents(EPOLLOUT);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, -1);
    delete aep;
}

/* ==================== ubsocket_socket.h 内联方法覆盖 ==================== */

/* SetWritableReady：CAS 循环把版本号 +1 并置位 bit0，必定成功 */
TEST_F(SocketBaseTest, SetWritableReady_IncrementsVersionAndSetsReadyBit)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    bool ok = sockBase->SetWritableReady();

    EXPECT_TRUE(ok);
    EXPECT_EQ(sockBase->Version(), 1U);
    EXPECT_TRUE(sockBase->Ready());
}

/* SetNotWritableReadyIfUnchanged：pre 与当前一致 → 仅把 bit0 清 0 并成功 */
TEST_F(SocketBaseTest, SetNotWritableReadyIfUnchanged_WhenUnchanged_ClearsReadyBit)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    uint32_t pre = sockBase->GetVersionedWritableReady();
    bool ok = sockBase->SetNotWritableReadyIfUnchanged(pre);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(sockBase->Ready());
    EXPECT_EQ(sockBase->Version(), SocketBase::Version(pre));
}

/* SetNotWritableReadyIfUnchanged：pre 与当前不一致（另一线程已变更）→ 失败且不改状态 */
TEST_F(SocketBaseTest, SetNotWritableReadyIfUnchanged_WhenChanged_ReturnsFalse)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    const uint32_t stalePre = 0x5; /* 当前为 0x1，传入不一致的 pre */
    bool ok = sockBase->SetNotWritableReadyIfUnchanged(stalePre);

    EXPECT_FALSE(ok);
    EXPECT_EQ(sockBase->GetVersionedWritableReady(), 0x1U);
}

/* ReadyAndExchange：当前 ready → 清 bit0 并返回 true */
TEST_F(SocketBaseTest, ReadyAndExchange_WhenReady_ClearsBitAndReturnsTrue)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    ASSERT_TRUE(sockBase->SetWritableReady()); /* 置为 ready（版本 1，bit0=1） */

    bool ok = sockBase->ReadyAndExchange();

    EXPECT_TRUE(ok);
    EXPECT_FALSE(sockBase->Ready());
    EXPECT_EQ(sockBase->Version(), 1U); /* 版本不变，仅 bit0 被清 */
}

/* ReadyAndExchange：当前非 ready → 返回 false */
TEST_F(SocketBaseTest, ReadyAndExchange_WhenNotReady_ReturnsFalse)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    ASSERT_TRUE(sockBase->SetNotWritableReadyIfUnchanged(sockBase->GetVersionedWritableReady()));

    bool ok = sockBase->ReadyAndExchange();

    EXPECT_FALSE(ok);
}

/* SetEpollData/GetEpollData 往返 */
TEST_F(SocketBaseTest, EpollData_RoundTrip)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    epoll_data_t data = {};
    data.u64 = 0xDEADBEEFCAFEBABEU;
    sockBase->SetEpollData(data);

    EXPECT_EQ(sockBase->GetEpollData().u64, data.u64);
}

/* GetAddedEpollFd：返回已发布的 epoll fd 与 data */
TEST_F(SocketBaseTest, GetAddedEpollFd_ReturnsPublishedFdAndData)
{
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    epoll_data_t inData = {};
    inData.fd = TEST_FD;
    sockBase->SetAddedEpollFd(aep, inData);

    epoll_data_t outData = {};
    EventPoll *outFd = sockBase->GetAddedEpollFd(outData);

    EXPECT_EQ(outFd, aep);
    EXPECT_EQ(outData.fd, TEST_FD);
    delete aep;
}

/* HandshakeCtx 析构的 delete connector / connector = nullptr 分支：
 * 隐式析构路径（delete socket）会把 dtor 内联进其它 TU，gcov 无法归因到本行，
 * 因此在本 TU 直接 new/delete 一个独立的 HandshakeCtx（dtor 在本 TU 内联出码），
 * 使其落入本 TU 的行覆盖。 */
TEST_F(SocketBaseTest, HandshakeCtxDtor_DeletesConnector)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketPtr base = ToSocketPtr(sock);
    SocketBase *sockBase = dynamic_cast<SocketBase *>(base.Get());
    ASSERT_NE(sockBase, nullptr);
    ASSERT_EQ(sockBase->EnsureConnector(base), UBS_OK);
    SocketBase::HandshakeCtx *srcHs = sockBase->Hs();
    ASSERT_NE(srcHs, nullptr);
    ASSERT_NE(srcHs->connector, nullptr);

    /* 独立 HandshakeCtx：把真实 connector 迁入，避免 socket 析构时对同一指针重复 delete */
    SocketBase::HandshakeCtx *hs = new SocketBase::HandshakeCtx();
    hs->connector = srcHs->connector;
    srcHs->connector = nullptr;

    delete hs; /* 触发 delete connector + connector = nullptr */
}

/* 构造+析构 trace 路径：UBS_MONITOR_ENABLE 下分配 stats_mgr，client 角色走活跃连接计数 */
TEST_F(SocketBaseTest, ConstructorTraceEnabled_DestructorReleasesActiveConnCount)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    {
        umq::UmqSocketPtr sock = NewUmqSocket();
        SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
        ASSERT_NE(sockBase, nullptr);

        EXPECT_NE(sockBase->GetStatsMgr(), nullptr); /* 构造时按 trace 使能分配 */
        sockBase->EnsureExt()->conn_info.type_fd = 1; /* 标记为 client 角色 */
    }
    GlobalSetting::UBS_MONITOR_ENABLE = false;
}

/* NotifyReadable：尚未注册 epoll → 返回错误 */
TEST_F(SocketBaseTest, NotifyReadable_NoEpollRegistered_ReturnsError)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    int ret = sockBase->NotifyReadable(false);

    EXPECT_EQ(ret, -1);
}

/* NotifyWritable：已关注 EPOLLOUT 但未注册 epoll → DoNotifyWritable 返回错误 */
TEST_F(SocketBaseTest, NotifyWritable_NoEpollRegistered_DoNotifyWritableReturnsError)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    sockBase->SetEvents(EPOLLOUT);

    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, -1);
}

/* NotifyWritable：DoNotifyWritable 成功路径（AddReadableEvent 成功 → SetReadableEventFd） */
TEST_F(SocketBaseTest, NotifyWritable_AddReadableEventOk_Dispatches)
{
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    MOCKER_CPP(&AsyncEventPoll::AddReadableEvent).stubs().will(returnValue(0));
    MOCKER_CPP(&AsyncEventPoll::SetReadableEventFd).stubs().will(returnValue(0));

    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    sockBase->SetEvents(EPOLLOUT);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, 0);
    delete aep;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
}

/* NotifyWritable：上层未关注 EPOLLOUT → SetWritableReady 置位后返回 0 */
TEST_F(SocketBaseTest, NotifyWritable_UpperNotWatchingEpollout_SetsReadyAndReturnsZero)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(sockBase->Version(), 1U);
    EXPECT_TRUE(sockBase->Ready());
}

/* NotifyWritable：RNR 反压中（CanNotifyWritable=false）→ 不派发直接返回 0 */
TEST_F(SocketBaseTest, NotifyWritable_RnrBlocked_ReturnsZeroWithoutDispatch)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    sock->SetRnrBlocked(true); /* CanNotifyWritable() → !IsRnrBlocked() → false */
    int ret = sockBase->NotifyWritable();

    EXPECT_EQ(ret, 0);
    sock->SetRnrBlocked(false);
}

/* NotifyWritable：与 EpollCtlMod 竞态（另一线程在两次 load 之间置位 EPOLLOUT）
 * → 命中 ReadyAndExchange 分支并派发 */
TEST_F(SocketBaseTest, NotifyWritable_RaceEpollCtlMod_DispatchesViaReadyAndExchange)
{
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    MOCKER_CPP(&AsyncEventPoll::AddReadableEvent).stubs().will(returnValue(0));
    MOCKER_CPP(&AsyncEventPoll::SetReadableEventFd).stubs().will(returnValue(0));

    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    epoll_data_t data = {};
    sockBase->SetAddedEpollFd(aep, data);

    std::atomic<bool> stop{false};
    std::atomic<bool> armed{false};
    /* 模拟另一线程 EpollCtlMod：events_ 为 std::atomic，并发 store 无数据竞争(UB)；
     * 仅在主线程 armed 期间持续置位 EPOLLOUT，把竞态窗口集中在 NotifyWritable
     * 内部对 events_ 的两次 load 之间，降低无谓的空转。 */
    std::thread racer([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            if (armed.load(std::memory_order_relaxed)) {
                sockBase->SetEvents(EPOLLOUT);
            }
        }
    });

    bool raced = false;
    for (int i = 0; i < 500000 && !raced; ++i) {
        sockBase->SetEvents(0); /* 让 NotifyWritable 进入"未关注 EPOLLOUT"分支 */
        armed.store(true, std::memory_order_relaxed);
        sockBase->NotifyWritable();
        armed.store(false, std::memory_order_relaxed);
        /* 竞态命中时 ReadyAndExchange 已把 bit0 清 0 */
        if (!sockBase->Ready()) {
            raced = true;
        }
    }
    stop.store(true, std::memory_order_relaxed);
    racer.join();

    delete aep;
    GlobalSetting::UBS_POLLER_OPS = nullptr;
    EXPECT_TRUE(raced);
}

/* 基类默认虚函数：通过限定调用触达 SocketBase 未被派生类覆盖路径覆盖的内联虚体
 * （ReleaseDataPlane/OnEstablished/CanNotifyWritable/FatalIfWriteBlocked） */
TEST_F(SocketBaseTest, BaseDefaultVirtuals_QualifiedCalls)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    EXPECT_TRUE(sockBase->SocketBase::CanNotifyWritable());
    EXPECT_FALSE(sockBase->SocketBase::FatalIfWriteBlocked());
    sockBase->SocketBase::OnEstablished();
    sockBase->SocketBase::ReleaseDataPlane();
}

/* WriteV：装配数据面条目（DataPlaneTable::Live 非空）→ 走 UMQ 数据面转发到
 * DataTx::WriteV（mock 屏蔽真实 URMA 下发） */
TEST_F(SocketBaseTest, WriteV_LiveDataPlaneEntry_ForwardsToDataTx)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);

    /* ReinitTxOps 装配整条目，使 DataPlaneTable::Live(raw_socket_) 非空 */
    ASSERT_NE(sock->ReinitTxOps(), nullptr);
    MOCKER_CPP(&DataTx::WriteV).stubs().will(returnValue(7));

    struct iovec iov[1] = {};
    ssize_t ret = sockBase->WriteV(base, iov, 1);

    EXPECT_EQ(ret, 7);
}

/* ReadV：装配数据面条目（Live 非空）→ 走 UMQ 数据面转发到 DataRx::ReadV（mock 屏蔽真实 URMA 收取） */
TEST_F(SocketBaseTest, ReadV_LiveDataPlaneEntry_ForwardsToDataRx)
{
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);
    SocketPtr base = ToSocketPtr(sock);

    ASSERT_NE(sock->ReinitTxOps(), nullptr);
    MOCKER_CPP(&DataRx::ReadV).stubs().will(returnValue(9));

    struct iovec iov[1] = {};
    ssize_t ret = sockBase->ReadV(base, iov, 1);

    EXPECT_EQ(ret, 9);
}

/* GetAddedEpollFd/SetAddedEpollFd：取成员函数地址（odr-use）强制在该 TU 内生成
 * ALWAYS_INLINE 函数的 out-of-line 副本并实际调用，覆盖 gcov 归因到函数收尾行 */
TEST_F(SocketBaseTest, GetSetAddedEpollFd_OdrUseEmitsOutOfLineCopy)
{
    auto *aep = new AsyncEventPoll(TEST_EPOLL_FD);
    umq::UmqSocketPtr sock = NewUmqSocket();
    SocketBase *sockBase = dynamic_cast<SocketBase *>(sock.Get());
    ASSERT_NE(sockBase, nullptr);

    EventPoll *(SocketBase::*getPmf)(epoll_data_t &) const = &SocketBase::GetAddedEpollFd;
    void (SocketBase::*setPmf)(EventPoll *, const epoll_data_t &) = &SocketBase::SetAddedEpollFd;

    epoll_data_t inData = {};
    inData.fd = TEST_FD;
    (sockBase->*setPmf)(aep, inData);

    epoll_data_t outData = {};
    EventPoll *outFd = (sockBase->*getPmf)(outData);

    EXPECT_EQ(outFd, aep);
    EXPECT_EQ(outData.fd, TEST_FD);
    delete aep;
}
