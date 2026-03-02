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
#include <unistd.h>
#include <utility>

#include "hcom_utils.h"
#include "net_common.h"
#include "ub_worker.h"


namespace ock {
namespace hcom {
UBOpContextInfo opCtxInfoPool{};
UBSglContextInfo sglOpCtxInfoPool{};

static UBOpContextInfo *MockOpCtxInfoPoolGet()
{
    return &opCtxInfoPool;
}

static UBSglContextInfo *MockSglOpCtxInfoPoolGet()
{
    return &sglOpCtxInfoPool;
}

class TestUbWorker : public testing::Test {
public:
    TestUbWorker();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string mName = "TestUbWorker";
    UBContext *ctx = nullptr;
    UBWorkerOptions options{};
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    NetMemPoolFixed *opCtxInfoPool = nullptr;
    NetMemPoolFixedOptions memOptions{};
    UBWorker *worker = nullptr;
    UBJetty *qp = nullptr;
};

TestUbWorker::TestUbWorker() {}

void TestUbWorker::SetUp()
{
    opCtxInfoPool = new (std::nothrow) NetMemPoolFixed(mName, memOptions);
    ASSERT_NE(opCtxInfoPool, nullptr);
    worker = new (std::nothrow) UBWorker(mName, ctx, options, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);
    worker->mInited = false;
    qp = new (std::nothrow) UBJetty(mName, 0, nullptr, nullptr);
    ASSERT_NE(qp, nullptr);

    MOCKER_CPP(&UBSglContextInfoPool::Get).stubs().will(invoke(MockSglOpCtxInfoPoolGet));
    MOCKER_CPP(&UBSglContextInfoPool::Return).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(invoke(MockOpCtxInfoPoolGet));
    MOCKER_CPP(&UBOpContextInfoPool::Return).stubs().will(ignoreReturnValue());
}

void TestUbWorker::TearDown()
{
    GlobalMockObject::verify();
    if (worker != nullptr) {
        delete worker;
        worker = nullptr;
    }
    if (opCtxInfoPool != nullptr) {
        delete opCtxInfoPool;
        opCtxInfoPool = nullptr;
    }
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
}

TEST_F(TestUbWorker, ToString)
{
    UBWorkerOptions option{};
    EXPECT_NO_FATAL_FAILURE(option.ToString());
}

TEST_F(TestUbWorker, IsWorkStarted)
{
    worker->mProgressThreadStarted = true;
    EXPECT_EQ(worker->IsWorkStarted(1), true);
}

TEST_F(TestUbWorker, SetIndex)
{
    UBSHcomNetWorkerIndex value{};
    worker->SetIndex(value);
    EXPECT_EQ(worker->mIndex.wholeIdx, 0);
}

TEST_F(TestUbWorker, ReturnOpContextInfo)
{
    UBOpContextInfo *ctx = nullptr;
    UBSglContextInfo *sglCtx = nullptr;
    EXPECT_NO_FATAL_FAILURE(worker->ReturnOpContextInfo(ctx));
    EXPECT_NO_FATAL_FAILURE(worker->ReturnSglContextInfo(sglCtx));
}

TEST_F(TestUbWorker, RegisterHandler)
{
    UBNewReqHandler ubNewReqHandler{};
    UBPostedHandler ubPostedHandler{};
    EXPECT_NO_FATAL_FAILURE(worker->RegisterNewRequestHandler(ubNewReqHandler));
    EXPECT_NO_FATAL_FAILURE(worker->RegisterPostedHandler(ubPostedHandler));
    worker->mNewRequestHandler = nullptr;
    worker->mSendPostedHandler = nullptr;
}

TEST_F(TestUbWorker, RegisterOneSideAndIdleHandler)
{
    UBOneSideDoneHandler ubOneSideDoneHandler{};
    UBIdleHandler ubIdleHandler{};
    EXPECT_NO_FATAL_FAILURE(worker->RegisterOneSideDoneHandler(ubOneSideDoneHandler));
    EXPECT_NO_FATAL_FAILURE(worker->RegisterIdleHandler(ubIdleHandler));
    worker->mOneSideDoneHandler = nullptr;
    worker->mIdleHandler = nullptr;
}

TEST_F(TestUbWorker, DetailName)
{
    EXPECT_NO_FATAL_FAILURE(worker->DetailName());
}

TEST_F(TestUbWorker, PortNum)
{
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mPortNumber = 1;
    worker->mUBContext = ubCtx;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    EXPECT_EQ(worker->PortNum(), 1);
    delete ubCtx;
    worker->mUBContext = nullptr;
}

TEST_F(TestUbWorker, WorkerTypeToString)
{
    std::string send("sender");
    std::string unknown("unknown worker type");
    EXPECT_EQ(WorkerTypeToString(UBWorkerType::UB_SENDER), send);
    EXPECT_EQ(WorkerTypeToString(static_cast<UBWorkerType>(NN_NO4)), unknown);
}

TEST_F(TestUbWorker, PollingModeToString)
{
    std::string busy("busy_polling");
    std::string unknown("unknown worker mode");
    EXPECT_EQ(PollingModeToString(UBPollingMode::UB_BUSY_POLLING), busy);
    EXPECT_EQ(PollingModeToString(static_cast<UBPollingMode>(NN_NO2)), unknown);
}

TEST_F(TestUbWorker, Initialize)
{
    worker->mInited = true;
    EXPECT_EQ(worker->Initialize(), UB_OK);

    worker->mInited = false;
    EXPECT_EQ(worker->Initialize(), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, InitializeSuccess)
{
    urma_context_t UrmaContext{};
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mUrmaContext = &UrmaContext;
    worker->mUBContext = ubCtx;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBOpContextInfoPool::Initialize, NResult(UBOpContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&UBSglContextInfoPool::Initialize, NResult(UBSglContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(0));
    EXPECT_EQ(worker->Initialize(), 0);

    ubCtx->mUrmaContext = nullptr;
    worker->mUBContext = nullptr;
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    worker->mUBJfc = nullptr;
}

TEST_F(TestUbWorker, InitializeUBJfcErr)
{
    urma_context_t UrmaContext{};
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mUrmaContext = &UrmaContext;
    worker->mUBContext = ubCtx;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(1));
    MOCKER_CPP(&UBJfc::UnInitialize).stubs().will(returnValue(0));

    EXPECT_EQ(worker->Initialize(), 1);

    ubCtx->mUrmaContext = nullptr;
    worker->mUBContext = nullptr;
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    worker->mUBJfc = nullptr;
    delete ubCtx;
    ubCtx = nullptr;
}

TEST_F(TestUbWorker, InitializeSglCtxInfoPoolErr)
{
    urma_context_t UrmaContext{};
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mUrmaContext = &UrmaContext;
    worker->mUBContext = ubCtx;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJfc::UnInitialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBOpContextInfoPool::Initialize, NResult(UBOpContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&UBSglContextInfoPool::Initialize, NResult(UBSglContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(1));

    EXPECT_EQ(worker->Initialize(), 1);

    ubCtx->mUrmaContext = nullptr;
    worker->mUBContext = nullptr;
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    delete ubCtx;
    ubCtx = nullptr;
}

TEST_F(TestUbWorker, InitializeOpCtxInfoPoolErr)
{
    urma_context_t UrmaContext{};
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mUrmaContext = &UrmaContext;
    worker->mUBContext = ubCtx;
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJfc::UnInitialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBOpContextInfoPool::Initialize, NResult(UBOpContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(1));

    EXPECT_EQ(worker->Initialize(), 1);

    ubCtx->mUrmaContext = nullptr;
    worker->mUBContext = nullptr;
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    delete ubCtx;
    ubCtx = nullptr;
}

TEST_F(TestUbWorker, InitializeJettyPtrMapErr)
{
    urma_context_t UrmaContext{};
    UBEId eid{};
    UBContext *ubCtx = new UBContext("ubTest");
    ubCtx->mUrmaContext = &UrmaContext;

    worker->mUBContext = ubCtx;
    ubCtx->IncreaseRef();

    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&HcomUrma::DeleteContext).stubs().will(returnValue(URMA_SUCCESS));
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBOpContextInfoPool::Initialize, NResult(UBOpContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&UBSglContextInfoPool::Initialize, NResult(UBSglContextInfoPool::*)(const NetMemPoolFixedPtr &))
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&mmap).stubs().will(returnValue(MAP_FAILED));
    EXPECT_EQ(worker->Initialize(), UB_MEMORY_ALLOCATE_FAILED);

    worker->mUBContext = nullptr;
    ubCtx->mUrmaContext = nullptr;
    delete ubCtx;
}

TEST_F(TestUbWorker, UnInitialize)
{
    EXPECT_EQ(worker->UnInitialize(), UB_OK);

    worker->mInited = true;
    MOCKER_CPP(&UBOpContextInfoPool::UnInitialize).stubs().will(returnValue(0));
    EXPECT_EQ(worker->UnInitialize(), UB_OK);
}

TEST_F(TestUbWorker, ReInitializeCQ)
{
    EXPECT_EQ(worker->ReInitializeCQ(), UB_OK);

    worker->mInited = true;
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(0));
    EXPECT_EQ(worker->ReInitializeCQ(), UB_OK);
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    worker->mUBJfc = nullptr;
}

TEST_F(TestUbWorker, ReInitializeCQErr)
{
    worker->mInited = true;
    MOCKER_CPP(&UBJfc::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(worker->ReInitializeCQ(), 1);
    if (worker->mUBJfc != nullptr) {
        delete worker->mUBJfc;
        worker->mUBJfc = nullptr;
    }
    worker->mUBJfc = nullptr;
}

TEST_F(TestUbWorker, Start)
{
    EXPECT_EQ(worker->Start(), UB_WORKER_NOT_INITIALIZED);

    worker->mInited = true;
    worker->mOptions.dontStartWorkers = true;
    EXPECT_EQ(worker->Start(), UB_OK);
    worker->mOptions.dontStartWorkers = false;
}

TEST_F(TestUbWorker, StartTypeErr)
{
    worker->mInited = true;
    worker->mOptions.workerType = UB_RECEIVER;
    EXPECT_EQ(worker->Start(), UB_WORKER_REQUEST_HANDLER_NOT_SET);

    worker->mOptions.workerType = UB_SENDER;
    worker->mOptions.dontStartWorkers = false;
    EXPECT_EQ(worker->Start(), UB_WORKER_SEND_POSTED_HANDLER_NOT_SET);
}

TEST_F(TestUbWorker, Stop)
{
    EXPECT_EQ(worker->Stop(), UB_OK);
}

TEST_F(TestUbWorker, RunInThreadErr)
{
    worker->mOptions.threadPriority = 1;
    worker->mOptions.workerMode = static_cast<UBPollingMode>(NN_NO2);
    EXPECT_NO_FATAL_FAILURE(worker->RunInThread());
}

TEST_F(TestUbWorker, CreateCtxNullErr)
{
    UBWorker *outWorker = nullptr;
    EXPECT_EQ(worker->Create(mName, nullptr, options, memPool, sglMemPool, outWorker), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, PostReceiveParamErr)
{
    EXPECT_EQ(worker->PostReceive(nullptr, 0, 0, nullptr), UB_PARAM_INVALID);

    MOCKER_CPP(&UBJetty::PostReceive).stubs().will(returnValue(1));
    qp->IncreaseRef();
    EXPECT_EQ(worker->PostReceive(qp, 0, 0, nullptr), 1);
}

TEST_F(TestUbWorker, PostReceiveCtxFull)
{
    GlobalMockObject::verify();
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostReceive(qp, 0, 0, nullptr), UB_QP_CTX_FULL);
}

TEST_F(TestUbWorker, RePostReceive)
{
    EXPECT_EQ(worker->RePostReceive(nullptr), UB_PARAM_INVALID);

    UBOpContextInfo ctx{};
    ctx.ubJetty = qp;
    MOCKER_CPP(&UBJetty::PostReceive).stubs().will(returnValue(static_cast<UResult>(UB_NEW_OBJECT_FAILED)));
    qp->IncreaseRef();
    qp->IncreaseRef();
    EXPECT_EQ(worker->RePostReceive(&ctx), UB_NEW_OBJECT_FAILED);
}

TEST_F(TestUbWorker, PostSendParamErr)
{
    UBSendReadWriteRequest req{};
    EXPECT_EQ(worker->PostSend(nullptr, req, nullptr, 0), UB_PARAM_INVALID);

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(false));
    EXPECT_EQ(worker->PostSend(qp, req, nullptr, 0), UB_QP_POST_SEND_WR_FULL);
}

TEST_F(TestUbWorker, PostSendCtxFull)
{
    GlobalMockObject::verify();
    UBSendReadWriteRequest req{};
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostSend(qp, req, nullptr, 0), UB_QP_CTX_FULL);
}

