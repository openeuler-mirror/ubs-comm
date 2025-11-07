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

class TestNetRdmaDriverOob : public testing::Test {
public:
    TestNetRdmaDriverOob();
    virtual void SetUp(void);
    virtual void TearDown(void);
    const std::string name = "TestNetRdmaDriverOob";
    NetDriverRDMAWithOob *testDriver = nullptr;
};

TestNetRdmaDriverOob::TestNetRdmaDriverOob() {}

void TestNetRdmaDriverOob::SetUp()
{
    bool startOobSvr = true;
    UBSHcomNetDriverProtocol protocol = RDMA;
    testDriver = new (std::nothrow) NetDriverRDMAWithOob(name, startOobSvr, protocol);
    ASSERT_NE(testDriver, nullptr);
}

void TestNetRdmaDriverOob::TearDown()
{
    if (testDriver != nullptr) {
        delete testDriver;
        testDriver = nullptr;
    }

    GlobalMockObject::verify();
}

OOBTCPConnection *newConn = nullptr;
NResult MockConnect(const std::string &ip, uint32_t port, OOBTCPConnection *&conn)
{
    conn = newConn;
    conn->SetIpAndPort("xx.xx", 1);
    return NN_OK;
}

NResult MockReceiveTest(void *&buf, uint32_t size)
{
    ConnectHeader *bufHeader = reinterpret_cast<ConnectHeader*>(buf);
    bufHeader->devIndex = NN_NO4;
    return NN_OK;
}

TEST_F(TestNetRdmaDriverOob, TestConnectMultiRailFail)
{
    std::string oobIp = "127.0.0.1";
    uint16_t oobPort = 1;
    testDriver->mOptions.enableMultiRail = true;
    testDriver->mEnableTls = false;
    std::string payload = "Test";
    UBSHcomNetEndpointPtr outEp;
    uint32_t flags = 0;
    uint8_t serverGrpNo = 1;
    uint8_t clientGrpNo = 1;
    uint64_t ctx = 1;
    int fd = -1;
    testDriver->mInited = true;
    testDriver->mStarted = true;
    newConn = new (std::nothrow) OOBTCPConnection(fd);
    newConn->IncreaseRef();

    MOCKER_CPP(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*newConn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*newConn, &OOBTCPConnection::Receive).stubs().will(invoke(MockReceiveTest));

    NResult res = testDriver->Connect(oobIp, oobPort, payload, outEp, flags, serverGrpNo, clientGrpNo, ctx);
    EXPECT_EQ(res, NN_ERROR);

    newConn->DecreaseRef();
}

TEST_F(TestNetRdmaDriverOob, DestroyEpByPortNum)
{
    UBSHcomNetWorkerIndex index {};
    RDMAAsyncEndPoint *ep1 = (RDMAAsyncEndPoint *)malloc(sizeof(RDMAAsyncEndPoint));
    RDMAQp *qp1 = (RDMAQp *)malloc(sizeof(RDMAQp));
    RDMAContext *context1 = (RDMAContext *)malloc(sizeof(RDMAContext));
    ep1->mQP = qp1;
    ep1->mQP->mRDMAContext = context1;
    ep1->mQP->mRDMAContext->mPortNumber = 0;
    UBSHcomNetEndpointPtr fakeEp1 = new (std::nothrow) NetAsyncEndpoint(0, ep1, testDriver, index);
    testDriver->IncreaseRef();
    testDriver->mEndPoints.emplace(fakeEp1->mId, fakeEp1);

    RDMAAsyncEndPoint *ep2 = (RDMAAsyncEndPoint *)malloc(sizeof(RDMAAsyncEndPoint));
    RDMAQp *qp2 = (RDMAQp *)malloc(sizeof(RDMAQp));
    RDMAContext *context2 = (RDMAContext *)malloc(sizeof(RDMAContext));
    ep2->mQP = qp2;
    ep2->mQP->mRDMAContext = context2;
    ep2->mQP->mRDMAContext->mPortNumber = 1;
    UBSHcomNetEndpointPtr fakeEp2 = new (std::nothrow) NetAsyncEndpoint(1, ep2, testDriver, index);
    testDriver->IncreaseRef();
    testDriver->mEndPoints.emplace(fakeEp2->mId, fakeEp2);

    MOCKER_CPP(&NetDriverRDMAWithOob::ProcessEpError).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(testDriver->DestroyEpByPortNum(1));
    free(context1);
    free(context2);
    free(qp1);
    free(qp2);
    free(ep1);
    free(ep2);
}

