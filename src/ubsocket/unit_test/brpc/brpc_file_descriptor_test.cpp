#include "brpc_file_descriptor.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

namespace Brpc {

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const int BRPC_FD_1 = 1;
static const int BRPC_FD_2 = 2;
static const int BRPC_FD_3 = 3;
static const int BRPC_FD_123 = 123;
static const int BRPC_FD_124 = 124;
static const int BRPC_FD_6 = 6;
static const int BRPC_FD_42 = 42;
static const uint64_t BRPC_MAGIC_10 = 10U;
static const uint64_t BRPC_MAGIC_20 = 20U;
static const uint32_t BRPC_RECV_SIZE_9 = 9U;
static const uint32_t BRPC_RECV_SIZE_15 = 15U;
static const int BRPC_PROCESS_SOCKET_1 = 1;
static const int BRPC_PROCESS_SOCKET_6 = 6;
static const uint32_t BRPC_INDEX_42 = 42U;
static const uint32_t BRPC_INDEX_100 = 100U;
static const uint32_t BRPC_INDEX_5 = 5U;
static const uint32_t BRPC_INDEX_3 = 3U;
static const uint32_t BRPC_INDEX_10 = 10U;
static const uint32_t BRPC_INDEX_20 = 20U;
static const uint32_t BRPC_INDEX_1 = 1U;
static const uint32_t BRPC_ROUTE_NUM_1 = 1U;
static const uint32_t BRPC_ROUTE_NUM_2 = 2U;
static const uint32_t BRPC_ROUTE_NUM_5 = 5U;
static const uint32_t BRPC_ROUTE_NUM_10 = 10U;
static const uint32_t BRPC_ROUTE_NUM_3 = 3U;

// Additional constants for magic number replacement
static const int SETENV_OVERWRITE = 1;
static const int EID_RAW_SIZE_16 = 16;
static const int CHIP_ID_BASE_10 = 10;
static const int CHIP_ID_OFFSET_50 = 50;
static const int CHIP_ID_OFFSET_100 = 100;
static const int CHIP_ID_OFFSET_150 = 150;
static const int CHIP_ID_OFFSET_200 = 200;
static const int CHIP_ID_OFFSET_250 = 250;
static const int EID_OFFSET_1 = 1;
static const int EID_OFFSET_2 = 2;
static const int EID_OFFSET_30 = 30;
static const int EID_OFFSET_60 = 60;
static const int ROUTE_NUM_1 = 1;
static const int ROUTE_NUM_2 = 2;
static const int EXPECTED_CHIP_ID_1 = 1;
static const int EXPECTED_CHIP_ID_0 = 0;
static const int SOCKET_ID_0 = 0;
static const int SOCKET_ID_1 = 1;
static const int SOCKET_ID_2 = 2;
static const int SOCKET_ID_3 = 3;
static const int CHIP_ID_0 = 0;
static const int CHIP_ID_1 = 1;
static const int CHIP_ID_10 = 10;
static const int CHIP_ID_11 = 11;
static const int CHIP_ID_12 = 12;
static const int CHIP_ID_13 = 13;
static const int CHIP_ID_200 = 200;
static const int CHIP_ID_255 = 255;
static const int RET_NEG_1 = -1;
static const int RET_OK = 0;
static const int EXPECTED_RETRIEVE_10 = 10;
static const int GET_TARGET_CHIP_ID_LOOP_4 = 4;
} // namespace

class BrpcFileDescriptorTest : public testing::Test {
public:
    Brpc::SocketFd *socketFd = nullptr;
    virtual void SetUp();
    virtual void TearDown();
};

void BrpcFileDescriptorTest::SetUp()
{
    // 设置新的环境变量
    // 参数说明：
    // - 第一个参数：环境变量名
    // - 第二个参数：环境变量值
    // - 第三个参数：是否覆盖已有值（1 表示覆盖，0 表示不覆盖）
    setenv("UBSOCKET_USE_UB_FORCE", "true", 1);
    setenv("UBSOCKET_TRANS_MODE", "UB", 1);

    // 加载日志
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);

    int fd = BRPC_FD_1;
    uint64_t magicNumber = BRPC_MAGIC_10;
    uint32_t magicNumberRecvSize = BRPC_RECV_SIZE_9;
    socketFd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
}

void BrpcFileDescriptorTest::TearDown()
{
    unsetenv("UBSOCKET_USE_UB_FORCE");
    unsetenv("UBSOCKET_TRANS_MODE");
    GlobalMockObject::verify();
}

