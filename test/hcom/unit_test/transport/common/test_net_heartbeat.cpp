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

#include "hcom.h"
#include "net_heartbeat.h"
#include "net_rdma_driver_oob.h"
#include "net_rdma_async_endpoint.h"
#include "net_ub_endpoint.h"
#include "rdma_composed_endpoint.h"

namespace ock {
namespace hcom {
class TestNetHeartbeat : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    UBSHcomNetDriver *mDriver = nullptr;
    NetHeartbeat *mHb = nullptr;

    UBSHcomNetDriver *mUbcDriver = nullptr;
    NetHeartbeat *mUbcHb = nullptr;
};

void TestNetHeartbeat::SetUp()
{
    mDriver = new (std::nothrow) NetDriverRDMAWithOob("test_driver", 1, RDMA);
    mUbcDriver = new (std::nothrow) NetDriverRDMAWithOob("test_driver", 1, UBC);
    ASSERT_NE(mDriver, nullptr);
    ASSERT_NE(mUbcDriver, nullptr);

    mHb = new NetHeartbeat(mDriver, NN_NO60, NN_NO2);
    mDriver->IncreaseRef();

    mUbcHb = new NetHeartbeat(mUbcDriver, NN_NO60, NN_NO2);
    mUbcDriver->IncreaseRef();
}

void TestNetHeartbeat::TearDown()
{
    GlobalMockObject::verify();
    if (mDriver != nullptr) {
        delete mDriver;
        mDriver = nullptr;
    }

    if (mHb != nullptr) {
        delete mHb;
        mHb = nullptr;
    }

    if (mUbcDriver != nullptr) {
        delete mUbcDriver;
        mUbcDriver = nullptr;
    }

    if (mUbcHb != nullptr) {
        delete mUbcHb;
        mUbcHb = nullptr;
    }
}

TEST_F(TestNetHeartbeat, NewHeartbeatZero)
{
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(mDriver, NN_NO60, 0);
    uint32_t interval = 5000;
    EXPECT_EQ(hb->mHeartBeatProbeInterval, interval);
    delete (hb);
    hb = nullptr;
}

TEST_F(TestNetHeartbeat, NewHeartbeatMax)
{
    uint32_t maxInterval = 1023;
    NetHeartbeat *hb = new (std::nothrow) NetHeartbeat(mDriver, NN_NO60, maxInterval);
    EXPECT_EQ(hb->mHeartBeatProbeInterval, maxInterval * NN_NO1000 * NN_NO1000);
    delete (hb);
    hb = nullptr;
}

TEST_F(TestNetHeartbeat, StartWithMrError)
{
    MOCKER_CPP_VIRTUAL(mDriver, &UBSHcomNetDriver::CreateMemoryRegion)
            .stubs()
            .will(returnValue(1))
            .then(returnValue(0))
            .then(returnValue(2));
    MOCKER_CPP(&NetHeartbeat::RunInHbThread).stubs().then(ignoreReturnValue());

    mHb->mHBStarted = true;
    NResult result = mHb->Start();
    EXPECT_EQ(result, 1);
    result = mHb->Start();
    EXPECT_EQ(result, 2);
}

TEST_F(TestNetHeartbeat, DetectSingleEpHbStateWithInvalidParam)
{
    UBSHcomNetEndpoint *ep = nullptr;
    UBSHcomNetTransRequest req {};
    EXPECT_NO_FATAL_FAILURE(mHb->DetectSingleEpHbState(dynamic_cast<NetAsyncEndpoint *>(ep),
        dynamic_cast<NetDriverRDMAWithOob *>(mDriver), req, RDMAOpContextInfo::HB_WRITE));
}

TEST_F(TestNetHeartbeat, DetectSingleEpHbStateWithBrokenEp)
{
        RDMAAsyncEndPoint *rdmaEp = nullptr;
        UBSHcomNetWorkerIndex index {};
        UBSHcomNetEndpoint *ep = new NetAsyncEndpoint(0, rdmaEp, (NetDriverRDMAWithOob*)mDriver, index);
        UBSHcomNetTransRequest req {};

        MOCKER_CPP(&NetAsyncEndpoint::checkTargetHbTime).stubs().will(returnValue(true));
        MOCKER_CPP(&NetAsyncEndpoint::HbCheckStateNormal).stubs().will(returnValue(false));
        MOCKER_CPP(&NetAsyncEndpoint::HbBrokenEp).stubs().will(returnValue(true));
        MOCKER_CPP(&NetDriverRDMAWithOob::ProcessEpError).stubs().will(ignoreReturnValue());

        EXPECT_NO_FATAL_FAILURE(mHb->DetectSingleEpHbState(dynamic_cast<NetAsyncEndpoint *>(ep),
            dynamic_cast<NetDriverRDMAWithOob *>(mDriver), req, RDMAOpContextInfo::HB_WRITE));
}

TEST_F(TestNetHeartbeat, DetectSingleEpHbStateWithOUTBrokenEp)
{
        RDMAAsyncEndPoint *rdmaEp = nullptr;
        UBSHcomNetWorkerIndex index {};
        UBSHcomNetEndpoint *ep = new NetAsyncEndpoint(0, rdmaEp, (NetDriverRDMAWithOob*)mDriver, index);
        UBSHcomNetTransRequest req {};

        MOCKER_CPP(&NetAsyncEndpoint::checkTargetHbTime).stubs().will(returnValue(true));
        MOCKER_CPP(&NetAsyncEndpoint::HbCheckStateNormal).stubs().will(returnValue(false));
        MOCKER_CPP(&NetAsyncEndpoint::HbBrokenEp).stubs().will(returnValue(false));

        EXPECT_NO_FATAL_FAILURE(mHb->DetectSingleEpHbState(dynamic_cast<NetAsyncEndpoint *>(ep),
            dynamic_cast<NetDriverRDMAWithOob *>(mDriver), req, RDMAOpContextInfo::HB_WRITE));
        EXPECT_EQ(ep->State().Compare(NEP_BROKEN), true);
}

TEST_F(TestNetHeartbeat, DetectSingleEpHbStateUBCInvalidParam)
{
    NetEndpoint *ep = nullptr;
    UBSHcomNetTransRequest req {};
    EXPECT_NO_FATAL_FAILURE(mUbcHb->DetectSingleEpHbState(dynamic_cast<NetUBAsyncEndpoint *>(ep),
                                                          dynamic_cast<NetDriverUBWithOob *>(mUbcDriver), req,
                                                          UBOpContextInfo::HB_WRITE));
}

TEST_F(TestNetHeartbeat, DetectSingleEpHbStateUBCWithBrokenEp)
{
    UBJetty *jetty = nullptr;
    NetEndpoint *ep = new NetUBAsyncEndpoint(0, jetty, (NetDriverUBWithOob *)mUbcDriver, nullptr);

    UBSHcomNetTransRequest req {};
    MOCKER_CPP(&NetUBAsyncEndpoint::checkTargetHbTime).stubs().will(returnValue(true));
    MOCKER_CPP(&NetUBAsyncEndpoint::HbCheckStateNormal).stubs().will(returnValue(true));
    MOCKER_CPP(&NetDriverUBWithOob::ProcessEpError).stubs().will(ignoreReturnValue());

    EXPECT_NO_FATAL_FAILURE(mUbcHb->DetectSingleEpHbState(dynamic_cast<NetUBAsyncEndpoint *>(ep),
                                                          dynamic_cast<NetDriverUBWithOob *>(mUbcDriver), req,
                                                          UBOpContextInfo::HB_WRITE));
}
}
}