TEST_F(TestNetRdmaDriverOob, HandlePortDown)
{
    RDMAWorker *fakeWorker = (RDMAWorker *)malloc(sizeof(RDMAWorker));
    RDMAContext *context = (RDMAContext *)malloc(sizeof(RDMAContext));
    fakeWorker->mRDMAContext = context;
    fakeWorker->mRDMAContext->mPortNumber = 1;
    testDriver->mWorkers.emplace_back(fakeWorker);
    MOCKER_CPP(&RDMAWorker::Stop).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(testDriver->HandlePortDown(1));
    free(context);
    free(fakeWorker);
}

TEST_F(TestNetRdmaDriverOob, HandlePortActive)
{
    RDMAWorker *fakeWorker = (RDMAWorker *)malloc(sizeof(RDMAWorker));
    RDMAContext *context = (RDMAContext *)malloc(sizeof(RDMAContext));
    fakeWorker->mRDMAContext = context;
    fakeWorker->mRDMAContext->mPortNumber = 1;
    testDriver->mWorkers.emplace_back(fakeWorker);
    MOCKER_CPP(&RDMAWorker::Start).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(testDriver->HandlePortActive(1));
    free(context);
    free(fakeWorker);
}

TEST_F(TestNetRdmaDriverOob, DestroyEpInWorker)
{
    UBSHcomNetWorkerIndex index{};
    RDMAWorker *fakeWorker = (RDMAWorker *)malloc(sizeof(RDMAWorker));
    RDMAContext *context1 = (RDMAContext *)malloc(sizeof(RDMAContext));
    context1->mPortNumber = 0;
    fakeWorker->mRDMAContext = context1;
    testDriver->mWorkers.emplace_back(fakeWorker);

    RDMAAsyncEndPoint *ep1 = (RDMAAsyncEndPoint *)malloc(sizeof(RDMAAsyncEndPoint));
    RDMAQp *qp1 = (RDMAQp *)malloc(sizeof(RDMAQp));
    ep1->mQP = qp1;
    ep1->mQP->mRDMAContext = context1;
    ep1->mWorker = fakeWorker;
    UBSHcomNetEndpointPtr fakeEp1 = new (std::nothrow) NetAsyncEndpoint(0, ep1, testDriver, fakeWorker->mIndex);
    testDriver->IncreaseRef();
    testDriver->mEndPoints.emplace(fakeEp1->mId, fakeEp1);

    RDMAAsyncEndPoint *ep2 = (RDMAAsyncEndPoint *)malloc(sizeof(RDMAAsyncEndPoint));
    RDMAQp *qp2 = (RDMAQp *)malloc(sizeof(RDMAQp));
    RDMAContext *context2 = (RDMAContext *)malloc(sizeof(RDMAContext));
    ep2->mQP = qp2;
    ep2->mQP->mRDMAContext = context2;
    ep2->mQP->mRDMAContext->mPortNumber = 1;
    UBSHcomNetEndpointPtr fakeEp2 = new (std::nothrow) NetAsyncEndpoint(1, ep2, testDriver, index);
    testDriver->IncreaseRef();
    testDriver->mEndPoints.emplace(fakeEp2->mId, fakeEp2);

    MOCKER_CPP(&NetDriverRDMAWithOob::ProcessEpError).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(testDriver->DestroyEpInWorker(fakeWorker));
    free(context1);
    free(context2);
    free(qp1);
    free(qp2);
    free(ep1);
    free(ep2);
    free(fakeWorker);
}

TEST_F(TestNetRdmaDriverOob, HandleCqEventParamErr)
{
    ibv_async_event event{};
    ibv_cq *cq = (ibv_cq *)malloc(sizeof(ibv_cq));
    event.element.cq = cq;
    event.element.cq->cq_context = nullptr;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleCqEvent(&event));

    RDMAWorker *fakeWorker = (RDMAWorker *)malloc(sizeof(RDMAWorker));
    event.element.cq->cq_context = (void *)fakeWorker;
    MOCKER_CPP(&RDMAWorker::Stop).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleCqEvent(&event));
    free(cq);
    free(fakeWorker);
}

