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
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_set.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_connector.h"
#include "core/ubsocket_socket_helper.h"
#include "core/umq/umq_socket_connector.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

namespace {
static const int TEST_FD = 42;

/* ============ ConnectorOps mock ============ */

/* 已移除 FakeConnectorOps 测试替身, 直接对真实 UmqConnectorOps 打桩(仅依赖 mockcpp)。
 * ConnectorOps 为纯虚接口, PrepareConnect/Negotiate/CreateSocketResources 均为虚成员函数。
 * 虚成员函数不能用 MOCKER_CPP(&Class::method): member-pointer 编码成 vtable 偏移, 被当作代码地址
 * 打桩会触发 SEGV(见 modules/core.md 陷阱 6)。正确写法是 MOCKER_CPP_VIRTUAL(实例, &Class::method):
 * 从实例 vtable 槽位读出真实函数地址再在函数级打桩, Connect() 经 vtable 分发到该地址即被拦截。
 * UmqConnectorOps 实例由 Connector 以 Ref 持有, 保证注册打桩到 GlobalMockObject::verify() 期间存活。 */

/* SocketBase 具体实现: 建链成功钩子 OnEstablished 用计数验证 */
class TestSocket : public SocketBase {
public:
    static int g_establishedCount;

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

    void OnEstablished() override
    {
        g_establishedCount++;
    }
};

int TestSocket::g_establishedCount = 0;

/* ============ LibcApi 替身 ============ */

static int g_fcntlCallCount = 0;
static bool g_fcntlSetflCalled = false;

/* flags=0(阻塞): IsBlocking=true; SetBlocking 内 F_GETFL 命中后不再 F_SETFL */
static int MockFcntlBlocking(int, int cmd, unsigned long)
{
    g_fcntlCallCount++;
    if (cmd == F_SETFL) {
        g_fcntlSetflCalled = true;
    }
    return 0;
}

/* flags=O_NONBLOCK(非阻塞): IsBlocking=false, SetBlocking 不会调用 */
static int MockFcntlNonBlocking(int, int cmd, unsigned long)
{
    g_fcntlCallCount++;
    if (cmd == F_SETFL) {
        g_fcntlSetflCalled = true;
    }
    return O_NONBLOCK;
}

static void ResetFakeState()
{
    g_fcntlCallCount = 0;
    g_fcntlSetflCalled = false;
    TestSocket::g_establishedCount = 0;
}

static void SetLibcApiPtrsToNull()
{
    LibcApi::fcntl_ptr = nullptr;
    LibcApi::recv_ptr = nullptr;
}

static sockaddr_in MakeSockAddr()
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}
} // namespace

class ConnectorConnectTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        ResetFakeState();
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;

        /* LibcApi 指针默认安装安全 fake, 防止未设置即调用的 segfault */
        LibcApi::fcntl_ptr = MockFcntlBlocking;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        SetLibcApiPtrsToNull();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        errno = 0;
    }

    /* 每次返回独立的堆分配 socket, 由 Ref 生命周期自动回收 */
    SocketPtr MakeSocketBase()
    {
        return SocketPtr(new (std::nothrow) TestSocket(TEST_FD));
    }
};

// ==================== Connect ====================

/* connector_ops_ 已释放(协商 ops 随上次建链成功释放): 重复 connect 直接以 EISCONN 拒绝 */
TEST_F(ConnectorConnectTest, Connect_OpsReleased_ReturnsMinusOneErrnoEisconn)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);

    /* 协商 ops 将释放: 三个阶段均不应被调用(打桩须在 ReleaseOps 析构实例前注册) */
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    Connector connector(sock, ops);
    connector.ReleaseOps(); /* 模拟 OnEstablished 已释放协商 ops */

    errno = 0;
    int ret = connector.Connect(sock, nullptr, 0);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EISCONN);
    GlobalMockObject::verify();
}

/* PrepareConnect 返回非 0 且非 -1(UBS_ERROR): 设置 EBADE */
TEST_F(ConnectorConnectTest, Connect_PrepareConnectFailNonMinusOne_ErrnoEbade)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LibcApi::recv).stubs().will(returnValue(static_cast<ssize_t>(0)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADE);
    EXPECT_EQ(TestSocket::g_establishedCount, 0);
    GlobalMockObject::verify();
}

/* PrepareConnect 返回 -1: 不设置 EBADE(errno 保持 FlushSocketMsg 后的 0) */
TEST_F(ConnectorConnectTest, Connect_PrepareConnectFailMinusOne_KeepsErrnoZero)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(-1)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LibcApi::recv).stubs().will(returnValue(static_cast<ssize_t>(0)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, 0);
    GlobalMockObject::verify();
}