TEST_F(TestUbWorker, PostSendMemcpyFail)
{
    UBSendReadWriteRequest req{};
    req.upCtxSize = 1;
    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    EXPECT_EQ(worker->PostSend(qp, req, nullptr, 0), UB_ERROR);
}

TEST_F(TestUbWorker, PostSend)
{
    UBSendReadWriteRequest req{};

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::PostSend).stubs().will(returnValue(1));

    qp->IncreaseRef();
    EXPECT_EQ(worker->PostSend(qp, req, nullptr, 0), 1);
}

TEST_F(TestUbWorker, PostSendSglInlineParamErr)
{
    UBSendSglInlineHeader header{};
    UBSendReadWriteRequest req{};
    EXPECT_EQ(worker->PostSendSglInline(nullptr, header, req, 0), 200);
}

TEST_F(TestUbWorker, PostSendSglInlineCtxNull)
{
    GlobalMockObject::verify();
    UBSendSglInlineHeader header{};
    UBSendReadWriteRequest req{};
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostSendSglInline(qp, header, req, 0), UB_QP_CTX_FULL);
}

TEST_F(TestUbWorker, PostSendSglInlineWrFull)
{
    UBSendSglInlineHeader header{};
    UBSendReadWriteRequest req{};

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(false));

    EXPECT_EQ(worker->PostSendSglInline(qp, header, req, 0), UB_QP_POST_SEND_WR_FULL);
}