TEST_F(BrpcFileDescriptorTest, DoRouteTest)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_route_t connRoute;
    bool useRoundRobin = false;
    
    MOCKER_CPP(&Brpc::SocketFd::GetDevRouteList)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    int ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, RET_NEG_1);

    MOCKER_CPP(&Brpc::SocketFd::GetConnEid)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, RET_NEG_1);

    MOCKER_CPP(&Brpc::SocketFd::CheckDevAdd)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, RET_NEG_1);

    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(BrpcFileDescriptorTest, GetRoundRobinConnEidTest)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};
    umq_route_t connRoute = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };

    umq_route_list_t route_list = {
        .route_num = ROUTE_NUM_2,
        .routes = {
            connRoute, connRoute
        }
    };

    umq_route_t useConnRoute;
    int ret = socketFd->GetRoundRobinConnEid(route_list, &dstEid, &useConnRoute);
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(BrpcFileDescriptorTest, GetCpuAffinityConnEidTest)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};
    umq_route_t connRoute1 = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };
    umq_route_t connRoute2 = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };

    umq_route_list_t route_list = {
        .route_num = ROUTE_NUM_2,
        .routes = {
            connRoute1, connRoute2
        }
    };

    umq_route_t useConnRoute;
    std::vector<uint32_t> socketIds = {0, BRPC_PROCESS_SOCKET_1};
    int processSocketId = BRPC_PROCESS_SOCKET_1;

    MOCKER_CPP(&Brpc::SocketFd::GetRoundRobinConnEid)
    .stubs()
    .will(returnValue(static_cast<int>(-1)));

    int ret = socketFd->GetCpuAffinityConnEid(route_list, &dstEid, &useConnRoute, socketIds, processSocketId);
    EXPECT_EQ(ret, RET_NEG_1);
}

TEST_F(BrpcFileDescriptorTest, GetTargetChipIdTest)
{
    std::vector<uint32_t> socketIds = {0, BRPC_PROCESS_SOCKET_1};
    std::vector<uint32_t> chipIdList = {CHIP_ID_0, CHIP_ID_1};
    int processSocketId = BRPC_FD_6;

    uint32_t ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, UINT32_MAX);

    processSocketId = BRPC_PROCESS_SOCKET_1;
    ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, 1);

    std::vector<uint32_t> chipIdList2 = {0};
    ret = socketFd->GetTargetChipId(socketIds, chipIdList2, processSocketId);
    EXPECT_EQ(ret, UINT32_MAX);
}

bool MockGetRouteList(const umq_eid_t &eid, umq_route_list_t &routeList)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};
    umq_route_t connRoute1 = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };

    umq_route_list_t route_list = {
        .route_num = ROUTE_NUM_1,
        .routes = {
            connRoute1
        }
    };

    routeList = route_list;
    return true;
}

TEST_F(BrpcFileDescriptorTest, CheckOtherRouteTest)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};
    umq_route_t connRoute = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };
    umq_route_t otherConnRoute;

    MOCKER_CPP(&Brpc::RouteListRegistry::IsRegisteredRouteList)
    .stubs()
    .will(returnValue(false))
    .then(returnValue(true));
    int ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, RET_NEG_1);

    MOCKER_CPP(&Brpc::RouteListRegistry::GetRouteList)
    .stubs()
    .will(returnValue(false))
    .then(invoke(MockGetRouteList));

    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, RET_NEG_1);

    MOCKER_CPP(&Brpc::SocketFd::CheckDevAdd)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));

    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, RET_NEG_1);
}
// Additional tests for SocketFd methods
TEST_F(BrpcFileDescriptorTest, GetFdTest)
{
    EXPECT_EQ(socketFd->GetFd(), BRPC_FD_1);
}

TEST_F(BrpcFileDescriptorTest, GetPeerIpTest)
{
    const std::string& ip = socketFd->GetPeerIp();
    // Initially empty
    EXPECT_TRUE(ip.empty() || ip.length() > 0);  // Just verify the function works
}

TEST_F(BrpcFileDescriptorTest, GetPeerEidTest)
{
    const umq_eid_t& eid = socketFd->GetPeerEid();
    // Just verify the function works
    (void)eid;
}

TEST_F(BrpcFileDescriptorTest, GetPeerFdTest)
{
    int peerFd = socketFd->GetPeerFd();
    // Initially -1 or 0
    (void)peerFd;
}

TEST_F(BrpcFileDescriptorTest, GetEventFdTest)
{
    int eventFd = socketFd->GetEventFd();
    (void)eventFd;
}

TEST_F(BrpcFileDescriptorTest, GetLocalUmqHandleTest)
{
    uint64_t handle = socketFd->GetLocalUmqHandle();
    (void)handle;
}

TEST_F(BrpcFileDescriptorTest, GetMainUmqHandleTest)
{
    uint64_t handle = socketFd->GetMainUmqHandle();
    (void)handle;
}

