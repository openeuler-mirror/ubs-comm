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

#ifdef UB_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <fcntl.h>
#include <sys/poll.h>

#include "net_monotonic.h"
#include "net_oob_ssl.h"
#include "net_ub_endpoint.h"
#include "ub_mr_fixed_buf.h"
#include "ub_worker.h"
#include "net_ub_driver_oob.h"
#include "net_oob_secure.h"
#include "ub_urma_wrapper_jetty.h"

namespace ock {
namespace hcom {

int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload << " id " << newEP->Id());
    UBSHcomNetEndpoint *ep = newEP.Get();
    reinterpret_cast<NetUBAsyncEndpoint *>(ep)->mDriver = nullptr;
    return 0;
}

int RequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int OneSideDone(const UBSHcomNetRequestContext &ctx)
{
    return 1;
}

void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    UBSHcomNetEndpointPtr tmpEp = ep;
    tmpEp.Set(nullptr);
}

int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    UBSHcomNetMessage *msg = ctx.Message();
    if (msg->mBuf != nullptr) {
        free(msg->mBuf);
        msg->mBuf = nullptr;
    }
    return 1;
}

class TestNetDriverUBWithOob : public testing::Test {
public:
    TestNetDriverUBWithOob();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string mName = "TestNetDriverUBWithOob";
    NetDriverUBWithOob *driver = nullptr;
    UBSHcomNetDriverOptions option{};
    UBContext *ctx = nullptr;
    UBEId eid{};
    urma_context_t mUrmaContext{};
    char mem[NN_NO8]{};
    JettyOptions jettyOptions{};
    // worker
    UBWorker *worker = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    // qp
    UBJetty *qp = nullptr;
    // lb
    NetWorkerLB *lb = nullptr;
    // tSeg
    urma_target_seg_t tSeg{};
    // jfc
    UBJfc *jfc = nullptr;
    // ctxInfo
    UBOpContextInfo ctxInfo{};
    // CallbackEp
    NetUBAsyncEndpoint *CallbackEp = nullptr;
};

TestNetDriverUBWithOob::TestNetDriverUBWithOob() {}

void TestNetDriverUBWithOob::SetUp()
{
    // create ctx
    ctx = new (std::nothrow) UBContext("ubTest", eid);
    ASSERT_NE(ctx, nullptr);
    ctx->mUrmaContext = &mUrmaContext;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    // create drivver
    driver = new (std::nothrow) NetDriverUBWithOob(mName, true, UBSHcomNetDriverProtocol::UBC);
    ASSERT_NE(driver, nullptr);
    driver->mOptions.enableTls = false;
    driver->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    driver->mEnableTls = false;
    driver->mContext = ctx;
    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    driver->mInited = true;
    driver->mStarted = true;
    driver->IncreaseRef();
    driver->mMajorVersion = 0;
    // create worker
    worker = new (std::nothrow) UBWorker(mName, ctx, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);
    worker->mInited = true;
    worker->IncreaseRef();
    // create qp
    qp = new (std::nothrow) UBJetty(mName, 0, ctx, nullptr);
    ASSERT_NE(qp, nullptr);
    qp->IncreaseRef();
    qp->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    qp->StoreExchangeInfo(new UBJettyExchangeInfo);
    // create lb
    lb = new (std::nothrow) NetWorkerLB(mName, UBSHcomNetDriverLBPolicy::NET_ROUND_ROBIN, 0);
    ASSERT_NE(lb, nullptr);
    lb->IncreaseRef();
    // initialize ctxInfo
    ctxInfo.ubJetty = qp;
    ctxInfo.upCtxSize = 1;
    // create CallbackEp
    CallbackEp = new (std::nothrow) NetUBAsyncEndpoint(1, qp, nullptr, worker);
    ASSERT_NE(CallbackEp, nullptr);
    CallbackEp->IncreaseRef();
    qp->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
}