TEST_F(TestUbWorker, PostSendSglInlineSuccess)
{
    UBSendSglInlineHeader header{};
    UBSendReadWriteRequest req{};

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(true));

    MOCKER_CPP(&UBJetty::PostSendSglInline).stubs().will(returnValue(0));

    EXPECT_EQ(worker->PostSendSglInline(qp, header, req, 0), 0);
}

TEST_F(TestUbWorker, PostSendSglParamErr)
{
    UBSHcomNetTransSglRequest req{};
    req.upCtxSize = 1;
    UBSHcomNetTransRequest tlsReq{};
    EXPECT_EQ(worker->PostSendSgl(nullptr, req, tlsReq, 0, false), UB_PARAM_INVALID);

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(false));
    EXPECT_EQ(worker->PostSendSgl(qp, req, tlsReq, 0, false), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, PostSendSglCtxFull)
{
    GlobalMockObject::verify();
    UBSHcomNetTransSglRequest req{};
    UBSHcomNetTransRequest tlsReq{};
    UBSglContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBSglContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostSendSgl(qp, req, tlsReq, 0, false), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, PostSendSgl)
{
    UBSHcomNetTransSglRequest req{};
    UBSHcomNetTransSgeIov iov{};
    req.iov = &iov;
    req.iovCount = 1;
    req.upCtxSize = 1;
    UBSHcomNetTransRequest tlsReq{};

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::PostSendSgl).stubs().will(returnValue(1));
    qp->IncreaseRef();
    EXPECT_EQ(worker->PostSendSgl(qp, req, tlsReq, 0, false), 1);
}