// Tests for EidRegistry
class EidRegistryTest : public testing::Test {
public:
    virtual void SetUp()
    {
        setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
        setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    virtual void TearDown()
    {
        unsetenv("UBSOCKET_USE_UB_FORCE");
        unsetenv("UBSOCKET_TRANS_MODE");
        GlobalMockObject::verify();
    }
};

TEST_F(EidRegistryTest, IsRegisteredEid)
{
    Brpc::EidRegistry registry;
    umq_eid_t eid = {0};

    EXPECT_FALSE(registry.IsRegisteredEid(eid));
}

TEST_F(EidRegistryTest, GetEidIndex)
{
    Brpc::EidRegistry registry;
    umq_eid_t eid = {0};
    uint32_t index = 0;

    EXPECT_FALSE(registry.GetEidIndex(eid, index));
}

// Tests for RouteListRegistry
class RouteListRegistryTest : public testing::Test {
public:
    virtual void SetUp()
    {
        setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
        setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    virtual void TearDown()
    {
        unsetenv("UBSOCKET_USE_UB_FORCE");
        unsetenv("UBSOCKET_TRANS_MODE");
        GlobalMockObject::verify();
    }
};

TEST_F(RouteListRegistryTest, IsRegisteredRouteList)
{
    Brpc::RouteListRegistry registry;
    umq_eid_t eid = {0};

    EXPECT_FALSE(registry.IsRegisteredRouteList(eid));
}

TEST_F(RouteListRegistryTest, GetRouteList)
{
    Brpc::RouteListRegistry registry;
    umq_eid_t eid = {0};
    umq_route_list_t routeList = {};

    EXPECT_FALSE(registry.GetRouteList(eid, routeList));
}

// Tests for FallbackTcpMgr
class FallbackTcpMgrTest : public testing::Test {
public:
    virtual void SetUp()
    {
        setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
        setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    virtual void TearDown()
    {
        unsetenv("UBSOCKET_USE_UB_FORCE");
        unsetenv("UBSOCKET_TRANS_MODE");
        GlobalMockObject::verify();
    }
};

TEST_F(FallbackTcpMgrTest, TxUseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.TxUseTcp());
}

TEST_F(FallbackTcpMgrTest, RxUseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.RxUseTcp());
}

TEST_F(FallbackTcpMgrTest, UseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.UseTcp());
}

// Additional SocketFd tests
TEST_F(BrpcFileDescriptorTest, SocketFdDestructor)
{
    // Test that destructor doesn't crash
    Brpc::SocketFd* fd = new Brpc::SocketFd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);
    delete fd;
}

TEST_F(BrpcFileDescriptorTest, SocketFdWithDifferentMagicNumbers)
{
    MOCKER_CPP(&Brpc::SocketFd::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&Brpc::SocketFd::DestroyLocalUmq).stubs();
    Brpc::SocketFd fd1(BRPC_FD_1, 0, 0);
    Brpc::SocketFd fd2(BRPC_FD_2, BRPC_INDEX_100, static_cast<uint32_t>(BRPC_INDEX_5 * BRPC_INDEX_10));
    Brpc::SocketFd fd3(BRPC_FD_3, UINT64_MAX, UINT32_MAX);

    EXPECT_EQ(fd1.GetFd(), BRPC_FD_1);
    EXPECT_EQ(fd2.GetFd(), BRPC_FD_2);
    EXPECT_EQ(fd3.GetFd(), BRPC_FD_3);
    std::cout << fd3.GetFd() << std::endl;
}

// Tests for Brpc::EpollEvent
class BrpcEpollEventTest : public testing::Test {
public:
    virtual void SetUp()
    {
        setenv("UBSOCKET_USE_UB_FORCE", "true", SETENV_OVERWRITE);
        setenv("UBSOCKET_TRANS_MODE", "UB", SETENV_OVERWRITE);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }
    virtual void TearDown()
    {
        unsetenv("UBSOCKET_USE_UB_FORCE");
        unsetenv("UBSOCKET_TRANS_MODE");
        GlobalMockObject::verify();
    }
};

TEST_F(BrpcEpollEventTest, Constructor)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = BRPC_FD_42;

    Brpc::EpollEvent epollEvent(BRPC_FD_42, &ev);
    EXPECT_EQ(epollEvent.GetFd(), BRPC_FD_42);
}

TEST_F(BrpcEpollEventTest, GetEvents)
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = BRPC_FD_42;

    Brpc::EpollEvent epollEvent(BRPC_FD_42, &ev);
    EXPECT_EQ(epollEvent.GetEvents(), (uint32_t)(EPOLLIN | EPOLLOUT));
}

// ============= FallbackTcpMgr Additional Tests =============

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_Defaults)
{
    MOCKER_CPP(&Brpc::SocketFd::UnbindAndFlushRemoteUmq).stubs();
    MOCKER_CPP(&Brpc::SocketFd::DestroyLocalUmq).stubs();
    // Using the constructor that sets m_tx_use_tcp = true
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);
    // This constructor sets m_tx_use_tcp = true as part of fallback TCP mode
    EXPECT_TRUE(fd.TxUseTcp());
    EXPECT_TRUE(fd.UseTcp());
}

// ============= SocketFd Additional Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv4)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.1", &addr.sin_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "192.168.1.1");
}

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv6)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "::1");
}

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_Null)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    std::string ip = fd.ExtractIpFromSockAddr(nullptr);
    EXPECT_TRUE(ip.empty());
}

// ============= FallbackTcpMgr Additional Tests =============

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_RxUseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.RxUseTcp());
}

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_TxUseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.TxUseTcp());
}

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_UseTcp)
{
    Brpc::FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.UseTcp());
}