TEST_F(TestNetRdmaDriverOob, HandleCqEvent)
{
    RDMAWorker *fakeWorker = (RDMAWorker *)malloc(sizeof(RDMAWorker));
    ibv_async_event event{};
    ibv_cq *cq = (ibv_cq *)malloc(sizeof(ibv_cq));
    event.element.cq = cq;
    event.element.cq->cq_context = (void *)fakeWorker;

    MOCKER_CPP(&RDMAWorker::Stop).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMAWithOob::DestroyEpInWorker).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAWorker::ReInitializeCQ).stubs()
            .will(returnValue(1))
            .then(returnValue(0));
    MOCKER_CPP(&RDMAWorker::Start).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleCqEvent(&event));
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleCqEvent(&event));
    free(cq);
    free(fakeWorker);
}

TEST_F(TestNetRdmaDriverOob, HandleAsyncEvent)
{
    RDMAContext *ctx = nullptr;
    RDMACq *cq = nullptr;
    ibv_async_event event {};
    ibv_qp *qp = (ibv_qp *)malloc(sizeof(ibv_qp));
    RDMAQp *rdmaQp = new RDMAQp("rdma qp", 0, ctx, cq);
    qp->qp_context = (void *)rdmaQp;
    event.element.qp = qp;
    char *name = "qp";

    MOCKER_CPP(&NetDriverRDMAWithOob::HandleCqEvent).expects(once());
    event.event_type = IBV_EVENT_CQ_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));

    MOCKER_CPP(&NetDriverRDMAWithOob::HandlePortDown).expects(once());
    event.event_type = IBV_EVENT_PORT_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));

    MOCKER_CPP(&NetDriverRDMAWithOob::HandlePortActive).expects(once());
    event.event_type = IBV_EVENT_PORT_ACTIVE;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));

    MOCKER_CPP(&RDMAContext::UpdateGid).expects(once()).will(returnValue(0));
    event.event_type = IBV_EVENT_GID_CHANGE;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));

    event.event_type = IBV_EVENT_QP_FATAL;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_QP_REQ_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_QP_ACCESS_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_COMM_EST;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_SQ_DRAINED;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_PATH_MIG;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_PATH_MIG_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_QP_LAST_WQE_REACHED;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_SRQ_ERR;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_SRQ_LIMIT_REACHED;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_LID_CHANGE;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_PKEY_CHANGE;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_SM_CHANGE;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_CLIENT_REREGISTER;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));
    event.event_type = IBV_EVENT_DEVICE_FATAL;
    EXPECT_NO_FATAL_FAILURE(testDriver->HandleAsyncEvent(&event));

    delete(rdmaQp);
    free(qp);
}

int MockRequestPostedHandler(const UBSHcomNetRequestContext &)
{
    return 0;
}

TEST_F(TestNetRdmaDriverOob, SendFinishedCB)
{
    RDMAOpContextInfo ctx {};
    ctx.opType = RDMAOpContextInfo::SEND_RAW_SGL;
    ctx.upCtxSize = 1;
    RDMAQp *qp = (RDMAQp *)malloc(sizeof(RDMAQp));
    ctx.qp = qp;
    UBSHcomNetEndpoint *ep = (UBSHcomNetEndpoint *)malloc((sizeof(UBSHcomNetEndpoint)));
    ctx.qp->mUpContext = (uintptr_t)ep;
    RDMASgeCtxInfo sgeCtx {};
    RDMASglContextInfo sglCtx {};
    sgeCtx.ctx = &sglCtx;
    memcpy_s(ctx.upCtx, sizeof(RDMASgeCtxInfo), &sgeCtx, sizeof(RDMASgeCtxInfo));
    ctx.upCtxSize = sizeof(RDMASgeCtxInfo);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAQp::ReturnPostSendWr).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAWorker::ReturnSglContextInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&RDMAMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));
    testDriver->mRequestPostedHandler = MockRequestPostedHandler;
    testDriver->mEnableTls = true;

    int result = testDriver->SendFinishedCB(&ctx);
    EXPECT_EQ(result, NN_OK);
    free(ep);
    free(qp);
}

