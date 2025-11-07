/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <unistd.h>
#include <sys/epoll.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "rdma_worker.h"
#include "transport/net_delay_release_timer.h"
#include "transport/net_heartbeat.h"
#include "transport/net_load_balance.h"
#include "transport/rdma/rdma_common.h"
#include "transport/rdma/verbs/net_rdma_async_endpoint.h"
#include "transport/ub/ub_urma_wrapper_jetty.h"
#include "transport/ub/ub_worker.h"
#include "transport/ub/net_ub_endpoint.h"
#include "transport/ub/net_ub_driver_oob.h"
#include "net_sock_driver_oob.h"

namespace ock {
namespace hcom {

class TestTransportCommon : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestTransportCommon::SetUp()
{
}

void TestTransportCommon::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestTransportCommon, NetDelayReleaseTimerStarted)
{
    std::string name = "timer_name";
    NetDelayReleaseTimer *timer = new (std::nothrow) NetDelayReleaseTimer(name, 0);

    ASSERT_NE(timer, nullptr);
    timer->mStarted = true;
    EXPECT_EQ(timer->Start(), static_cast<int>(NN_OK));
    EXPECT_NO_FATAL_FAILURE(timer->Stop());

    if (timer != nullptr) {
        delete timer;
        timer = nullptr;
    }
}

TEST_F(TestTransportCommon, NetDelayReleaseTimerStartedThread)
{
    MOCKER(epoll_create).defaults().will(returnValue(-1));

    std::string name = "timer_name";
    NetDelayReleaseTimer *timer = new (std::nothrow) NetDelayReleaseTimer(name, 0);

    ASSERT_NE(timer, nullptr);
    EXPECT_NO_FATAL_FAILURE(timer->RunDelayReleaseThread());

    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerIndex);
    auto epRes = NetDelayReleaseResource(ep, NN_NO1);
    epRes.mTimeout = 0;
    timer->mDelayReleaseQueue.push(epRes);
    EXPECT_NO_FATAL_FAILURE(timer->DequeueDelayRelease());

    auto epRes1 = NetDelayReleaseResource(ep, NN_NO1);
    epRes1.mTimeout = 0;
    epRes1.mEp.Set(nullptr);
    timer->mDelayReleaseQueue.push(epRes1);
    EXPECT_NO_FATAL_FAILURE(timer->DequeueDelayRelease());

    EXPECT_NO_FATAL_FAILURE(timer->EnqueueDelayRelease(ep));
    if (timer != nullptr) {
        delete timer;
        timer = nullptr;
    }
}

void FakeRunInHbThread(NetHeartbeat *This)
{
    return;
}

TEST_F(TestTransportCommon, NetHeartbeatStart)
{
    NetHeartbeat *heartbeat = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    EXPECT_EQ(heartbeat->Start(), static_cast<int>(NN_INVALID_PARAM));

    std::thread tmpThread(&FakeRunInHbThread, heartbeat);
    heartbeat->mHbThread = std::move(tmpThread);
    EXPECT_NO_FATAL_FAILURE(heartbeat->Stop());

    NetDriverRDMAWithOob *driver = new (std::nothrow) NetDriverRDMAWithOob("name", false,
        UBSHcomNetDriverProtocol::UBC);
    heartbeat->mDriver = driver;
    EXPECT_NO_FATAL_FAILURE(heartbeat->DetectHbState());

    UBSHcomNetWorkerIndex workerIndex{};
    NetAsyncEndpoint *ep = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerIndex);

    MOCKER_CPP_VIRTUAL(*ep, &NetAsyncEndpoint::PostSend,
        NResult(NetAsyncEndpoint::*)(uint16_t, const UBSHcomNetTransRequest &, uint32_t)).stubs()
        .will(returnValue(static_cast<int>(NN_OK)))
        .then(returnValue(static_cast<int>(NN_INVALID_PARAM)));
    EXPECT_EQ(heartbeat->SendTwoSideHeartBeat(ep), static_cast<int>(NN_OK));
    EXPECT_EQ(heartbeat->SendTwoSideHeartBeat(ep), static_cast<int>(NN_INVALID_PARAM));
    if (heartbeat != nullptr) {
        delete heartbeat;
        heartbeat = nullptr;
    }
    if (ep != nullptr) {
        delete ep;
        ep = nullptr;
    }
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
}

TEST_F(TestTransportCommon, NetHeartbeatDetectSingleEpHbState)
{
    NetHeartbeat *heartbeat = new (std::nothrow) NetHeartbeat(nullptr, 0, 0);
    UBSHcomNetTransRequest req {};
    EXPECT_NO_FATAL_FAILURE(heartbeat->DetectSingleEpHbState(req, nullptr));

    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(0, nullptr, nullptr, workerIndex);
    NetDriverSockWithOOB *driver =
        new (std::nothrow) NetDriverSockWithOOB("name", false, UBSHcomNetDriverProtocol::TCP, SOCK_TCP);
    heartbeat->mDriver = driver;
    EXPECT_NO_FATAL_FAILURE(heartbeat->DetectSingleEpHbState(req, nullptr));
    if (heartbeat != nullptr) {
        delete heartbeat;
        heartbeat = nullptr;
    }
}