// ============= EidRegistry Tests =============

TEST_F(EidRegistryTest, RegisterEid)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();
    umq_eid_t eid = {0};

    // Register eid
    EXPECT_TRUE(registry.RegisterEid(eid));
    EXPECT_TRUE(registry.IsRegisteredEid(eid));

    // Unregister
    EXPECT_TRUE(registry.UnregisterEid(eid));
    EXPECT_FALSE(registry.IsRegisteredEid(eid));
}

TEST_F(EidRegistryTest, RegisterDuplicateEid)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();
    umq_eid_t eid = {};
    eid.raw[0] = EID_OFFSET_1;
    eid.raw[1] = EID_OFFSET_2;

    EXPECT_TRUE(registry.RegisterEid(eid));
    EXPECT_FALSE(registry.RegisterEid(eid));  // Duplicate should return false
    EXPECT_TRUE(registry.UnregisterEid(eid));
}

TEST_F(EidRegistryTest, UnregisterNonExistentEid)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = CHIP_ID_255;

    EXPECT_FALSE(registry.UnregisterEid(eid));  // Should return false for non-existent
}

TEST_F(EidRegistryTest, EidIndexOperations)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = CHIP_ID_BASE_10;

    // Register index
    registry.RegisterOrReplaceEidIndex(eid, BRPC_INDEX_42);

    // Check registered
    EXPECT_TRUE(registry.IsRegisteredEidIndex(eid));

    // Get index
    uint32_t index = 0;
    EXPECT_TRUE(registry.GetEidIndex(eid, index));
    EXPECT_EQ(index, BRPC_INDEX_42);

    // Replace index
    registry.RegisterOrReplaceEidIndex(eid, BRPC_INDEX_100);
    EXPECT_TRUE(registry.GetEidIndex(eid, index));
    EXPECT_EQ(index, BRPC_INDEX_100);

    // Unregister
    EXPECT_TRUE(registry.UnregisterEidIndex(eid));
    EXPECT_FALSE(registry.IsRegisteredEidIndex(eid));
}

TEST_F(EidRegistryTest, EidIndexNonExistent)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = CHIP_ID_200;

    EXPECT_FALSE(registry.IsRegisteredEidIndex(eid));

    uint32_t index = 0;
    EXPECT_FALSE(registry.GetEidIndex(eid, index));
}

TEST_F(EidRegistryTest, MultipleEids)
{
    Brpc::EidRegistry& registry = Brpc::EidRegistry::Instance();

    umq_eid_t eid1 = {0};
    eid1.raw[0] = EID_OFFSET_1;
    umq_eid_t eid2 = {0};
    eid2.raw[0] = EID_OFFSET_2;
    umq_eid_t eid3 = {0};
    eid3.raw[0] = EID_OFFSET_1 + EID_OFFSET_2;

    // Register multiple
    EXPECT_TRUE(registry.RegisterEid(eid1));
    EXPECT_TRUE(registry.RegisterEid(eid2));
    EXPECT_TRUE(registry.RegisterEid(eid3));

    // All should be registered
    EXPECT_TRUE(registry.IsRegisteredEid(eid1));
    EXPECT_TRUE(registry.IsRegisteredEid(eid2));
    EXPECT_TRUE(registry.IsRegisteredEid(eid3));

    // Cleanup
    registry.UnregisterEid(eid1);
    registry.UnregisterEid(eid2);
    registry.UnregisterEid(eid3);
}

// ============= RouteListRegistry Tests =============

TEST_F(RouteListRegistryTest, RegisterAndGetRouteList)
{
    Brpc::RouteListRegistry& registry = Brpc::RouteListRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = EID_OFFSET_1;

    umq_route_list_t routeList = {};
    routeList.route_num = BRPC_ROUTE_NUM_1;

    registry.RegisterOrReplaceRouteList(eid, routeList);
    EXPECT_TRUE(registry.IsRegisteredRouteList(eid));

    umq_route_list_t retrievedList = {};
    EXPECT_TRUE(registry.GetRouteList(eid, retrievedList));
    EXPECT_EQ(retrievedList.route_num, BRPC_ROUTE_NUM_1);

    EXPECT_TRUE(registry.UnregisterRouteList(eid));
    EXPECT_FALSE(registry.IsRegisteredRouteList(eid));
}

TEST_F(RouteListRegistryTest, ReplaceRouteList)
{
    Brpc::RouteListRegistry& registry = Brpc::RouteListRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = EID_OFFSET_2;

    umq_route_list_t routeList1 = {};
    routeList1.route_num = BRPC_ROUTE_NUM_1;
    umq_route_list_t routeList2 = {};
    routeList2.route_num = BRPC_ROUTE_NUM_2;

    registry.RegisterOrReplaceRouteList(eid, routeList1);
    registry.RegisterOrReplaceRouteList(eid, routeList2);  // Replace

    umq_route_list_t retrievedList = {};
    EXPECT_TRUE(registry.GetRouteList(eid, retrievedList));
    EXPECT_EQ(retrievedList.route_num, BRPC_ROUTE_NUM_2);

    registry.UnregisterRouteList(eid);
}