TEST_F(TestUbWorker, PostReadParamErr)
{
    UBSendReadWriteRequest req{};
    EXPECT_EQ(worker->PostRead(nullptr, req), UB_PARAM_INVALID);

    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(false));
    EXPECT_EQ(worker->PostRead(qp, req), UB_QP_ONE_SIDE_WR_FULL);
}

TEST_F(TestUbWorker, PostReadMemcpyFail)
{
    UBSendReadWriteRequest req{};
    req.upCtxSize = 1;
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetProtocol).stubs().will(returnValue(UBSHcomNetDriverProtocol::UBC));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    EXPECT_EQ(worker->PostRead(qp, req), UB_ERROR);
}

TEST_F(TestUbWorker, PostRead)
{
    UBSendReadWriteRequest req{};
    req.upCtxSize = 1;
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetProtocol).stubs().will(returnValue(UBSHcomNetDriverProtocol::UBC));
    MOCKER_CPP(&UBJetty::PostRead, UResult(UBJetty::*)(uintptr_t, urma_target_seg_t *,
        uintptr_t, uint64_t, uint32_t, uint64_t))
        .stubs()
        .will(returnValue(1));
    qp->IncreaseRef();
    EXPECT_EQ(worker->PostRead(qp, req), 1);
}

TEST_F(TestUbWorker, PostReadCtxFull)
{
    GlobalMockObject::verify();
    UBSendReadWriteRequest req{};
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostRead(qp, req), UB_QP_CTX_FULL);
}