TEST_F(TestNetRdmaDriverOob, ProcessErrorSendFinished)
{
    RDMAOpContextInfo ctx {};
    EXPECT_NO_FATAL_FAILURE(testDriver->ProcessErrorSendFinished(&ctx));

    RDMAQp *qp = (RDMAQp *)malloc(sizeof(RDMAQp));
    qp->mUpContext1 = 1;
    ctx.qp = qp;
    EXPECT_NO_FATAL_FAILURE(testDriver->ProcessErrorSendFinished(&ctx));
    free(qp);
}

TEST_F(TestNetRdmaDriverOob, TestRDMAMemoryRegionCreate)
{
    int ret;
    std::string name = "mr";
    RDMAContext *ctx = nullptr;
    uint64_t size = NN_NO64;
    RDMAMemoryRegion *buf = nullptr;
    uintptr_t address = 0;
 
    ret = RDMAMemoryRegion::Create(name, ctx, size, buf);
    EXPECT_EQ(ret, RR_PARAM_INVALID);
 
    ret = RDMAMemoryRegion::Create(name, ctx, address, size, buf);
    EXPECT_EQ(ret, RR_PARAM_INVALID);
}

TEST_F(TestNetRdmaDriverOob, TestRDMAFixBufferCreate)
{
    int ret;
    std::string name = "mr";
    RDMAContext *ctx = nullptr;
    RDMAMemoryRegionFixedBuffer *mr = nullptr;
 
    mr = new RDMAMemoryRegionFixedBuffer(name, ctx, 0, 0);
    EXPECT_NE(mr->Initialize(), RR_OK);
}

TEST_F(TestNetRdmaDriverOob, TestDriverInitializeFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};
 
    testDriver->mInited = false;
    option.enableTls = false;
 
    MOCKER_CPP(&NetDriverRDMA::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateWorkerResource).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateWorkers).stubs().will(returnValue(1));
 
    ret = testDriver->Initialize(option);
    EXPECT_NE(ret, 0);
 
    ret = testDriver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetRdmaDriverOob, TestDriverInitializeFail2)
{
    int ret;
    UBSHcomNetDriverOptions option{};
 
    testDriver->mInited = false;
    option.enableTls = false;
 
    MOCKER_CPP(&NetDriverRDMA::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&RDMAContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateWorkers).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateClientLB).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateListeners).stubs().will(returnValue(1));
 
    ret = testDriver->Initialize(option);
    EXPECT_NE(ret, 0);
 
    ret = testDriver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetRdmaDriverOob, CreateWorkerResourceFail)
{
    int ret;
 
    MOCKER_CPP(&NetDriverRDMA::CreateSendMr).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateOpCtxMemPool).stubs().will(returnValue(1));
 
    ret = testDriver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
 
    ret = testDriver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetRdmaDriverOob, CreateWorkerResourceFail2)
{
    int ret;
 
    MOCKER_CPP(&NetDriverRDMA::CreateSendMr).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateOpCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverRDMA::CreateSglCtxMemPool).stubs().will(returnValue(1));
 
    ret = testDriver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetRdmaDriverOob, Connect)
{
    int ret;
    testDriver->mInited = true;
    testDriver->mStarted = true;
    std::string badUrl = "unknown://127.0.0.1";
    std::string serverUrl = "tcp://127.0.0.1:9981";
    std::string payload{};
    UBSHcomNetEndpointPtr outEp;
    MOCKER_CPP(&NetDriverRDMAWithOob::Connect,
        NResult(NetDriverRDMAWithOob::*)(const OOBTCPClientPtr &, const std::string &, UBSHcomNetEndpointPtr &, uint8_t,
        uint8_t, uint64_t)).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverRDMAWithOob::ConnectSyncEp).stubs().will(returnValue(0));
    ret = testDriver->Connect(badUrl, payload, outEp, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    testDriver->mEnableTls = true;
    ret = testDriver->Connect(serverUrl, payload, outEp, 0, 0, 0, 0);
    EXPECT_EQ(ret, 1);

    testDriver->mEnableTls = false;
    ret = testDriver->Connect(serverUrl, payload, outEp, NET_EP_SELF_POLLING, 0, 0, 0);
    EXPECT_EQ(ret, 0);
}