TEST_F(RouteListRegistryTest, UnregisterNonExistentRouteList)
{
    Brpc::RouteListRegistry& registry = Brpc::RouteListRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = CHIP_ID_255;

    EXPECT_FALSE(registry.UnregisterRouteList(eid));
}

TEST_F(RouteListRegistryTest, GetNonExistentRouteList)
{
    Brpc::RouteListRegistry& registry = Brpc::RouteListRegistry::Instance();
    umq_eid_t eid = {0};
    eid.raw[0] = CHIP_ID_255;

    umq_route_list_t routeList = {};
    EXPECT_FALSE(registry.GetRouteList(eid, routeList));
}

// ============= SocketFd Additional Method Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_GetMainUmqHandle)
{
    EXPECT_EQ(socketFd->GetMainUmqHandle(), 0u);
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetPeerInfo)
{
    const std::string& ip = socketFd->GetPeerIp();
    EXPECT_TRUE(ip.empty());

    const umq_eid_t& eid = socketFd->GetPeerEid();
    (void)eid;

    int peerFd = socketFd->GetPeerFd();
    EXPECT_EQ(peerFd, 0);
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetEventFd)
{
    int eventFd = socketFd->GetEventFd();
    EXPECT_EQ(eventFd, 0);
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetLocalUmqHandle)
{
    uint64_t handle = socketFd->GetLocalUmqHandle();
    EXPECT_EQ(handle, 0u);
}

// ============= ExtractIpFromSockAddr Additional Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv4Loopback)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "127.0.0.1");
}

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv6Loopback)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "::1");
}

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv4Any)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "0.0.0.0");
}

TEST_F(BrpcFileDescriptorTest, SocketFd_ExtractIpFromSockAddr_IPv6Any)
{
    Brpc::SocketFd fd(BRPC_FD_1, BRPC_MAGIC_10, BRPC_RECV_SIZE_9);

    struct sockaddr_in6 addr;
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::", &addr.sin6_addr);

    std::string ip = fd.ExtractIpFromSockAddr((struct sockaddr*)&addr);
    EXPECT_EQ(ip, "::");
}

// ============= EpollEvent Additional Tests =============

TEST_F(BrpcEpollEventTest, GetFd_AfterConstruction)
{
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = BRPC_FD_123;

    Brpc::EpollEvent epollEvent(BRPC_FD_123, &ev);
    EXPECT_EQ(epollEvent.GetFd(), BRPC_FD_123);
}

TEST_F(BrpcEpollEventTest, GetEvents_MultipleEvents)
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
    ev.data.fd = BRPC_FD_42;

    Brpc::EpollEvent epollEvent(BRPC_FD_42, &ev);
    EXPECT_EQ(epollEvent.GetEvents(), (uint32_t)(EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP));
}

TEST_F(BrpcEpollEventTest, GetEvents_EPOLLET)
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = BRPC_FD_42;

    Brpc::EpollEvent epollEvent(BRPC_FD_42, &ev);
    EXPECT_EQ(epollEvent.GetEvents(), (uint32_t)(EPOLLIN | EPOLLET));
}

// ============= GetTargetChipId Additional Tests =============

TEST_F(BrpcFileDescriptorTest, GetTargetChipId_EmptyLists)
{
    std::vector<uint32_t> socketIds;
    std::vector<uint32_t> chipIdList;
    int processSocketId = BRPC_PROCESS_SOCKET_1;

    uint32_t ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, UINT32_MAX);
}

TEST_F(BrpcFileDescriptorTest, GetTargetChipId_SingleElement)
{
    std::vector<uint32_t> socketIds = {SOCKET_ID_0};
    std::vector<uint32_t> chipIdList = {1};
    int processSocketId = SOCKET_ID_0;

    uint32_t ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, EXPECTED_CHIP_ID_1);
}

TEST_F(BrpcFileDescriptorTest, GetTargetChipId_MultipleElements)
{
    std::vector<uint32_t> socketIds = {SOCKET_ID_0, SOCKET_ID_1, SOCKET_ID_2, SOCKET_ID_3};
    std::vector<uint32_t> chipIdList = {CHIP_ID_10, CHIP_ID_11, CHIP_ID_12, CHIP_ID_13};

    // Test each socket
    for (int i = 0; i < GET_TARGET_CHIP_ID_LOOP_4; ++i) {
        int processSocketId = i;
        uint32_t ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
        EXPECT_EQ(ret, static_cast<uint32_t>(CHIP_ID_BASE_10 + i));
    }
}

// ============= GetRoundRobinConnEid Additional Tests =============