void TestNetDriverUBWithOob::TearDown()
{
    if (driver != nullptr) {
        driver->mContext = nullptr;
        delete driver;
        driver = nullptr;
    }
    if (CallbackEp != nullptr) {
        CallbackEp->mJetty = nullptr;
        delete CallbackEp;
        CallbackEp = nullptr;
    }
    if (worker != nullptr) {
        worker->mUBContext = nullptr;
        delete worker;
        worker = nullptr;
    }
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
    if (ctx != nullptr) {
        ctx->mUrmaContext = nullptr;
        delete ctx;
        ctx = nullptr;
    }
    if (lb != nullptr) {
        delete lb;
        lb = nullptr;
    }
    if (jfc != nullptr) {
        delete jfc;
        jfc = nullptr;
    }
    GlobalMockObject::verify();
}

static ssize_t MockRecv(int socket, void *buf, size_t size, int flags)
{
    switch (size) {
        case sizeof(ConnectHeader): {
            ConnectHeader *tmp = reinterpret_cast<ConnectHeader *>(buf);
            tmp->magic = 1;
            tmp->protocol = UBSHcomNetDriverProtocol::UBC;
            break;
        }
        case sizeof(uint32_t): {
            uint32_t *tmp = reinterpret_cast<uint32_t *>(buf);
            *tmp = 1;
            break;
        }
        default:
            break;
    }

    return size;
}

static ssize_t MockRecvFakeSize(int socket, void *buf, size_t size, int flags)
{
    switch (size) {
        case sizeof(ConnectHeader): {
            ConnectHeader *tmp = reinterpret_cast<ConnectHeader *>(buf);
            tmp->magic = 1;
            tmp->protocol = UBSHcomNetDriverProtocol::UBC;
            break;
        }
        case sizeof(uint32_t): {
            uint32_t *tmp = reinterpret_cast<uint32_t *>(buf);
            *tmp = NN_NO1000;
            break;
        }
        default:
            break;
    }

    return size;
}

static ssize_t MockRecvUBC(int socket, void *buf, size_t size, int flags)
{
    switch (size) {
        case sizeof(ConnectHeader): {
            ConnectHeader *tmp = reinterpret_cast<ConnectHeader *>(buf);
            tmp->magic = 1;
            tmp->protocol = UBSHcomNetDriverProtocol::UBC;
            break;
        }
        case sizeof(uint32_t): {
            uint32_t *tmp = reinterpret_cast<uint32_t *>(buf);
            *tmp = 1;
            break;
        }
        default:
            break;
    }
 
    return size;
}

static ssize_t MockConnSend(int socket, void const *buf, size_t size, int flags)
{
    return size;
}

TEST_F(TestNetDriverUBWithOob, NewConnectionCBSecErr)
{
    OOBTCPConnection conn(-1);
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Receive).stubs()
        .will(returnValue(1));
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_OOB_SEC_PROCESS_ERROR);
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, NewConnectionCBMagicErr)
{
    OOBTCPConnection conn(-1);
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs()
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs()
        .will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mOptions.magic = 1;
    driver->mProtocol = UBSHcomNetDriverProtocol::RDMA;
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, NewConnectionLbErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs()
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs()
        .will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs()
        .will(returnValue(false));
    driver->mOptions.magic = 1;
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, NewConnectionWorkerErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(false));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionQpCreateErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().will(returnValue(1));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionQpErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionHccsSizeCheckErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecvFakeSize));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionUBCHeartBeat)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecvUBC));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0)).then(returnValue(1));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(1));

    driver->mOptions.magic = 1;
    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    driver->mWorkers.emplace_back(worker);
    driver->mHeartBeat = new (std::nothrow) NetHeartbeat(driver, NN_NO60, NN_NO2);
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    EXPECT_EQ(driver->NewConnectionCB(conn), 1);
    EXPECT_EQ(driver->NewConnectionCB(conn), 1);
    driver->mWorkers.clear();
    if (driver->mHeartBeat != nullptr) {
        delete driver->mHeartBeat;
        driver->mHeartBeat = nullptr;
    }
}

