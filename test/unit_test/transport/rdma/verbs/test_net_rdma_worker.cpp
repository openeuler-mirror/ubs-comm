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
//#ifdef RDMA_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "hcom.h"
#include "net_common.h"
#include "net_rdma_driver_oob.h"
#include "net_security_rand.h"
#include "rdma_validation.h"
#include "rdma_composed_endpoint.h"
#include "net_rdma_sync_endpoint.h"

namespace ock {
namespace hcom {

class TestNetRdmaWorker : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name;
    RDMAContext *ctx = nullptr;
    RDMAWorker *mWorker = nullptr;
    RDMACq *cq = nullptr;
    RDMAQp *qp = nullptr;
    UBSHcomNetWorkerIndex mWorkerIndex{};
    UBSHcomNetTransRequest request;
    RDMASglContextInfo *sglCtx;
    RDMAOpContextInfo *rdmaCtx;
    UBSHcomNetTransSglRequest sglRequest;
    UBSHcomNetTransSgeIov *iov = nullptr;
};

void TestNetRdmaWorker::SetUp()
{
    RDMAGId gid = {};
    ctx = new (std::nothrow) RDMAContext(name, true, gid);
    ASSERT_NE(ctx, nullptr);

    RDMAWorkerOptions options{};
    NetMemPoolFixedPtr memPool;
    NetMemPoolFixedPtr sglMemPool;
    mWorker = new (std::nothrow) RDMAWorker(name, ctx, options, memPool, sglMemPool);
    ASSERT_NE(mWorker, nullptr);

    cq = new (std::nothrow) RDMACq(name, ctx, false, 0);
    ASSERT_NE(cq, nullptr);

    uint32_t mid = 0;
    QpOptions qpOptions = {};
    qp = new (std::nothrow) RDMAQp(name, mid, ctx, cq, qpOptions);
    ASSERT_NE(qp, nullptr);

    request.lAddress = reinterpret_cast<uintptr_t>(&mWorkerIndex);
    request.size = 1;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    sglRequest = UBSHcomNetTransSglRequest(iov, 1, 0);
    sglCtx = new (std::nothrow) RDMASglContextInfo();
    rdmaCtx = new (std::nothrow) RDMAOpContextInfo();
}

void TestNetRdmaWorker::TearDown()
{
    if (mWorker != nullptr) {
        delete mWorker;
        mWorker = nullptr;
    }
    if (sglCtx != nullptr) {
        delete sglCtx;
        sglCtx = nullptr;
    }
    if (rdmaCtx != nullptr) {
        delete rdmaCtx;
        rdmaCtx = nullptr;
    }
    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSgl)
{
    name = "NetSyncEndpointRdmaPostSendSgl";
    RDMAQp *tmpQp = nullptr;
    int ret = mWorker->PostSendSgl(tmpQp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
    RDMASglContextInfo *tmpSglCtx = nullptr;
    MOCKER_CPP(&RDMASglContextInfoPool::Get).stubs().will(returnValue(tmpSglCtx));
    ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglTwo)
{
    name = "NetSyncEndpointRdmaPostSendSgltwo";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    MOCKER_CPP(&RDMASglContextInfoPool::Get).stubs().will(returnValue(sglCtx));
    int ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));

    sglRequest.upCtxSize = 1;
    ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_PARAM_INVALID));
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglThree)
{
    name = "NetSyncEndpointRdmaPostSendSglThree";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    MOCKER_CPP(&RDMASglContextInfoPool::Get).stubs().will(returnValue(sglCtx));

    RDMAOpContextInfo *tmpCtx = nullptr;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(tmpCtx)).then(returnValue(rdmaCtx));

    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(false));

    sglRequest.upCtxSize = 0;
    int ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_QP_CTX_FULL));
    MOCKER_CPP(&RDMAOpContextInfoPool::Return).stubs().will(returnValue(0));
    ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(RR_QP_POST_SEND_WR_FULL));
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglFour)
{
    name = "NetSyncEndpointRdmaPostSendSglFour";
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    RDMASglContextInfoPool mSglCtxInfoPool;
    MOCKER_CPP(&RDMASglContextInfoPool::Get).stubs().will(returnValue(sglCtx));

    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(rdmaCtx));

    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(true));

    MOCKER_CPP(&RDMAQp::PostSend).stubs().will(returnValue(1));

    MOCKER_CPP(&RDMAQp::PostSendSgl).stubs().will(returnValue(0));

    MOCKER_CPP(&RDMAOpContextInfoPool::Return).stubs().will(returnValue(0));

    MOCKER_CPP(&RDMASglContextInfoPool ::Return).stubs().will(returnValue(0));
    sglRequest.upCtxSize = 0;
    int ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 0);
    EXPECT_EQ(ret, 0);

    ret = mWorker->PostSendSgl(qp, sglRequest, request, 0, 1);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineOne)
{
    name = "RdmaWorkerPostSendSglInlineOne";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    uint32_t immData = 0;
    RResult ret = mWorker->PostSendSglInline(nullptr, header, req, immData);
    EXPECT_EQ(ret, 200);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineTwo)
{
    name = "RdmaWorkerPostSendSglInlineTwo";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    uint32_t immData = 0;
    RDMAOpContextInfo *tmpRdmaCtx = nullptr;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(tmpRdmaCtx));
    RResult ret = mWorker->PostSendSglInline(qp, header, req, immData);
    EXPECT_EQ(ret, 232);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineThree)
{
    name = "RdmaWorkerPostSendSglInlineThree";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    uint32_t immData = 0;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(rdmaCtx));
    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(false));
    MOCKER_CPP(&RDMAOpContextInfoPool::Return).stubs().will(returnValue(0));
    RResult ret = mWorker->PostSendSglInline(qp, header, req, immData);
    EXPECT_EQ(ret, 230);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineFive)
{
    name = "RdmaWorkerPostSendSglInlineFive";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    req.upCtxSize = 100;
    uint32_t immData = 0;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(rdmaCtx));
    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    RResult ret = mWorker->PostSendSglInline(qp, header, req, immData);
    EXPECT_EQ(ret, 200);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineSix)
{
    name = "RdmaWorkerPostSendSglInlineSix";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    uint32_t immData = 0;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(rdmaCtx));
    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));

    MOCKER_CPP(&RDMAQp::PostSendSglInline).stubs().will(returnValue(201));
    MOCKER_CPP(&RDMAQp::ReturnPostSendWr).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAQp::DecreaseRef).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAOpContextInfoPool::Return).stubs().will(returnValue(0));
    RResult ret = mWorker->PostSendSglInline(qp, header, req, immData);
    EXPECT_EQ(ret, 201);
}

TEST_F(TestNetRdmaWorker, RdmaWorkerPostSendSglInlineSeven)
{
    name = "RdmaWorkerPostSendSglInlineSeven";
    RDMASendSglInlineHeader header;
    RDMASendReadWriteRequest req;
    uint32_t immData = 0;
    MOCKER_CPP(&RDMAOpContextInfoPool::Get).stubs().will(returnValue(rdmaCtx));
    MOCKER_CPP(&RDMAQp::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAQp::PostSendSglInline).stubs().will(returnValue(0));
    RResult ret = mWorker->PostSendSglInline(qp, header, req, immData);
    EXPECT_EQ(ret, 0);
}

}
}
//#endif