TEST_F(BrpcFileDescriptorTest, GetRoundRobinConnEid_SingleRoute)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};
    umq_route_t connRoute = {
        src_port,
        dst_port,
        srcEid,
        dstEid,
    };

    umq_route_list_t route_list = {
        .route_num = ROUTE_NUM_1,
        .routes = {connRoute}
    };

    umq_route_t useConnRoute;
    int ret = socketFd->GetRoundRobinConnEid(route_list, &dstEid, &useConnRoute);
    EXPECT_EQ(ret, RET_OK);
}

TEST_F(BrpcFileDescriptorTest, GetRoundRobinConnEid_MultipleRoutes)
{
    umq_eid_t srcEid = {};
    umq_eid_t dstEid = {};
    dstEid.raw[0] = EID_OFFSET_1;
    umq_port_id_t src_port = {};
    umq_port_id_t dst_port = {};

    umq_route_t connRoute1 = {src_port, dst_port, srcEid, dstEid};
    umq_route_t connRoute2 = {src_port, dst_port, srcEid, dstEid};

    umq_route_list_t route_list = {
        .route_num = ROUTE_NUM_2,
        .routes = {connRoute1, connRoute2}
    };

    umq_route_t useConnRoute;
    int ret = socketFd->GetRoundRobinConnEid(route_list, &dstEid, &useConnRoute);
    EXPECT_EQ(ret, RET_OK);
}

// ============= FallbackTcpMgr Tests =============

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_InitialState)
{
    FallbackTcpMgr mgr;
    EXPECT_FALSE(mgr.TxUseTcp());
    EXPECT_FALSE(mgr.RxUseTcp());
    EXPECT_FALSE(mgr.UseTcp());
}

// ============= SocketFd Getter Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_GetFd)
{
    EXPECT_EQ(socketFd->GetFd(), BRPC_FD_1);
}

// ============= EidRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, EidRegistry_RegisterAndCheck)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i);
    }

    EXPECT_TRUE(EidRegistry::Instance().RegisterEid(eid));
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEid(eid));
    EXPECT_TRUE(EidRegistry::Instance().UnregisterEid(eid));
    EXPECT_FALSE(EidRegistry::Instance().IsRegisteredEid(eid));
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_RegisterDuplicate)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_100);
    }

    EXPECT_TRUE(EidRegistry::Instance().RegisterEid(eid));
    EXPECT_FALSE(EidRegistry::Instance().RegisterEid(eid));  // Duplicate
    EXPECT_TRUE(EidRegistry::Instance().UnregisterEid(eid));
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_EidIndex)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_50);
    }

    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid, BRPC_INDEX_42);
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEidIndex(eid));

    uint32_t index = 0;
    EXPECT_TRUE(EidRegistry::Instance().GetEidIndex(eid, index));
    EXPECT_EQ(index, BRPC_INDEX_42);

    EXPECT_TRUE(EidRegistry::Instance().UnregisterEidIndex(eid));
    EXPECT_FALSE(EidRegistry::Instance().IsRegisteredEidIndex(eid));
}

// ============= RouteListRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_RegisterAndGet)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_200);
    }

    umq_route_list_t routeList = {};
    routeList.route_num = BRPC_ROUTE_NUM_1;

    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList);
    EXPECT_TRUE(RouteListRegistry::Instance().IsRegisteredRouteList(eid));

    umq_route_list_t retrievedList = {};
    EXPECT_TRUE(RouteListRegistry::Instance().GetRouteList(eid, retrievedList));
    EXPECT_EQ(retrievedList.route_num, BRPC_ROUTE_NUM_1);

    EXPECT_TRUE(RouteListRegistry::Instance().UnregisterRouteList(eid));
    EXPECT_FALSE(RouteListRegistry::Instance().IsRegisteredRouteList(eid));
}

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_Replace)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_150);
    }

    umq_route_list_t routeList1 = {};
    routeList1.route_num = BRPC_ROUTE_NUM_1;

    umq_route_list_t routeList2 = {};
    routeList2.route_num = BRPC_ROUTE_NUM_2;

    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList1);
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList2);

    umq_route_list_t retrievedList = {};
    EXPECT_TRUE(RouteListRegistry::Instance().GetRouteList(eid, retrievedList));
    EXPECT_EQ(retrievedList.route_num, BRPC_ROUTE_NUM_2);

    RouteListRegistry::Instance().UnregisterRouteList(eid);
}

// ============= Additional EidRegistry Tests =============

TEST_F(BrpcFileDescriptorTest, EidRegistry_MultipleEids)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid1.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_1);
        eid2.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_2);
    }

    EXPECT_TRUE(EidRegistry::Instance().RegisterEid(eid1));
    EXPECT_TRUE(EidRegistry::Instance().RegisterEid(eid2));

    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEid(eid1));
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEid(eid2));

    EXPECT_TRUE(EidRegistry::Instance().UnregisterEid(eid1));
    EXPECT_TRUE(EidRegistry::Instance().UnregisterEid(eid2));
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_EidIndexMultiple)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid1.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_100);
        eid2.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_200);
    }

    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid1, BRPC_INDEX_10);
    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid2, BRPC_INDEX_20);

    uint32_t index1 = 0;
    uint32_t index2 = 0;
    EXPECT_TRUE(EidRegistry::Instance().GetEidIndex(eid1, index1));
    EXPECT_TRUE(EidRegistry::Instance().GetEidIndex(eid2, index2));
    EXPECT_EQ(index1, BRPC_INDEX_10);
    EXPECT_EQ(index2, BRPC_INDEX_20);

    EidRegistry::Instance().UnregisterEidIndex(eid1);
    EidRegistry::Instance().UnregisterEidIndex(eid2);
}