/* Negotiate 失败: 跳过 CreateSocketResources, 设置 EBADE */
TEST_F(ConnectorConnectTest, Connect_NegotiateFail_SkipsCreateResources_ErrnoEbade)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(never())
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER(&LibcApi::recv).stubs().will(returnValue(static_cast<ssize_t>(0)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADE);
    EXPECT_EQ(TestSocket::g_establishedCount, 0);
    GlobalMockObject::verify();
}

/* CreateSocketResources 失败: 设置 EBADE */
TEST_F(ConnectorConnectTest, Connect_CreateResourcesFail_ErrnoEbade)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_ERROR)));
    MOCKER(&LibcApi::recv).stubs().will(returnValue(static_cast<ssize_t>(0)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EBADE);
    EXPECT_EQ(TestSocket::g_establishedCount, 0);
    GlobalMockObject::verify();
}

/* 全阶段成功 + 阻塞模式: OnEstablished 被调用, socket 身份就位 */
TEST_F(ConnectorConnectTest, Connect_Success_Blocking_CallsOnEstablished)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(TestSocket::g_establishedCount, 1);
    EXPECT_TRUE(sock->IsClient()); /* conn_info.type_fd==1 标记 client 角色 */
    EXPECT_EQ(sock->create_type_, SOCK_CREATE_TYPE_CONNECT);
    /* 阻塞: IsBlocking + SetBlocking 各一次 F_GETFL, 不再 F_SETFL */
    EXPECT_EQ(g_fcntlCallCount, 2);
    EXPECT_FALSE(g_fcntlSetflCalled);
    GlobalMockObject::verify();
}

/* 全阶段成功 + 非阻塞模式: 跳过 SetBlocking */
TEST_F(ConnectorConnectTest, Connect_Success_NonBlocking_SkipsSetBlocking)
{
    LibcApi::fcntl_ptr = MockFcntlNonBlocking;
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    Connector connector(sock, ops);
    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(TestSocket::g_establishedCount, 1);
    /* 非阻塞: 仅 IsBlocking 一次 F_GETFL, SetBlocking 未调用 */
    EXPECT_EQ(g_fcntlCallCount, 1);
    EXPECT_FALSE(g_fcntlSetflCalled);
    GlobalMockObject::verify();
}

/* 成功路径 + UBS_MONITOR_ENABLE + SocketBase: 全局统计 +1 */
TEST_F(ConnectorConnectTest, Connect_Success_TraceEnabled_SocketBase_UpdateTraceStats)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::PrepareConnect)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::Negotiate)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ops, &ock::ubs::umq::UmqConnectorOps::CreateSocketResources)
        .expects(exactly(1))
        .will(returnValue(static_cast<ock::ubs::Result>(UBS_OK)));

    Connector connector(sock, ops);
    uint32_t connBefore = Statistics::StatsMgr::GetConnCount();
    uint32_t activeBefore = Statistics::StatsMgr::GetActiveConnCount();

    sockaddr_in addr = MakeSockAddr();
    int ret = connector.Connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

    EXPECT_EQ(ret, 0);
    EXPECT_EQ(TestSocket::g_establishedCount, 1);
    EXPECT_EQ(Statistics::StatsMgr::GetConnCount(), connBefore + 1);
    EXPECT_EQ(Statistics::StatsMgr::GetActiveConnCount(), activeBefore + 1);
    GlobalMockObject::verify();
}

// ==================== 析构 ====================

/* ~Connector 为空实现(统计递减已收敛到 SocketBase 析构), 用例仅验证销毁路径无异常 */
TEST_F(ConnectorConnectTest, Destructor_TraceEnabled_SubCounts)
{
    GlobalSetting::UBS_MONITOR_ENABLE = true;
    SocketPtr sock = MakeSocketBase();
    {
        Connector connector(sock, new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD));
    } /* 作用域结束触发 ~Connector */
    GlobalMockObject::verify();
}

// ==================== GetConnectorOps ====================

/* 建链中(协商 ops 未释放) GetConnectorOps 返回持有中的 ops(非空) */
TEST_F(ConnectorConnectTest, GetConnectorOps_ReturnsOps)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);

    Connector connector(sock, ops);
    ConnectorOpsPtr got = connector.GetConnectorOps();

    EXPECT_EQ(got.Get(), ops);
    GlobalMockObject::verify();
}

/* ReleaseOps 后协商 ops 已释放, GetConnectorOps 返回空 */
TEST_F(ConnectorConnectTest, GetConnectorOps_AfterRelease_ReturnsNull)
{
    SocketPtr sock = MakeSocketBase();
    ock::ubs::umq::UmqConnectorOps *ops = new (std::nothrow) ock::ubs::umq::UmqConnectorOps(TEST_FD);

    Connector connector(sock, ops);
    connector.ReleaseOps();
    ConnectorOpsPtr got = connector.GetConnectorOps();

    EXPECT_EQ(got.Get(), nullptr);
    GlobalMockObject::verify();
}