TEST_F(TestNetDriverUBWithOob, NewConnectionExchangeErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(1));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    EXPECT_EQ(driver->NewConnectionCB(conn), 1);

    // 必须放在最后 MOCK，保证之前通过 std::nothrow new 分配的实例已完成。否则会
    // 遇到 NetLogger 的 this 为空.
    MOCKER_CPP(&operator new, void *(*)(size_t, const std::nothrow_t &))
            .stubs()
            .will(returnValue(static_cast<void *>(nullptr)));
    EXPECT_EQ(driver->NewConnectionCB(conn), NN_MALLOC_FAILED);

    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionPostRecvErr)
{
    int err = 1;
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(err));
    MOCKER_CPP(&UBJetty::Stop).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    driver->mOptions.magic = err;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), err);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionGetbufferErr)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_MALLOC_FAILED);
    EXPECT_EQ(driver->NewConnectionCB(conn), 0);
    driver->mWorkers.clear();
}

template<typename T> void *NewExceptFor(size_t sz, const std::nothrow_t &)
{
    if (sz == sizeof(T)) {
        return nullptr;
    }

    return std::malloc(sz);
}

TEST_F(TestNetDriverUBWithOob, NewConnectionAllocEPFail)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));
    MOCKER_CPP(&UBJetty::Stop).stubs().will(returnValue(0));
    MOCKER_CPP(&operator new, void *(*)(size_t, const std::nothrow_t &))
        .stubs()
        .will(invoke(NewExceptFor<NetUBAsyncEndpoint>));
    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_NEW_OBJECT_FAILED);
    driver->mWorkers.clear();
}

int NewEPFail(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("Mock user callback fail");
    UBSHcomNetEndpoint *ep = newEP.Get();
    reinterpret_cast<NetUBAsyncEndpoint *>(ep)->mDriver = nullptr;
    return 1;
}

TEST_F(TestNetDriverUBWithOob, NewConnectionUsrCbFail)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    driver->RegisterNewEPHandler(
        std::bind(&NewEPFail, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, NewConnectionTest)
{
    OOBTCPConnection conn(-1);
    conn.mLb = lb;
    conn.mIpAndPort = "192.168.1.1:5684";
    MOCKER(OOBSecureProcess::SecProcessInOOBServer).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(conn, &OOBTCPConnection::Send).stubs().will(returnValue(0));
    MOCKER(::recv).stubs().will(invoke(MockRecv));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    driver->mOptions.magic = 1;
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->NewConnectionCB(conn), NN_MALLOC_FAILED);
    EXPECT_EQ(driver->NewConnectionCB(conn), 0);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, ConnectBranch)
{
    std::string payload{};
    UBSHcomNetEndpointPtr ep = nullptr;
    driver->mInited = false;
    
    driver->mOptions.oobType = NET_OOB_TCP;
    EXPECT_EQ(driver->Connect(payload, ep, 0, 0, 0), NN_ERROR);

    driver->mOptions.oobType = NET_OOB_UDS;
    EXPECT_EQ(driver->Connect(payload, ep, 0, 0, 0), NN_ERROR);

    driver->mOptions.oobType = NET_OOB_TCP;
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectInitErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload{};

    driver->mInited = false;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);

    driver->mInited = true;
    driver->mStarted = false;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectParamErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload(NN_NO2048, 'a');

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectTCPErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_OOB_SEC_PROCESS_ERROR);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectTCPRecvErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(returnValue(ssize_t(0)));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_OOB_CONN_RECEIVE_ERROR);
}