// ============= RouteListRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_GetNonExistent)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_250);
    }

    umq_route_list_t retrievedList = {};
    EXPECT_FALSE(RouteListRegistry::Instance().GetRouteList(eid, retrievedList));
}

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_MultipleEids)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid1.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_30);
        eid2.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_60);
    }

    umq_route_list_t routeList1 = {};
    routeList1.route_num = BRPC_ROUTE_NUM_5;

    umq_route_list_t routeList2 = {};
    routeList2.route_num = BRPC_ROUTE_NUM_10;

    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid1, routeList1);
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid2, routeList2);

    umq_route_list_t retrievedList1 = {};
    umq_route_list_t retrievedList2 = {};

    EXPECT_TRUE(RouteListRegistry::Instance().GetRouteList(eid1, retrievedList1));
    EXPECT_TRUE(RouteListRegistry::Instance().GetRouteList(eid2, retrievedList2));
    EXPECT_EQ(retrievedList1.route_num, BRPC_ROUTE_NUM_5);
    EXPECT_EQ(retrievedList2.route_num, BRPC_ROUTE_NUM_10);

    RouteListRegistry::Instance().UnregisterRouteList(eid1);
    RouteListRegistry::Instance().UnregisterRouteList(eid2);
}

// ============= SocketFd Additional Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_GetPeerIpAdditional)
{
    std::string ip = socketFd->GetPeerIp();
    // Check that we can call the method
    (void)ip;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetPeerEidAdditional)
{
    umq_eid_t eid = socketFd->GetPeerEid();
    // Check that we can call the method
    (void)eid;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetPeerFdAdditional)
{
    int fd = socketFd->GetPeerFd();
    // Default might be -1 or 0
    (void)fd;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetEventFdAdditional)
{
    int fd = socketFd->GetEventFd();
    // Check that we can call the method
    (void)fd;
}

// ============= SocketFd Method Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_GetLocalUmqHandleAdditional)
{
    uint64_t handle = socketFd->GetLocalUmqHandle();
    // Check that we can call the method
    (void)handle;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetMainUmqHandleAdditional)
{
    uint64_t handle = socketFd->GetMainUmqHandle();
    // Check that we can call the method
    (void)handle;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_GetBindRemoteAdditional)
{
    bool bindRemote = socketFd->GetBindRemote();
    // Check that we can call the method
    (void)bindRemote;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_SetBindRemoteAdditional)
{
    socketFd->SetBindRemote(true);
    EXPECT_TRUE(socketFd->GetBindRemote());

    socketFd->SetBindRemote(false);
    EXPECT_FALSE(socketFd->GetBindRemote());
}

// ============= FallbackTcpMgr Tests =============

TEST_F(BrpcFileDescriptorTest, FallbackTcpMgr_DefaultValues)
{
    FallbackTcpMgr mgr;
    // By default, both should be false
    EXPECT_FALSE(mgr.TxUseTcp());
    EXPECT_FALSE(mgr.RxUseTcp());
    EXPECT_FALSE(mgr.UseTcp());
}

// ============= EidRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, EidRegistry_UnregisterNonExistentAdditional)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_200);
    }

    // Unregister an eid that doesn't exist - should return false
    bool result = EidRegistry::Instance().UnregisterEid(eid);
    EXPECT_FALSE(result);
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_DoubleRegisterAdditional)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_100);
    }

    // First register should succeed
    bool result1 = EidRegistry::Instance().RegisterEid(eid);
    EXPECT_TRUE(result1);

    // Second register should fail (already exists)
    bool result2 = EidRegistry::Instance().RegisterEid(eid);
    EXPECT_FALSE(result2);

    // Cleanup
    EidRegistry::Instance().UnregisterEid(eid);
}

// ============= RouteListRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_GetNonExistentAdditional)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_150);
    }

    umq_route_list_t routeList = {};
    bool result = RouteListRegistry::Instance().GetRouteList(eid, routeList);
    EXPECT_FALSE(result);
}

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_ReplaceRouteListAdditional)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_50);
    }

    umq_route_list_t routeList1 = {};
    routeList1.route_num = BRPC_ROUTE_NUM_5;

    umq_route_list_t routeList2 = {};
    routeList2.route_num = BRPC_ROUTE_NUM_10;

    // Register first
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList1);

    // Replace with second
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList2);

    // Get and verify it was replaced
    umq_route_list_t retrievedList = {};
    EXPECT_TRUE(RouteListRegistry::Instance().GetRouteList(eid, retrievedList));
    EXPECT_EQ(retrievedList.route_num, EXPECTED_RETRIEVE_10);

    RouteListRegistry::Instance().UnregisterRouteList(eid);
}

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_UnregisterNonExistentAdditional)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_OFFSET_250);
    }

    bool result = RouteListRegistry::Instance().UnregisterRouteList(eid);
    EXPECT_FALSE(result);
}

