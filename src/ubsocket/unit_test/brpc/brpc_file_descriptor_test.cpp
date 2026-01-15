#include "brpc_file_descriptor.h"
#include "rpc_adpt_vlog.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

namespace Brpc {

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
    setenv("RPC_ADPT_UB_FORCE", "1", 1);
    setenv("RPC_ADPT_TRANS_MODE", "UB", 1);

    // 加载日志
    RpcAdptSetLogCtx(UTIL_VLOG_LEVEL_INFO);

    int fd = 1;
    uint64_t magicNumber = 10;
    uint32_t magicNumberRecvSize = 9;
    socketFd = new Brpc::SocketFd(fd, magicNumber, magicNumberRecvSize);
}

void BrpcFileDescriptorTest::TearDown()
{
    unsetenv("RPC_ADPT_UB_FORCE");
    unsetenv("RPC_ADPT_TRANS_MODE");
    GlobalMockObject::verify();
}

TEST_F(BrpcFileDescriptorTest, DoRouteTest)
{
    umq_eid_t srcEid = {0};
    umq_eid_t dstEid = {1};
    umq_route_t connRoute;
    bool useRoundRobin = false;
    
    MOCKER_CPP(&Brpc::SocketFd::GetDevRouteList)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    int ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, -1);

    MOCKER_CPP(&Brpc::SocketFd::GetConnEid)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, -1);

    MOCKER_CPP(&Brpc::SocketFd::CheckDevAdd)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));
    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, -1);

    ret = socketFd->DoRoute(&srcEid, &dstEid, &connRoute, useRoundRobin);
    EXPECT_EQ(ret, 0);
}

TEST_F(BrpcFileDescriptorTest, GetRoundRobinConnEidTest)
{
    umq_route_flag_t flag = {0};
    flag.bs.rtp = 1;
    umq_eid_t srcEid = {0};
    umq_eid_t dstEid = {1};
    umq_route_t connRoute = {
        .src = srcEid,
        .dst = dstEid,
        .flag = flag,
        .hops = 1,
        .chip_id = 1
    };

    umq_route_list_t route_list = {
        .len = 2,
        .buf = {
            connRoute, connRoute
        }
    };

    umq_route_t useConnRoute;
    int ret = socketFd->GetRoundRobinConnEid(route_list, &dstEid, &useConnRoute);
    EXPECT_EQ(ret, 0);

    umq_route_list_t route_list2 = {0};
    ret = socketFd->GetRoundRobinConnEid(route_list2, &dstEid, &useConnRoute);
    EXPECT_EQ(ret, -1);
}

TEST_F(BrpcFileDescriptorTest, GetCpuAffinityConnEidTest)
{
    umq_route_flag_t flag = {0};
    flag.bs.rtp = 1;
    umq_eid_t srcEid = {0};
    umq_eid_t dstEid = {1};
    umq_route_t connRoute1 = {
        .src = srcEid,
        .dst = dstEid,
        .flag = flag,
        .hops = 1,
        .chip_id = 0
    };
    umq_route_t connRoute2 = {
        .src = srcEid,
        .dst = dstEid,
        .flag = flag,
        .hops = 1,
        .chip_id = 1
    };

    umq_route_list_t route_list = {
        .len = 2,
        .buf = {
            connRoute1, connRoute2
        }
    };

    umq_route_t useConnRoute;
    std::vector<uint32_t> socketIds = {0, 1};
    int processSocketId = 1;

    MOCKER_CPP(&Brpc::SocketFd::GetRoundRobinConnEid)
    .stubs()
    .will(returnValue(static_cast<int>(-1)));

    int ret = socketFd->GetCpuAffinityConnEid(route_list, &dstEid, &useConnRoute, socketIds, processSocketId);
    EXPECT_EQ(ret, -1);

    socketFd->mPeerSocketId = 1;
    uint32_t chipId1 = 1;
    uint32_t chipId2 = 2;
    MOCKER_CPP(&Brpc::SocketFd::GetTargetChipId)
    .stubs()
    .will(returnValue(UINT32_MAX))
    .then(returnValue(chipId1))
    .then(returnValue(chipId2));
    ret = socketFd->GetCpuAffinityConnEid(route_list, &dstEid, &useConnRoute, socketIds, processSocketId);
    EXPECT_EQ(ret, -1);

    ret = socketFd->GetCpuAffinityConnEid(route_list, &dstEid, &useConnRoute, socketIds, processSocketId);
    EXPECT_EQ(ret, 0);

    ret = socketFd->GetCpuAffinityConnEid(route_list, &dstEid, &useConnRoute, socketIds, processSocketId);
    EXPECT_EQ(ret, -1);
}

TEST_F(BrpcFileDescriptorTest, GetTargetChipIdTest)
{
    std::vector<uint32_t> socketIds = {0, 1};
    std::vector<uint32_t> chipIdList = {0, 1};
    int processSocketId = 6;

    uint32_t ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, UINT32_MAX);

    processSocketId = 1;
    ret = socketFd->GetTargetChipId(socketIds, chipIdList, processSocketId);
    EXPECT_EQ(ret, 1);

    std::vector<uint32_t> chipIdList2 = {0};
    ret = socketFd->GetTargetChipId(socketIds, chipIdList2, processSocketId);
    EXPECT_EQ(ret, UINT32_MAX);
}

bool MockGetRouteList(const umq_eid_t &eid, umq_route_list_t &routeList)
{
    umq_route_flag_t flag = {0};
    flag.bs.rtp = 1;
    umq_eid_t srcEid = {0};
    umq_eid_t dstEid = {1};
    umq_route_t connRoute1 = {
        .src = srcEid,
        .dst = dstEid,
        .flag = flag,
        .hops = 1,
        .chip_id = 0
    };

    umq_route_list_t route_list = {
        .len = 1,
        .buf = {
            connRoute1
        }
    };

    routeList = route_list;
    return true;
}

TEST_F(BrpcFileDescriptorTest, CheckOtherRouteTest)
{
    umq_route_flag_t flag = {0};
    flag.bs.rtp = 1;
    umq_eid_t srcEid = {0};
    umq_eid_t dstEid = {1};
    umq_route_t connRoute = {
        .src = srcEid,
        .dst = dstEid,
        .flag = flag,
        .hops = 1,
        .chip_id = 1
    };
    umq_route_t otherConnRoute;

    MOCKER_CPP(&Brpc::RouteListRegistry::IsRegisteredRouteList)
    .stubs()
    .will(returnValue(false))
    .then(returnValue(true));
    int ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, -1);

    MOCKER_CPP(&Brpc::RouteListRegistry::GetRouteList)
    .stubs()
    .will(returnValue(false))
    .then(invoke(MockGetRouteList));

    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, -1);

    MOCKER_CPP(&Brpc::SocketFd::CheckDevAdd)
    .stubs()
    .will(returnValue(static_cast<int>(-1)))
    .then(returnValue(static_cast<int>(0)));

    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, -1);

    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, 0);

    connRoute.chip_id = 0;
    ret = socketFd->CheckOtherRoute(otherConnRoute, dstEid, connRoute);
    EXPECT_EQ(ret, -1);
}
}