TEST_F(TestUbWorker, PostWriteParamErr)
{
    UBSendReadWriteRequest req{};
    EXPECT_EQ(worker->PostWrite(nullptr, req), UB_PARAM_INVALID);

    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(false));
    EXPECT_EQ(worker->PostWrite(qp, req), UB_QP_ONE_SIDE_WR_FULL);
}

TEST_F(TestUbWorker, PostWriteCtxFull)
{
    GlobalMockObject::verify();
    UBSendReadWriteRequest req{};
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));
    EXPECT_EQ(worker->PostWrite(qp, req), UB_QP_CTX_FULL);
}

TEST_F(TestUbWorker, PostWriteMemcpyFail)
{
    UBSendReadWriteRequest req{};
    req.upCtxSize = 1;
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetProtocol).stubs().will(returnValue(UBSHcomNetDriverProtocol::UBC));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(1));

    EXPECT_EQ(worker->PostWrite(qp, req), UB_ERROR);
}

TEST_F(TestUbWorker, PostWrite)
{
    UBSendReadWriteRequest req{};
    req.upCtxSize = 1;
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(true));
    MOCKER_CPP(&UBJetty::GetProtocol).stubs().will(returnValue(UBSHcomNetDriverProtocol::UBC));
    MOCKER_CPP(&UBJetty::PostWrite, UResult(UBJetty::*)(uintptr_t, urma_target_seg_t *,
        uintptr_t, uint64_t, uint32_t, uint64_t))
        .stubs()
        .will(returnValue(1));
    qp->IncreaseRef();
    EXPECT_EQ(worker->PostWrite(qp, req), 1);
}

TEST_F(TestUbWorker, CreateOneSideCtxParamErr)
{
    UBSgeCtxInfo sgeInfo{};
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    EXPECT_EQ(worker->CreateOneSideCtx(sgeInfo, nullptr, 0, ctxArr, true), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, CreateOneSideCtxCtxFull)
{
    GlobalMockObject::verify();
    UBSgeCtxInfo sgeInfo{};
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    UBSHcomNetTransSgeIov *iov = nullptr;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    UBOpContextInfo *testPool = nullptr;
    MOCKER_CPP(&UBOpContextInfoPool::Get).stubs().will(returnValue(testPool));

    EXPECT_EQ(worker->CreateOneSideCtx(sgeInfo, iov, 1, ctxArr, true), UB_QP_CTX_FULL);

    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

TEST_F(TestUbWorker, CreateOneSideCtxOneSideWrFull)
{
    UBSgeCtxInfo sgeInfo{};
    sgeInfo.ctx = MockSglOpCtxInfoPoolGet();
    sgeInfo.ctx->qp = qp;
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    UBSHcomNetTransSgeIov *iov = nullptr;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(false));

    EXPECT_EQ(worker->CreateOneSideCtx(sgeInfo, iov, 1, ctxArr, true), UB_QP_ONE_SIDE_WR_FULL);

    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

TEST_F(TestUbWorker, CreateOneSideCtx)
{
    UBSgeCtxInfo sgeInfo{};
    sgeInfo.ctx = MockSglOpCtxInfoPoolGet();
    sgeInfo.ctx->qp = qp;
    uint64_t ctxArr[NET_SGE_MAX_IOV];
    UBSHcomNetTransSgeIov *iov = nullptr;
    iov = new (std::nothrow) UBSHcomNetTransSgeIov();
    MOCKER_CPP(&UBJetty::GetOneSideWr).stubs().will(returnValue(true));

    EXPECT_EQ(worker->CreateOneSideCtx(sgeInfo, iov, 1, ctxArr, true), UB_OK);

    if (iov != nullptr) {
        delete iov;
        iov = nullptr;
    }
}

TEST_F(TestUbWorker, PostOneSideSglParamErr)
{
    UBSendSglRWRequest rwReq{};
    rwReq.upCtxSize = 1;
    EXPECT_EQ(worker->PostOneSideSgl(nullptr, rwReq, false), UB_PARAM_INVALID);

    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0)).then(returnValue(1));
    EXPECT_EQ(worker->PostOneSideSgl(qp, rwReq, false), UB_PARAM_INVALID);
    EXPECT_EQ(worker->PostOneSideSgl(qp, rwReq, false), UB_PARAM_INVALID);
}