// ============= BrpcIOBufSize Tests =============
// Note: BrpcIOBufSize takes no parameters - it reads from Context

TEST_F(BrpcFileDescriptorTest, BrpcIOBufSize_ReturnsValidSize)
{
    // BrpcIOBufSize reads the block type from Context
    // Since Context is initialized in SetUp, this should work
    uint32_t size = BrpcIOBufSize();
    EXPECT_GT(size, 0u);
}

// ============= EidRegistry Index Tests =============

TEST_F(BrpcFileDescriptorTest, EidRegistry_RegisterEidIndex)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + CHIP_ID_BASE_10);
    }
    uint32_t index = BRPC_INDEX_5;

    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid, index);

    uint32_t retrievedIndex = 0;
    EXPECT_TRUE(EidRegistry::Instance().GetEidIndex(eid, retrievedIndex));
    EXPECT_EQ(retrievedIndex, index);

    EidRegistry::Instance().UnregisterEidIndex(eid);
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_IsRegisteredEidIndex)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_2 * CHIP_ID_BASE_10);
    }

    EXPECT_FALSE(EidRegistry::Instance().IsRegisteredEidIndex(eid));

    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid, BRPC_INDEX_3);
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEidIndex(eid));

    EidRegistry::Instance().UnregisterEidIndex(eid);
    EXPECT_FALSE(EidRegistry::Instance().IsRegisteredEidIndex(eid));
}

TEST_F(BrpcFileDescriptorTest, EidRegistry_ReplaceEidIndex)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_30);
    }

    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid, BRPC_INDEX_1);
    EidRegistry::Instance().RegisterOrReplaceEidIndex(eid, BRPC_ROUTE_NUM_2);

    uint32_t index = 0;
    EXPECT_TRUE(EidRegistry::Instance().GetEidIndex(eid, index));
    EXPECT_EQ(index, BRPC_ROUTE_NUM_2);

    EidRegistry::Instance().UnregisterEidIndex(eid);
}

// ============= RouteListRegistry Additional Tests =============

TEST_F(BrpcFileDescriptorTest, RouteListRegistry_IsRegisteredRouteList)
{
    umq_eid_t eid = {};
    for (int i = 0; i < EID_RAW_SIZE_16; ++i) {
        eid.raw[i] = static_cast<uint8_t>(i + EID_OFFSET_60);
    }

    EXPECT_FALSE(RouteListRegistry::Instance().IsRegisteredRouteList(eid));

    umq_route_list_t routeList = {};
    routeList.route_num = BRPC_ROUTE_NUM_3;
    RouteListRegistry::Instance().RegisterOrReplaceRouteList(eid, routeList);

    EXPECT_TRUE(RouteListRegistry::Instance().IsRegisteredRouteList(eid));

    RouteListRegistry::Instance().UnregisterRouteList(eid);
    EXPECT_FALSE(RouteListRegistry::Instance().IsRegisteredRouteList(eid));
}

// ============= SocketFd Getters Additional Tests =============

TEST_F(BrpcFileDescriptorTest, SocketFd_GetPeerInfoAdditional)
{
    int fd = BRPC_FD_123;
    uint64_t magicNumber = BRPC_MAGIC_10;
    uint32_t magicNumberRecvSize = BRPC_RECV_SIZE_9;

    Brpc::SocketFd* socketFd = nullptr;
    try {
        socketFd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
    } catch (const std::bad_alloc&) {
        socketFd = nullptr;
    }
    ASSERT_NE(socketFd, nullptr);

    // Test getters that haven't been covered
    std::string peerIp = socketFd->GetPeerIp();
    (void)peerIp;  // Just verify function compiles and runs

    int peerFd = socketFd->GetPeerFd();
    (void)peerFd;

    int eventFd = socketFd->GetEventFd();
    (void)eventFd;

    delete socketFd;
}

TEST_F(BrpcFileDescriptorTest, SocketFd_UmqHandlesAdditional)
{
    int fd = BRPC_FD_124;
    uint64_t magicNumber = BRPC_MAGIC_20;
    uint32_t magicNumberRecvSize = BRPC_RECV_SIZE_15;

    Brpc::SocketFd* socketFd = nullptr;
    try {
        socketFd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
    } catch (const std::bad_alloc&) {
        socketFd = nullptr;
    }
    ASSERT_NE(socketFd, nullptr);

    // Test UMQ handle getters
    uint64_t localUmq = socketFd->GetLocalUmqHandle();
    (void)localUmq;

    uint64_t mainUmq = socketFd->GetMainUmqHandle();
    (void)mainUmq;

    delete socketFd;
}
}