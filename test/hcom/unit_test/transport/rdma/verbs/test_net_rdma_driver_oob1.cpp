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
#include <fcntl.h>
#include <sys/poll.h>

#include "net_monotonic.h"
#include "net_oob_ssl.h"
#include "net_rdma_sync_endpoint.h"
#include "net_rdma_async_endpoint.h"
#include "rdma_mr_dm_buf.h"
#include "rdma_mr_fixed_buf.h"
#include "net_rdma_driver_oob.h"
#include "net_oob_secure.h"

namespace ock {
namespace hcom {

class TestNetRdmaDriverOob1 : public testing::Test {
public:
    TestNetRdmaDriverOob1();
    virtual void SetUp(void);
    virtual void TearDown(void);
    const std::string name = "TestNetRdmaDriverOob1";
    NetDriverRDMAWithOob *testDriver1 = nullptr;
};

TestNetRdmaDriverOob1::TestNetRdmaDriverOob1() {}

void TestNetRdmaDriverOob1::SetUp()
{
    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = RDMA;
    testDriver1 = new (std::nothrow) NetDriverRDMAWithOob(name, startOobSvr, protocol);
    ASSERT_NE(testDriver1, nullptr);
}

void TestNetRdmaDriverOob1::TearDown()
{
    if (testDriver1 != nullptr) {
        delete testDriver1;
        testDriver1 = nullptr;
    }

    GlobalMockObject::verify();
}

TEST_F(TestNetRdmaDriverOob1, TestDoUnInitialize)
{
    testDriver1->mStarted = true;
    EXPECT_NO_FATAL_FAILURE(testDriver1->DoUnInitialize());
}

TEST_F(TestNetRdmaDriverOob1, TestNewConnectionCBFailed)
{
    OOBTCPConnection *conn = new (std::nothrow) OOBTCPConnection(-1);
    MOCKER_CPP(&OOBSecureProcess::SecProcessInOOBServer).stubs()
        .will(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP(&OOBSecureProcess::SecProcessCompareEpNum,
        NResult(uint32_t, uint32_t, const std::string &, const std::vector<NetOOBServer *> &)).stubs()
        .will(returnValue(static_cast<int>(NN_OOB_SEC_PROCESS_ERROR)))
        .then(returnValue(static_cast<int>(NN_OK)));
    EXPECT_EQ(testDriver1->NewConnectionCB(*conn), static_cast<int>(NN_OOB_SEC_PROCESS_ERROR));

    MOCKER_CPP_VIRTUAL(*conn, &OOBTCPConnection::Receive)
        .stubs()
        .will(returnValue(static_cast<int>(NN_OK)));
    MOCKER_CPP(&OOBSecureProcess::SecCheckConnectionHeader).stubs()
        .will(returnValue(static_cast<int>(NN_OOB_SEC_PROCESS_ERROR)))
        .then(returnValue(static_cast<int>(NN_OK)));
    EXPECT_EQ(testDriver1->NewConnectionCB(*conn), static_cast<int>(NN_ERROR));

    testDriver1->mOptions.enableMultiRail = true;
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs()
        .will(returnValue(static_cast<int>(NN_ERROR)));
    EXPECT_EQ(testDriver1->NewConnectionCB(*conn), static_cast<int>(NN_ERROR));
}

TEST_F(TestNetRdmaDriverOob1, TestConnectFailed)
{
    std::string payload(1025, 'a');
    UBSHcomNetEndpointPtr ep = nullptr;
    testDriver1->mInited = true;
    testDriver1->mStarted = true;
    auto ret = testDriver1->Connect(std::string("127.0.0.1"), 0, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
}

TEST_F(TestNetRdmaDriverOob1, TestProcessError)
{
    EXPECT_NO_FATAL_FAILURE(testDriver1->ProcessErrorNewRequest(nullptr));
    EXPECT_NO_FATAL_FAILURE(testDriver1->ProcessErrorOneSideDone(nullptr));
    EXPECT_NO_FATAL_FAILURE(testDriver1->ProcessQPError(nullptr));
    EXPECT_NO_FATAL_FAILURE(testDriver1->SendFinished(nullptr));
    EXPECT_NO_FATAL_FAILURE(testDriver1->OneSideDone(nullptr));
}
}
}