ConnectResp respCode = ConnectResp::OK;
int8_t ready = -1;
static ssize_t MockConnRecv(int socket, void *buf, size_t size, int flags)
{
    switch (size) {
        case sizeof(ConnRespWithUId): {
            ConnRespWithUId *tmpConnResp = reinterpret_cast<ConnRespWithUId *>(buf);
            tmpConnResp->connResp = respCode;
            break;
        }
        case sizeof(uint32_t): {
            uint32_t *tmp = reinterpret_cast<uint32_t *>(buf);
            *tmp = 1;
            break;
        }
        case sizeof(int8_t): {
            int8_t *tmp = reinterpret_cast<int8_t *>(buf);
            *tmp = ready;
            break;
        }
        default:
            break;
    }

    return size;
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectTCPAckErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = MAGIC_MISMATCH;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_CONNECT_REFUSED);

    respCode = WORKER_GRPNO_MISMATCH;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_CONNECT_REFUSED);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectProtoErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = PROTOCOL_MISMATCH;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_CONNECT_PROTOCOL_MISMATCH);

    respCode = SERVER_INTERNAL_ERROR;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectElseErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = CONN_ACCEPT_QUEUE_FULL;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectWorkerErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(false));
    driver->mWorkers.emplace_back(worker);

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectQpCreateErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectQpErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectUBCHeartBeatErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);
    driver->mHeartBeat = new (std::nothrow) NetHeartbeat(driver, NN_NO60, NN_NO2);
    qp->mUBContext->protocol = UBSHcomNetDriverProtocol::UBC;

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::CreateHBMemoryRegion).stubs().will(returnValue(0)).then(returnValue(1));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);
    driver->mWorkers.clear();
    if (driver->mHeartBeat != nullptr) {
        delete driver->mHeartBeat;
        driver->mHeartBeat = nullptr;
    }
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectExchangeErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);

    // 必须放在最后 MOCK，保证之前通过 std::nothrow new 分配的实例已完成。否则会
    // 遇到 NetLogger 的 this 为空.
    //
    // 并且在正常流程中也会遇到 OOBTCPClient, OOBTCPConnection, UBJetty 等通过
    // std::nothrow 版本的 new 来分配内存，这些必须避免。
    MOCKER_CPP(&operator new, void *(*)(size_t, const std::nothrow_t &))
            .stubs()
            .will(invoke(NewExceptFor<UBJettyExchangeInfo>));
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_MALLOC_FAILED);

    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectPostrecvErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(1));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), 1);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectReadyErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    ready = -1;
    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_ERROR);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, AsyncConnectSuccess)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;
    ready = 1;
    driver->mWorkers.emplace_back(worker);

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&NetWorkerLB::ChooseWorker).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::IsWorkStarted).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::CreateQP).stubs().with(outBound(qp)).will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(driver->Connect(oobIp, oobPort, payload, outEp, 0, 0, 0, 0), NN_OK);
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEp)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(1));
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), 1);
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_OOB_SEC_PROCESS_ERROR);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEpCreateResourcesErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), 1);
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEpQpErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEpTCPAckErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = MAGIC_MISMATCH;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);

    respCode = WORKER_GRPNO_MISMATCH;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_CONNECT_REFUSED);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEpProtoErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = PROTOCOL_MISMATCH;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_CONNECT_PROTOCOL_MISMATCH);

    respCode = SERVER_INTERNAL_ERROR;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncEpElseErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));

    respCode = CONN_ACCEPT_QUEUE_FULL;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncExchangeErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(1));

    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), 1);
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), 1);

    // 必须放在最后 MOCK，保证之前通过 std::nothrow new 分配的实例已完成。否则会
    // 遇到 NetLogger 的 this 为空.
    //
    // 并且在正常流程中也会遇到 OOBTCPClient, OOBTCPConnection, UBJetty 等通过
    // std::nothrow 版本的 new 来分配内存，这些必须避免。
    MOCKER_CPP(&operator new, void *(*)(size_t, const std::nothrow_t &))
            .stubs()
            .will(invoke(NewExceptFor<UBJettyExchangeInfo>));
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_MALLOC_FAILED);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncPostrecvErr)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&NetUBSyncEndpoint::PostReceive).stubs().will(returnValue(1));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), 1);
}