TEST_F(TestUbWorker, PostOneSideSgl)
{
    UBSendSglRWRequest rwReq{};
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&UBWorker::CreateOneSideCtx).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBJetty::GetProtocol)
        .stubs()
        .will(returnValue(UBSHcomNetDriverProtocol::UBC));
    MOCKER_CPP(&UBJetty::PostOneSideSgl).stubs().will(returnValue(0));

    EXPECT_EQ(worker->PostOneSideSgl(qp, rwReq, false), NN_NO1);
    EXPECT_EQ(worker->PostOneSideSgl(qp, rwReq, false), UB_OK);
}

TEST_F(TestUbWorker, CreateQPErr)
{
    UBJetty *tmpQp = nullptr;
    worker->mInited = false;
    EXPECT_EQ(worker->CreateQP(tmpQp), UB_WORKER_NOT_INITIALIZED);
}

TEST_F(TestUbWorker, CreateQP)
{
    UBJetty *tmpQp = nullptr;
    worker->mInited = true;
    EXPECT_EQ(worker->CreateQP(tmpQp), UB_OK);
    delete tmpQp;
}

TEST_F(TestUbWorker, TestPostSendSgl)
{
    UBSHcomNetTransSglRequest req{};
    req.upCtxSize = 0;
    UBSHcomNetTransRequest tlsReq{};

    MOCKER_CPP(&UBJetty::GetPostSendWr).stubs().will(returnValue(true));
    MOCKER_CPP(&memcpy_s).stubs().will(returnValue(0));
    MOCKER_CPP(&UBJetty::PostSend).stubs().will(returnValue(0));
    EXPECT_EQ(worker->PostSendSgl(qp, req, tlsReq, 0, true), 0);
}

void MockRemoveOpCtxInfo(UBOpContextInfo *ctxInfo)
{
    return;
}

void MockUpdateTargetHbTime()
{
    return;
}

void TestUbEndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point broken");
}

int TestUbRequestReceived(const UBOpContextInfo *ctx)
{
    return 0;
}

int TestUbRequestPosted(const UBOpContextInfo *ctx)
{
    return 0;
}

int TestUbOneSideDone(const UBOpContextInfo *ctx)
{
    return 0;
}

TEST_F(TestUbWorker, TestProcessPollingResult)
{
    MOCKER_CPP(&UBJetty::RemoveOpCtxInfo).stubs().will(invoke(MockRemoveOpCtxInfo));
    MOCKER_CPP(&UBJetty::Cleanup).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetUBAsyncEndpoint::UpdateTargetHbTime).stubs().will(invoke(MockUpdateTargetHbTime));
    MOCKER_CPP(&NetDriverUBWithOob::ProcessEpError).stubs();

    worker->RegisterPostedHandler(std::bind(&TestUbRequestReceived, std::placeholders::_1));
    worker->RegisterNewRequestHandler(std::bind(&TestUbRequestPosted, std::placeholders::_1));
    worker->RegisterOneSideDoneHandler(std::bind(&TestUbOneSideDone, std::placeholders::_1));
    worker->mJettyPtrMap.Initialize();

    uint32_t pollCount = NN_NO1;
    UBJetty *lastBrokenQp = nullptr;
    urma_cr_status_t lastErrorWcStatus = URMA_CR_SUCCESS;
    auto *wc = static_cast<urma_cr_t *>(calloc(NN_NO1, sizeof(urma_cr_t)));
    auto *jetty = new (std::nothrow) UBJetty("testUbjetty", NN_NO0, nullptr, nullptr);
    auto *driver = new (std::nothrow) NetDriverUBWithOob("testDriver", 0, UBSHcomNetDriverProtocol::UBC);
    auto *ep = new (std::nothrow) NetUBAsyncEndpoint(0, nullptr, driver, nullptr);
    ep->IncreaseRef(); // 存在会被 NetEndpointPtr 持有的情况
    ASSERT_NE(wc, nullptr);
    ASSERT_NE(jetty, nullptr);
    ASSERT_NE(driver, nullptr);
    ASSERT_NE(ep, nullptr);

    UBOpContextInfo contextInfo{};
    contextInfo.ubJetty = jetty;
    contextInfo.ubJetty->mUpContext = reinterpret_cast<uintptr_t>(ep);

    // 一开始正常发送
    contextInfo.ubJetty->mState = UBJettyState::READY;
    contextInfo.opType = UBOpContextInfo::OpType::SEND;
    wc[0].status = URMA_CR_SUCCESS;
    wc[0].user_ctx = reinterpret_cast<uint64_t>(&contextInfo);
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);

    contextInfo.opType = UBOpContextInfo::OpType::RECEIVE;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);

    contextInfo.opType = UBOpContextInfo::OpType::SGL_READ;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);

    // 遇到不同的连续错误
    wc[0].status = URMA_CR_REM_ACCESS_ABORT_ERR;
    contextInfo.opType = UBOpContextInfo::OpType::SEND;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);
    EXPECT_EQ(lastBrokenQp, jetty);

    wc[0].status = URMA_CR_ACK_TIMEOUT_ERR;
    contextInfo.opType = UBOpContextInfo::OpType::SEND;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);
    EXPECT_EQ(lastBrokenQp, jetty);
    EXPECT_EQ(lastErrorWcStatus, URMA_CR_ACK_TIMEOUT_ERR);

    free(wc);
    delete jetty;
    ep->mJetty = nullptr;
    delete ep;
}