static ssize_t MockSend(int socket, void const *buf, size_t size, int flags)
{
    return size;
}

ConnectResp mockResp;
static ssize_t MockRecv(int socket, void *buf, size_t size, int flags)
{
    switch (size) {
        case sizeof(ConnectHeader): {
            ConnectHeader *tmp = reinterpret_cast<ConnectHeader *>(buf);
            tmp->magic = 1;
            tmp->protocol = UBSHcomNetDriverProtocol::RDMA;
            break;
        }
        case sizeof(uint32_t): {
            uint32_t *tmp = reinterpret_cast<uint32_t *>(buf);
            *tmp = 1;
            break;
        }
        case sizeof(ConnRespWithUId): {
            ConnRespWithUId *tmp = reinterpret_cast<ConnRespWithUId *>(buf);
            tmp->connResp = mockResp;
            break;
        }
        default:
            break;
    }

    return size;
}

TEST_F(TestNetRdmaDriverOob, Connect2)
{
    std::string ip("127.0.0.1");
    std::string payload{};
    UBSHcomNetEndpointPtr outEp;
    OOBTCPClientPtr client = new (std::nothrow) OOBTCPClient(ip, 1);
    ASSERT_NE(client.Get(), nullptr);
    client->mOobType = NET_OOB_UDS;
    testDriver->mOptions.enableMultiRail = true;
    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, int &)).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockSend));
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), RR_PARAM_INVALID);
}

TEST_F(TestNetRdmaDriverOob, Connect3)
{
    std::string ip("127.0.0.1");
    std::string payload{};
    UBSHcomNetEndpointPtr outEp;
    OOBTCPClientPtr client = new (std::nothrow) OOBTCPClient(ip, 1);
    ASSERT_NE(client.Get(), nullptr);
    client->mOobType = NET_OOB_UDS;
    RDMASyncEndpoint *rep = new (std::nothrow) RDMASyncEndpoint(ip, nullptr, BUSY_POLLING, nullptr, nullptr, 0);
    ASSERT_NE(rep, nullptr);
    testDriver->mOptions.enableMultiRail = true;
    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, int &)).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockSend));
    MOCKER(RDMASyncEndpoint::Create).stubs()
        .with(any(), any(), any(), any(), any(), outBound(rep))
        .will(returnValue(0));
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), RR_EP_NOT_INITIALIZED);
}

TEST_F(TestNetRdmaDriverOob, Connect4)
{
    std::string ip("127.0.0.1");
    std::string payload{};
    UBSHcomNetEndpointPtr outEp;
    OOBTCPClientPtr client = new (std::nothrow) OOBTCPClient(ip, 1);
    ASSERT_NE(client.Get(), nullptr);
    client->mOobType = NET_OOB_UDS;
    RDMASyncEndpoint *rep = new (std::nothrow) RDMASyncEndpoint(ip, nullptr, BUSY_POLLING, nullptr, nullptr, 0);
    ASSERT_NE(rep, nullptr);
    testDriver->mOptions.enableMultiRail = true;
    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, int &)).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockSend));
    MOCKER(RDMASyncEndpoint::Create).stubs()
        .with(any(), any(), any(), any(), any(), outBound(rep))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*rep, &RDMASyncEndpoint::Initialize).stubs().will(returnValue(0));
    rep->IncreaseRef();
    mockResp = MAGIC_MISMATCH;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);
    rep->IncreaseRef();
    mockResp = WORKER_GRPNO_MISMATCH;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);
    rep->IncreaseRef();
    mockResp = PROTOCOL_MISMATCH;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_CONNECT_PROTOCOL_MISMATCH);
    rep->IncreaseRef();
    mockResp = SERVER_INTERNAL_ERROR;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_ERROR);
    rep->IncreaseRef();
    mockResp = VERSION_MISMATCH;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);
    mockResp = TLS_VERSION_MISMATCH;
    EXPECT_EQ(testDriver->ConnectSyncEp(client, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);
}

}
}