TEST_F(TestNetDriverUBWithOob, ConnectSyncSuccess)
{
    std::string oobIp = "192.168.1.1";
    uint16_t oobPort = 1;
    UBSHcomNetEndpointPtr outEp = nullptr;
    std::string payload("hello world");
    respCode = ConnectResp::OK;

    MOCKER(OOBTCPClient::ConnectWithFd, NResult(const std::string &, uint32_t, int &)).stubs().will(returnValue(0));
    MOCKER(OOBSecureProcess::SecProcessInOOBClient).stubs().will(returnValue(0));
    MOCKER(NetUBSyncEndpoint::CreateResources).stubs()
        .with(any(), any(), any(), any(), outBound(qp), outBound(jfc))
        .will(returnValue(0));
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::Initialize).stubs().will(returnValue(0));
    MOCKER(::send).stubs().will(invoke(MockConnSend));
    MOCKER(::recv).stubs().will(invoke(MockConnRecv));
    MOCKER_CPP(&UBJetty::FillExchangeInfo).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::ChangeToReady).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetFreeBufferN).stubs().will(returnValue(true));
    MOCKER_CPP(&NetUBSyncEndpoint::PostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetMemorySeg).stubs().will(returnValue(&tSeg));

    ready = -1;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_ERROR);
    ready = 1;
    EXPECT_EQ(driver->ConnectSyncEp(oobIp, oobPort, payload, outEp, 0, 0, 0), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, ProcessErrorNewRequestParamErr)
{
    EXPECT_NO_FATAL_FAILURE(driver->ProcessErrorNewRequest(nullptr));
}

TEST_F(TestNetDriverUBWithOob, ProcessErrorNewRequest)
{
    ctxInfo.opType = UBOpContextInfo::RECEIVE;
    MOCKER_CPP(&UBJetty::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(driver->ProcessErrorNewRequest(&ctxInfo));

    ctxInfo.opType = UBOpContextInfo::SEND_RAW;
    EXPECT_NO_FATAL_FAILURE(driver->ProcessErrorNewRequest(&ctxInfo));
}

TEST_F(TestNetDriverUBWithOob, SendRawSglFinishedCB)
{
    UBSHcomNetRequestContext netCtx{};
    UBSglContextInfo sglCtx{};
    ctxInfo.upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
    auto upCtx = static_cast<UBSgeCtxInfo *>((void *)&(ctxInfo.upCtx));
    upCtx->ctx = &sglCtx;

    MOCKER_CPP(&UBWorker::ReturnSglContextInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    EXPECT_EQ(driver->SendRawSglFinishedCB(&ctxInfo, netCtx), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, SendFinishedCB)
{
    ctxInfo.opType = UBOpContextInfo::SEND;
    ctxInfo.upCtxSize = 1;

    MOCKER_CPP(&UBJetty::ReturnPostSendWr).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(false));
    MOCKER_CPP(&UBWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    EXPECT_EQ(driver->SendFinishedCB(&ctxInfo), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, ProcessErrorSendFinished)
{
    EXPECT_NO_FATAL_FAILURE(driver->ProcessErrorSendFinished(nullptr));
}

TEST_F(TestNetDriverUBWithOob, RWSglOneSideDoneCB)
{
    UBSHcomNetRequestContext netCtx{};
    UBSglContextInfo sglCtx{};
    ctxInfo.upCtxSize = static_cast<uint16_t>(sizeof(UBSgeCtxInfo));
    auto upCtx = static_cast<UBSgeCtxInfo *>((void *)&(ctxInfo.upCtx));
    upCtx->ctx = &sglCtx;
    sglCtx.iovCount = 1;

    MOCKER_CPP(&UBWorker::ReturnSglContextInfo).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    EXPECT_EQ(driver->RWSglOneSideDoneCB(&ctxInfo, netCtx), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, OneSideDoneCB)
{
    ctxInfo.opType = UBOpContextInfo::WRITE;
    ctxInfo.upCtxSize = 1;

    MOCKER_CPP(&UBJetty::ReturnOneSideWr).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBWorker::ReturnOpContextInfo).stubs().will(ignoreReturnValue());
    EXPECT_EQ(driver->OneSideDoneCB(&ctxInfo), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, ProcessErrorOneSideDone)
{
    EXPECT_NO_FATAL_FAILURE(driver->ProcessErrorOneSideDone(nullptr));
}

TEST_F(TestNetDriverUBWithOob, ProcessEpError)
{
    UBOpContextInfo remainingOpCtx{};
    UBOpContextInfo nextOpCtx{};
    remainingOpCtx.next = &nextOpCtx;
    uint32_t a = 1;
    uint32_t b = 0;
    remainingOpCtx.opType = UBOpContextInfo::OpType::SEND;
    remainingOpCtx.opResultType = UBOpContextInfo::SUCCESS;
    nextOpCtx.opType = UBOpContextInfo::OpType::WRITE;
    remainingOpCtx.opResultType = UBOpContextInfo::SUCCESS;
    CallbackEp->State().Set(NEP_ESTABLISHED);

    MOCKER_CPP(&UBJetty::Stop).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::GetCtxPosted).stubs().with(outBound(&remainingOpCtx)).will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUBWithOob::ProcessErrorSendFinished).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUBWithOob::ProcessErrorOneSideDone).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJetty::GetPostedCount).stubs().will(returnValue(a)).then(returnValue(b));

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(reinterpret_cast<uintptr_t>(CallbackEp)));
}