TEST_F(TestUbWorker, TestProcessPollingResultTwo)
{
    MOCKER_CPP(&UBJetty::RemoveOpCtxInfo).stubs().will(invoke(MockRemoveOpCtxInfo));
    MOCKER_CPP(&UBJetty::Cleanup).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetUBAsyncEndpoint::UpdateTargetHbTime).stubs().will(invoke(MockUpdateTargetHbTime));
    MOCKER_CPP(&NetDriverUBWithOob::ProcessEpError).stubs();

    worker->RegisterPostedHandler(std::bind(&TestUbRequestReceived, std::placeholders::_1));
    worker->RegisterNewRequestHandler(std::bind(&TestUbRequestPosted, std::placeholders::_1));
    worker->RegisterOneSideDoneHandler(std::bind(&TestUbOneSideDone, std::placeholders::_1));
    worker->mJettyPtrMap.Initialize();

    uint32_t pollCount = NN_NO1;
    UBJetty *lastBrokenQp = nullptr;
    urma_cr_status_t lastErrorWcStatus = URMA_CR_SUCCESS;
    auto *wc = static_cast<urma_cr_t *>(calloc(NN_NO1, sizeof(urma_cr_t)));
    auto *jetty = new (std::nothrow) UBJetty("testUbjetty", NN_NO0, nullptr, nullptr);
    auto *driver = new (std::nothrow) NetDriverUBWithOob("testDriver", 0, UBSHcomNetDriverProtocol::UBC);
    auto *ep = new (std::nothrow) NetUBAsyncEndpoint(0, nullptr, driver, nullptr);
    ep->IncreaseRef(); // 存在会被 NetEndpointPtr 持有的情况
    ASSERT_NE(wc, nullptr);
    ASSERT_NE(jetty, nullptr);
    ASSERT_NE(driver, nullptr);
    ASSERT_NE(ep, nullptr);

    UBOpContextInfo contextInfo{};
    contextInfo.ubJetty = jetty;
    // status 9 必定会导致jetty 为 error 状态
    contextInfo.ubJetty->mState = UBJettyState::ERROR;
    worker->mJettyPtrMap.Emplace(0, jetty);
    wc[0].user_ctx = reinterpret_cast<uint64_t>(&contextInfo);
    wc[0].status = URMA_CR_ACK_TIMEOUT_ERR;
    contextInfo.opType = UBOpContextInfo::OpType::SEND;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);

    // 在 modify jetty error过程中可能会收到 FLUSH_ERR
    wc[0].status = URMA_CR_WR_FLUSH_ERR;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);

    // 最终以 FLUSH_ERR_DONE 结尾
    wc[0].status = URMA_CR_WR_FLUSH_ERR_DONE;
    worker->ProcessPollingResult(wc, pollCount, lastBrokenQp, lastErrorWcStatus);
    EXPECT_EQ(worker->mJettyPtrMap.Lookup(0), nullptr);

    free(wc);
    delete jetty;
    ep->mJetty = nullptr;
    delete ep;
}
}  // namespace hcom
}  // namespace ock
#endif