TEST_F(TestTransportCommon, NetWorkerLBFunction)
{
    NetWorkerLB *lb = new (std::nothrow) NetWorkerLB("name", UBSHcomNetDriverLBPolicy::NET_ROUND_ROBIN, 64);
    std::vector<std::pair<uint16_t, uint16_t>> groups;
    groups.push_back(std::make_pair(0, 0));
    EXPECT_EQ(lb->AddWorkerGroups(groups), static_cast<int>(NN_INVALID_PARAM));

    uint16_t wkrIdx = 0;
    EXPECT_EQ(lb->ChooseWorker(2, "127.0.0.1", wkrIdx), false);
    NetWorkerGroupLbInfo info {};
    info.wrkCntLimited = 1;
    info.wrkCntInGrp = 0;
    lb->mWrkGroups.push_back(info);
    lb->mWrkGroups[0].wrkLimited.push_back(1);
    EXPECT_EQ(lb->ChooseWorker(0, "127.0.0.1", wkrIdx), true);

    lb->mWorkerLimitedCnt = 0;
    lb->AddWorkerGroup(0, 1);
    if (lb != nullptr) {
        delete lb;
        lb = nullptr;
    }
}

TEST_F(TestTransportCommon, NetWorkerLBChooseWorkerLimited)
{
    NetWorkerLB *lb = new (std::nothrow) NetWorkerLB("name", UBSHcomNetDriverLBPolicy::NET_HASH_IP_PORT, 64);
    uint16_t wkrIdx = 0;
    EXPECT_EQ(lb->ChooseWorkerLimited(2, "127.0.0.1", wkrIdx), false);

    NetWorkerGroupLbInfo info {};
    info.wrkCntLimited = 1;
    info.wrkCntInGrp = 0;
    lb->mWrkGroups.push_back(info);
    lb->mWrkGroups[0].wrkLimited.push_back(1);
    EXPECT_EQ(lb->ChooseWorkerLimited(0, "127.0.0.1", wkrIdx), true);
    if (lb != nullptr) {
        delete lb;
        lb = nullptr;
    }
}

TEST_F(TestTransportCommon, NormalMemoryRegionInit)
{
    NormalMemoryRegion *region = new (std::nothrow) NormalMemoryRegion("name", false, 0, 0);
    region->mInited = true;
    EXPECT_EQ(region->Initialize(), static_cast<int>(NN_OK));

    region->mInited = false;
    region->mExternalMemory = true;
    region->mBuf = 0;
    region->mSize = 0;
    EXPECT_EQ(region->Initialize(), static_cast<int>(NN_INVALID_PARAM));

    region->mExternalMemory = false;
    MOCKER(memalign).defaults().will(returnValue(static_cast<void *>(nullptr)));
    EXPECT_EQ(region->Initialize(), static_cast<int>(NN_MALLOC_FAILED));
    if (region != nullptr) {
        delete region;
        region = nullptr;
    }
}

TEST_F(TestTransportCommon, NormalMemoryRegionFixedBufferInit)
{
    NormalMemoryRegionFixedBuffer *buffer = new (std::nothrow) NormalMemoryRegionFixedBuffer("name", 0, 0);
    EXPECT_EQ(buffer->Initialize(), static_cast<int>(NN_INVALID_PARAM));
    MOCKER_CPP(NetRingBuffer<uintptr_t>::Initialize).stubs()
        .will(returnValue(static_cast<int>(NN_INVALID_PARAM)));
    EXPECT_EQ(buffer->Initialize(), static_cast<int>(NN_INVALID_PARAM));

    if (buffer != nullptr) {
        delete buffer;
        buffer = nullptr;
    }
}

TEST_F(TestTransportCommon, NormalMemoryRegionGetMemorySeg)
{
    NormalMemoryRegion region("name", false, 0, 0);

    EXPECT_EQ(region.GetMemorySeg(), static_cast<void *>(nullptr));
    uint64_t va = 0;
    uint64_t vaLen = 0;
    uint32_t token = 0;
    EXPECT_NO_FATAL_FAILURE(region.GetVa(va, vaLen, token));
}

TEST_F(TestTransportCommon, NormalMemoryRegionFixedBufferGetFreeBufferN)
{
    NormalMemoryRegionFixedBuffer buffer("name", 0, 0);
    EXPECT_EQ(buffer.GetFreeBufferN(nullptr, 0), false);
}
}
}
//#endif