TEST_F(TestNetDriverUBWithOob, ProcessQPError)
{
    EXPECT_NO_FATAL_FAILURE(driver->ProcessQPError(nullptr));
}

TEST_F(TestNetDriverUBWithOob, ProcessTwoSideHeartbeat)
{
    UBSHcomNetRequestContext netCtx{};
    netCtx.mHeader.opCode == HB_SEND_OP;
    MOCKER_CPP(&UBJetty::PostSend).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(driver->ProcessTwoSideHeartbeat(&ctxInfo, netCtx));

    netCtx.mHeader.opCode == HB_RECV_OP;
    MOCKER_CPP(&NetUBAsyncEndpoint::HbRecordCount).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(driver->ProcessTwoSideHeartbeat(&ctxInfo, netCtx));
}

TEST_F(TestNetDriverUBWithOob, NewRequestParamErr)
{
    EXPECT_EQ(driver->NewRequest(nullptr), NN_ERROR);

    ctxInfo.opResultType = UBOpContextInfo::ERR_TIMEOUT;
    MOCKER_CPP(&NetDriverUBWithOob::ProcessQPError).stubs().will(ignoreReturnValue());
    EXPECT_EQ(driver->NewRequest(&ctxInfo), NN_OK);
}

TEST_F(TestNetDriverUBWithOob, NewRequestRecvRaw)
{
    ctxInfo.opResultType = UBOpContextInfo::SUCCESS;
    ctxInfo.opType = UBOpContextInfo::RECEIVE;
    // imm_data
    int *tmp = reinterpret_cast<int *>(ctxInfo.upCtx);
    *tmp = 1;
    // mrMemAddr free in callback
    UBSHcomNetTransHeader *header = (UBSHcomNetTransHeader *)malloc(sizeof(UBSHcomNetTransHeader) + NN_NO8);
    ctxInfo.dataSize = NN_NO8;
    ctxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(header);
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *)).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewRequest(&ctxInfo), NN_OK);
    free(header);
}

TEST_F(TestNetDriverUBWithOob, SendFinishedParamErr)
{
    EXPECT_EQ(driver->SendFinished(nullptr), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, SendFinished)
{
    ctxInfo.opResultType = UBOpContextInfo::ERR_TIMEOUT;
    MOCKER_CPP(&NetDriverUBWithOob::ProcessQPError).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUBWithOob::SendFinishedCB).stubs().will(returnValue(0));
    EXPECT_EQ(driver->SendFinished(&ctxInfo), NN_OK);

    ctxInfo.opResultType = UBOpContextInfo::SUCCESS;
    EXPECT_EQ(driver->SendFinished(&ctxInfo), 0);
}

TEST_F(TestNetDriverUBWithOob, OneSideDoneParamErr)
{
    EXPECT_EQ(driver->OneSideDone(nullptr), NN_ERROR);
}

TEST_F(TestNetDriverUBWithOob, OneSideDone)
{
    ctxInfo.opResultType = UBOpContextInfo::ERR_TIMEOUT;
    MOCKER_CPP(&NetDriverUBWithOob::ProcessQPError).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUBWithOob::OneSideDoneCB).stubs().will(returnValue(0));
    EXPECT_EQ(driver->OneSideDone(&ctxInfo), NN_OK);

    ctxInfo.opResultType = UBOpContextInfo::SUCCESS;
    EXPECT_EQ(driver->OneSideDone(&ctxInfo), 0);
}

std::string MockUBDetailName()
{
    std::string str = "test";
    return str;
}

TEST_F(TestNetDriverUBWithOob, NewRequestGetEpErr)
{
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    UBWorker *fakeWorker = (UBWorker *)malloc(sizeof(UBWorker));
    char fakeUpCtx[NN_NO16] = {};

    ctxInfo.ubJetty = qp1;
    ctxInfo.opResultType = UBOpContextInfo::SUCCESS;
    memcpy_s(ctxInfo.upCtx, sizeof(ctxInfo.upCtx), fakeUpCtx, sizeof(ctxInfo.upCtx));
    ctxInfo.opType = UBOpContextInfo::RECEIVE;
    driver->mOptions.enableTls = true;

    MOCKER_CPP(&UBJetty::GetUpContext1, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(reinterpret_cast<uintptr_t>(fakeWorker)));
    MOCKER_CPP(&UBJetty::GetUpContext, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(static_cast<uintptr_t>(0)));
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewRequest(&ctxInfo), NN_ERROR);
    free(qp1);
    free(fakeWorker);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRawRequestGetEpErr)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    driver->mOptions.enableTls = true;

    MOCKER_CPP(&UBJetty::GetUpContext, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(static_cast<uintptr_t>(0)));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewReceivedRawRequest(&ctxInfo, netCtx, msg, nullptr, 0), NN_ERROR);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestGetEpErr)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    driver->mOptions.enableTls = true;

    MOCKER_CPP(&UBJetty::GetUpContext, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(static_cast<uintptr_t>(0)));
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewReceivedRequest(&ctxInfo, netCtx, msg, nullptr), NN_ERROR);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestEnableTlsOnEpEnctypeTrueDecryptFail)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    ctxInfo.ubJetty->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    ctxInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    UBSHcomNetTransHeader header{};
    ctxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    driver->mOptions.enableTls = true;
    CallbackEp->mIsNeedEncrypt = true;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(false));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewReceivedRequest(&ctxInfo, netCtx, msg, nullptr), NN_DECRYPT_FAILED);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestEnableTlsOnEpEnctypeTrueDecryptSuccess)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    ctxInfo.ubJetty->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    ctxInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    UBSHcomNetTransHeader header{};
    ctxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    driver->mOptions.enableTls = true;
    CallbackEp->mIsNeedEncrypt = true;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(true));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewReceivedRequest(&ctxInfo, netCtx, msg, nullptr), NN_OK);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestEnableTlsOnEpEnctypeFalse)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    ctxInfo.ubJetty->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    ctxInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    UBSHcomNetTransHeader header{};
    ctxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    driver->mOptions.enableTls = true;
    CallbackEp->mIsNeedEncrypt = false;

    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));
    EXPECT_EQ(driver->NewReceivedRequest(&ctxInfo, netCtx, msg, nullptr), NN_INVALID_PARAM);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestEnableTlsOff)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    driver->mOptions.enableTls = false;

    MOCKER_CPP(&UBJetty::GetUpContext, uintptr_t(UBJetty::*)() const).stubs()
        .will(returnValue(static_cast<uintptr_t>(0)));
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::NewReceivedRequestWithoutCopy).stubs().will(returnValue(0));

    EXPECT_EQ(driver->NewReceivedRequest(&ctxInfo, netCtx, msg, nullptr), NN_OK);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewReceivedRequestWithoutCopy)
{
    UBSHcomNetRequestContext netCtx{};
    UBJetty *qp1 = (UBJetty *)malloc(sizeof(UBJetty));
    ctxInfo.ubJetty = qp1;
    ctxInfo.ubJetty->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    ctxInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    UBSHcomNetMessage msg;
    UBSHcomNetTransHeader header{};
    header.dataLength = 10;

    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));
    auto result = driver->NewReceivedRequestWithoutCopy(&ctxInfo, netCtx, msg, worker, nullptr, &header);
    EXPECT_EQ(result, NN_OK);
    free(qp1);
}

TEST_F(TestNetDriverUBWithOob, NewRequestOnEncryption)
{
    UBSHcomNetRequestContext netCtx{};
    UBSHcomNetMessage msg{};
    bool messageReady = true;
    ctxInfo.ubJetty->mUpContext1 = reinterpret_cast<uintptr_t>(worker);
    ctxInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(CallbackEp);
    UBSHcomNetTransHeader header{};
    ctxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&header);

    MOCKER_CPP(&UBWorker::RePostReceive).stubs().will(returnValue(0));
    MOCKER_CPP(&AesGcm128::GetRawLen).stubs().will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(driver->NewRequestOnEncryption(nullptr, msg, messageReady, netCtx), NN_INVALID_PARAM);
    CallbackEp->mIsNeedEncrypt = false;
    EXPECT_EQ(driver->NewRequestOnEncryption(&ctxInfo, msg, messageReady, netCtx), NN_INVALID_PARAM);
    CallbackEp->mIsNeedEncrypt = true;
    EXPECT_EQ(driver->NewRequestOnEncryption(&ctxInfo, msg, messageReady, netCtx), NN_DECRYPT_FAILED);
    EXPECT_EQ(driver->NewRequestOnEncryption(&ctxInfo, msg, messageReady, netCtx), NN_ERROR);
    EXPECT_EQ(driver->NewRequestOnEncryption(&ctxInfo, msg, messageReady, netCtx), NN_OK);
    if (msg.mBuf != nullptr) {
        free(msg.mBuf);
        msg.mBuf = nullptr;
    }
}

TEST_F(TestNetDriverUBWithOob, NetOobListenerOptionsSetEid)
{
    UBSHcomNetOobListenerOptions opt{};
    std::string eid = "00000000000000000000ffffc0a80164";
    EXPECT_EQ(opt.SetEid(eid, 0, 0), true);
}

TEST_F(TestNetDriverUBWithOob, OobEidAndJettyId)
{
    std::string eid = "0000:0000:0000:0000:0000:ffff:c0a8:0164";
    std::string ip = "1.2.3.4";

    MOCKER_CPP(HexStringToBuff).stubs().will(returnValue(false)).then(returnValue(true));
    driver->mStartOobSvr = true;
    EXPECT_NO_FATAL_FAILURE(driver->OobEidAndJettyId(ip, 0));
    EXPECT_NO_FATAL_FAILURE(driver->OobEidAndJettyId(eid, 0));
    EXPECT_NO_FATAL_FAILURE(driver->OobEidAndJettyId(eid, NN_NO64));
    EXPECT_NO_FATAL_FAILURE(driver->OobEidAndJettyId(eid, NN_NO64));

    driver->mStartOobSvr = false;
    EXPECT_NO_FATAL_FAILURE(driver->OobEidAndJettyId(eid, NN_NO64));
}

TEST_F(TestNetDriverUBWithOob, connectUrl)
{
    int ret;
    driver->mInited = false;
    std::string badUrl = "unknown://127.0.0.1";
    std::string serverUrl = "tcp://127.0.0.1:9981";
    std::string payload{};
    UBSHcomNetEndpointPtr outEp;

    ret = driver->Connect(badUrl, payload, outEp, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = driver->Connect(serverUrl, payload, outEp, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_ERROR);
}

}
}
#endif
