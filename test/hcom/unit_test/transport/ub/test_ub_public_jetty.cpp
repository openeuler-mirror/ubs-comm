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
#include <sys/poll.h>
#include <fcntl.h>

#include "ub_urma_wrapper_public_jetty.h"
#include "ub_mr_fixed_buf.h"
#include "ub_fixed_mem_pool.h"
namespace ock {
namespace hcom {

class TestUBPublicJetty : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    std::string name = "test-public-jetty";
    UBEId eid{};
    UBPublicJetty *jetty = nullptr;
    UBContext *ctx = nullptr;
    UBJfc *jfc = nullptr;
};

void TestUBPublicJetty::SetUp()
{
    ctx = new (std::nothrow) UBContext(name);
    ASSERT_NE(ctx, nullptr);
    jfc = new (std::nothrow) UBJfc(name, ctx);
    ASSERT_NE(jfc, nullptr);
    jetty = new (std::nothrow) UBPublicJetty(name, 0, ctx, jfc);
    ASSERT_NE(jetty, nullptr);
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(returnValue(0));
}

void TestUBPublicJetty::TearDown()
{
    if (jetty != nullptr) {
        delete jetty;
        jetty = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestUBPublicJetty, ImportPublicJetty)
{
    urma_target_jetty_t targetJetty{};
    urma_eid_t remoteEid{};
    urma_jetty_t tmpJetty{};
    urma_target_jetty_t *out = nullptr;
    jetty->mUrmaJetty = &tmpJetty;
    MOCKER_CPP(HcomUrma::ImportJetty).stubs().will(returnValue(out)).then(returnValue(&targetJetty));
    EXPECT_EQ(jetty->ImportPublicJetty(remoteEid, 0), UB_QP_IMPORT_FAILED);
    EXPECT_EQ(jetty->ImportPublicJetty(remoteEid, 0), UB_OK);
    jetty->mUrmaJetty = nullptr;
    jetty->mTargetJetty = nullptr;
}

TEST_F(TestUBPublicJetty, FillJfsCfg)
{
    urma_jfs_cfg_t jfs_cfg{};
    EXPECT_NO_FATAL_FAILURE(jetty->FillJfsCfg(&jfs_cfg));
}

TEST_F(TestUBPublicJetty, FillJfrCfg)
{
    urma_jfr_cfg_t jfr_cfg{};
    EXPECT_NO_FATAL_FAILURE(jetty->FillJfrCfg(&jfr_cfg));
}

TEST_F(TestUBPublicJetty, CreateUrmaPublicJetty)
{
    urma_jfc_t mUrmaJfc{};
    urma_context_t urmaContext{};
    EXPECT_EQ(jetty->CreateUrmaPublicJetty(0), UB_PARAM_INVALID);

    jfc->mUrmaJfc = &mUrmaJfc;
    ctx->mUrmaContext = &urmaContext;
    urma_jfr_t *outJfr = nullptr;
    urma_jfr_t outJfr2{};
    MOCKER_CPP(HcomUrma::CreateJfr).stubs().will(returnValue(outJfr)).then(returnValue(&outJfr2));
    urma_jetty_t *outJetty = nullptr;
    urma_jetty_t outJetty2{};
    urma_status_t res = 0;
    MOCKER_CPP(HcomUrma::CreateJetty).stubs().will(returnValue(outJetty)).then(returnValue(&outJetty2));
    MOCKER_CPP(HcomUrma::DeleteJfr).stubs().will(returnValue(res));
    EXPECT_EQ(jetty->CreateUrmaPublicJetty(0), UB_PARAM_INVALID);
    EXPECT_EQ(jetty->CreateUrmaPublicJetty(0), UB_QP_CREATE_FAILED);
    EXPECT_EQ(jetty->CreateUrmaPublicJetty(0), UB_OK);

    jfc->mUrmaJfc = nullptr;
    ctx->mUrmaContext = nullptr;
    jetty->mUrmaJetty = nullptr;
}

TEST_F(TestUBPublicJetty, CreateJettyMr)
{
    urma_target_seg_t tmpMr{};
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(&tmpMr));
    MOCKER(HcomUrma::UnregisterSeg).stubs().will(returnValue(0));
    EXPECT_EQ(jetty->CreateJettyMr(), UB_OK);
}

TEST_F(TestUBPublicJetty, InitializePublicJetty)
{
    MOCKER_CPP(&UBPublicJetty::CreateJettyMr).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::CreateCtxInfoPool).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::CreateUrmaPublicJetty).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(jetty->InitializePublicJetty(0), 1);
    EXPECT_EQ(jetty->InitializePublicJetty(0), 1);
    EXPECT_EQ(jetty->InitializePublicJetty(0), 1);
    EXPECT_EQ(jetty->InitializePublicJetty(0), 0);
}

TEST_F(TestUBPublicJetty, StartPublicJettyFail)
{
    urma_target_seg_t tmpMr{};
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(&tmpMr));
    MOCKER(HcomUrma::UnregisterSeg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::CreateUrmaPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBufferN).stubs().will(returnValue(false));
    jetty->InitializePublicJetty(0);
    EXPECT_EQ(jetty->StartPublicJetty(), UB_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestUBPublicJetty, StartPublicJetty)
{
    urma_target_seg_t tmpMr{};
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(&tmpMr));
    MOCKER(HcomUrma::UnregisterSeg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::CreateUrmaPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PostReceive).stubs().will(returnValue(1)).then(returnValue(0));
    jetty->InitializePublicJetty(0);
    EXPECT_EQ(jetty->StartPublicJetty(), UB_ERROR);
    EXPECT_EQ(jetty->StartPublicJetty(), UB_OK);
}

NResult MockNewRequestHandler(UBOpContextInfo *ctx)
{
    return NN_OK;
}

TEST_F(TestUBPublicJetty, NewRequest)
{
    UBOpContextInfo ctx{};
    JettyConnHeader header{};
    urma_target_seg_t *tseg = nullptr;

    MOCKER_CPP(&UBPublicJetty::GetMemorySeg).stubs().will(returnValue(tseg));
    MOCKER_CPP(&UBFixedMemPool::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBPublicJetty::PostReceive).stubs().will(returnValue(1)).then(returnValue(0));
    ctx.mrMemAddr = reinterpret_cast<uintptr_t>(&header);
    jetty->SetNewConnCB(MockNewRequestHandler);
    EXPECT_EQ(jetty->NewRequest(nullptr), NN_ERROR);

    header.msgType = UrmaConnectMsgType::CONNECT_REQ;
    EXPECT_EQ(jetty->NewRequest(&ctx), UB_QP_POST_RECEIVE_FAILED);

    header.msgType = UrmaConnectMsgType::EXCHANGE_MSG;
    EXPECT_EQ(jetty->NewRequest(&ctx), UB_OK);
}

TEST_F(TestUBPublicJetty, SendFinished)
{
    UBOpContextInfo ctx{};

    MOCKER_CPP(&UBPublicJetty::ReturnBuffer).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER_CPP(&UBFixedMemPool::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(jetty->SendFinished(&ctx), UB_OK);
    EXPECT_EQ(jetty->SendFinished(&ctx), UB_OK);
}

TEST_F(TestUBPublicJetty, RunInThread)
{
    jetty->mNeedStop = true;
    EXPECT_NO_FATAL_FAILURE(jetty->RunInThread());
}

TEST_F(TestUBPublicJetty, SendByPublicJettyFail)
{
    urma_jetty_t tmpJetty{};
    uint8_t data;
    urma_target_jetty_t targetJetty{};
    jetty->mTargetJetty = &targetJetty;
    urma_target_seg_t tmpMr{};
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(&tmpMr));
    MOCKER(HcomUrma::UnregisterSeg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::CreateUrmaPublicJetty).stubs().will(returnValue(0));
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::GetFreeBuffer).stubs().will(returnValue(false));
    EXPECT_EQ(jetty->SendByPublicJetty(&data, 1), UB_QP_NOT_INITIALIZED);

    jetty->mUrmaJetty = &tmpJetty;
    jetty->InitializePublicJetty(0);
    EXPECT_EQ(jetty->SendByPublicJetty(&data, 1), UB_MEMORY_ALLOCATE_FAILED);
    jetty->mUrmaJetty = nullptr;
    jetty->mTargetJetty = nullptr;
}

TEST_F(TestUBPublicJetty, SendByPublicJetty)
{
    urma_jetty_t tmpJetty{};
    urma_target_jetty_t targetJetty{};
    jetty->mTargetJetty = &targetJetty;
    uint8_t data;
    urma_target_seg_t *tseg = nullptr;
    urma_target_seg_t tmpMr{};
    MOCKER(HcomUrma::RegisterSeg).stubs().will(returnValue(&tmpMr));
    MOCKER(HcomUrma::UnregisterSeg).stubs().will(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::GetMemorySeg).stubs().will(returnValue(tseg));
    MOCKER_CPP(&UBPublicJetty::CreateUrmaPublicJetty).stubs().will(returnValue(0));
    MOCKER(HcomUrma::UnimportJetty).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::PostJettySendWr, urma_status_t(urma_jetty_t *, urma_jfs_wr_t *, urma_jfs_wr_t **))
        .stubs().will(returnValue(1)).then(returnValue(0));
    jetty->mUrmaJetty = &tmpJetty;
    jetty->InitializePublicJetty(0);

    EXPECT_EQ(jetty->SendByPublicJetty(&data, 1), UB_QP_POST_SEND_FAILED);
    EXPECT_EQ(jetty->SendByPublicJetty(&data, 1), UB_OK);
    jetty->mUrmaJetty = nullptr;
}

TEST_F(TestUBPublicJetty, PostReceive)
{
    urma_target_seg_t localSeg{};
    urma_jetty_t tmpJetty{};
    uint8_t data;
    urma_target_seg_t *tseg = nullptr;

    MOCKER_CPP(&UBPublicJetty::GetMemorySeg).stubs().will(returnValue(tseg));
    MOCKER_CPP(HcomUrma::PostJettyRecvWr).stubs().will(returnValue(1)).then(returnValue(0));

    jetty->mUrmaJetty = &tmpJetty;
    EXPECT_EQ(jetty->PostReceive(reinterpret_cast<uintptr_t>(&data), 0, &localSeg, 0), NN_INVALID_PARAM);
    EXPECT_EQ(jetty->PostReceive(reinterpret_cast<uintptr_t>(&data), 1, &localSeg, 0), UB_QP_POST_RECEIVE_FAILED);
    EXPECT_EQ(jetty->PostReceive(reinterpret_cast<uintptr_t>(&data), 1, &localSeg, 0), UB_OK);
    jetty->mUrmaJetty = nullptr;
}

UBOpContextInfo tmpCtxInfo{};
UResult MockEventProgressV(urma_cr_t *cr, uint32_t &countInOut, int32_t timeoutInMs = NN_NO500)
{
    switch (timeoutInMs) {
        case NN_NO1000:
            countInOut = 0;
            break;
        case NN_NO2000:
            countInOut = 1;
            cr->status = URMA_CR_LOC_LEN_ERR;
            break;
        case NN_NO10000:
            countInOut = 1;
            cr->status = URMA_CR_SUCCESS;
            cr->user_ctx = 0;
            break;
        case (-1):
            countInOut = 1;
            cr->status = URMA_CR_SUCCESS;
            cr->user_ctx = reinterpret_cast<uintptr_t>(&tmpCtxInfo);
            break;
        default:
            break;
    }
    return UB_OK;
}

TEST_F(TestUBPublicJetty, PollingCompletion)
{
    urma_jfc_t mUrmaJfc{};
    urma_jetty_t mUrmaJetty{};
    uint32_t pollCount = 0;

    MOCKER_CPP(&UBJfc::ProgressV).stubs().with(any(), outBound(pollCount))
        .will(returnValue(1));
    MOCKER_CPP(&UBPublicJetty::ReturnBuffer).stubs().will(returnValue(false));
    MOCKER_CPP(&UBFixedMemPool::ReturnBuffer).stubs().will(returnValue(true));
    EXPECT_EQ(jetty->PollingCompletion(), UB_EP_NOT_INITIALIZED);

    jetty->mUrmaJetty = &mUrmaJetty;
    jfc->mUrmaJfc = &mUrmaJfc;
    EXPECT_EQ(jetty->PollingCompletion(), UB_CQ_POLLING_FAILED);
    jfc->mUrmaJfc = nullptr;
    jetty->mUrmaJetty = nullptr;
}

TEST_F(TestUBPublicJetty, ReceiveFail)
{
    uint8_t buf;
    urma_jfc_t mUrmaJfc{};
    urma_jetty_t mUrmaJetty{};
    uint32_t pollCount = 0;
    MOCKER_CPP(&UBJfc::ProgressV).stubs().with(any(), outBound(pollCount))
        .will(returnValue(1));

    jetty->mUrmaJetty = &mUrmaJetty;
    jfc->mUrmaJfc = &mUrmaJfc;
    EXPECT_EQ(jetty->Receive(nullptr, 1), UB_PARAM_INVALID);
    EXPECT_EQ(jetty->Receive(&buf, 1), UB_ERROR);
    jfc->mUrmaJfc = nullptr;
    jetty->mUrmaJetty = nullptr;
}

UResult MockEventProgressV2(urma_cr_t *cr, uint32_t &countInOut, int32_t timeoutInMs = NN_NO500)
{
    countInOut = 1;
    cr->completion_len = 1;
    switch (timeoutInMs) {
        case NN_NO1000:
            cr->user_ctx = 0;
            break;
        case NN_NO2000:
            cr->user_ctx = reinterpret_cast<uintptr_t>(&tmpCtxInfo);
            tmpCtxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&tmpCtxInfo);
            break;
        default:
            break;
    }
    return UB_OK;
}

UResult MockProgressV(urma_cr_t *cr, uint32_t &countInOut)
{
    countInOut = 1;
    cr->completion_len = 1;
    cr->user_ctx = reinterpret_cast<uintptr_t>(&tmpCtxInfo);
    tmpCtxInfo.mrMemAddr = reinterpret_cast<uintptr_t>(&tmpCtxInfo);
    return UB_OK;
}

TEST_F(TestUBPublicJetty, Receive)
{
    uint8_t buf;
    urma_target_seg_t *tseg = nullptr;

    MOCKER_CPP(&UBJfc::ProgressV).stubs().will(invoke(MockProgressV));
    MOCKER_CPP(&UBPublicJetty::GetMemorySeg).stubs().will(returnValue(tseg));
    MOCKER_CPP(&UBPublicJetty::CheckRecvResult).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBPublicJetty::PostReceive).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBFixedMemPool::ReturnBuffer).stubs().will(returnValue(true));
    MOCKER_CPP(&UBMemoryRegionFixedBuffer::ReturnBuffer).stubs().will(returnValue(true));

    EXPECT_EQ(jetty->Receive(&buf, 1), UB_ERROR);
    EXPECT_EQ(jetty->Receive(&buf, 1), UB_QP_POST_RECEIVE_FAILED);
    EXPECT_EQ(jetty->Receive(&buf, 1), UB_OK);
}

TEST_F(TestUBPublicJetty, Stop)
{
    urma_jetty_t tmpJetty{};
    urma_target_jetty_t tmpTargetJetty{};
    jetty->mUrmaJetty = &tmpJetty;
    jetty->mTargetJetty = &tmpTargetJetty;
    MOCKER(HcomUrma::ModifyJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::ModifyJfr).stubs().will(returnValue(0));
    MOCKER(HcomUrma::UnimportJetty).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER(HcomUrma::DeleteJetty).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_NO_FATAL_FAILURE(jetty->Stop());
    EXPECT_NO_FATAL_FAILURE(jetty->Stop());
    EXPECT_NO_FATAL_FAILURE(jetty->Stop());
}

TEST_F(TestUBPublicJetty, UBFixedMemPool)
{
    uintptr_t buf = 0;
    UBFixedMemPool *mempool = new (std::nothrow) UBFixedMemPool(NN_NO128, NN_NO64);
    EXPECT_EQ(mempool->GetFreeBuffer(buf), false);
    EXPECT_EQ(mempool->MakeFreeList(), UB_PARAM_INVALID);
    mempool->Initialize();
    EXPECT_EQ(mempool->ReturnBuffer(0), false);
    delete mempool;
}

TEST_F(TestUBPublicJetty, ProcessWorkerCompletion)
{
    UBOpContextInfo ctx{};
    MOCKER_CPP(&UBPublicJetty::NewRequest).stubs().will(returnValue(0));
    ctx.opType = UBOpContextInfo::OpType::RECEIVE;
    EXPECT_NO_FATAL_FAILURE(jetty->ProcessWorkerCompletion(&ctx));

    ctx.opType = UBOpContextInfo::OpType::SEND_RAW;
    EXPECT_NO_FATAL_FAILURE(jetty->ProcessWorkerCompletion(&ctx));
}

TEST_F(TestUBPublicJetty, CheckRecvResult)
{
    urma_cr_t wc{};
    uint32_t size = 0;
    UResult result = UB_OK;
    uint32_t pollCount = 0;
    int32_t timeoutInMs = 1;

    urma_jfc_t mUrmaJfc{};
    urma_jetty_t mUrmaJetty{};
    jetty->mUrmaJetty = &mUrmaJetty;
    jfc->mUrmaJfc = &mUrmaJfc;

    EXPECT_EQ(jetty->CheckRecvResult(wc, size, result, pollCount, timeoutInMs), UB_CQ_POLLING_FAILED);
    pollCount = 1;
    result = UB_ERROR;
    EXPECT_EQ(jetty->CheckRecvResult(wc, size, result, pollCount, timeoutInMs), UB_ERROR);
    result = UB_OK;
    wc.status = URMA_CR_LOC_LEN_ERR;
    EXPECT_EQ(jetty->CheckRecvResult(wc, size, result, pollCount, timeoutInMs), UB_CQ_WC_WRONG);
    wc.status = URMA_CR_SUCCESS;
    wc.completion_len = 1;
    EXPECT_EQ(jetty->CheckRecvResult(wc, size, result, pollCount, timeoutInMs), UB_CQ_WC_WRONG);
    size = 1;
    EXPECT_EQ(jetty->CheckRecvResult(wc, size, result, pollCount, timeoutInMs), UB_OK);

    jfc->mUrmaJfc = nullptr;
    jetty->mUrmaJetty = nullptr;
}

TEST_F(TestUBPublicJetty, ProcessPollingResult)
{
    urma_cr_t wc{};

    MOCKER_CPP(&UBPublicJetty::NewRequest).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(jetty->ProcessPollingResult(wc));

    UBOpContextInfo ctxInfo{};
    wc.user_ctx = reinterpret_cast<uint64_t>(&ctxInfo);
    EXPECT_NO_FATAL_FAILURE(jetty->ProcessPollingResult(wc));
}

TEST_F(TestUBPublicJetty, UBThreadPool)
{
    UBThreadPool *threadPool = new (std::nothrow) UBThreadPool(NN_NO3);
    ASSERT_NE(threadPool, nullptr);

    threadPool->Stop();
    threadPool->Initialize();
    threadPool->Submit([]() {NN_LOG_INFO("Run a test task");});
    threadPool->Submit([]() {
        NN_LOG_INFO("Run a std error task");
        throw std::runtime_error("Run a std error task");
    });
    threadPool->Submit([]() {
        NN_LOG_INFO("Run a unknown error task");
        throw NN_NO58;
    });
    sleep(NN_NO1);
    EXPECT_NO_FATAL_FAILURE(threadPool->Stop());
    if (threadPool != nullptr) {
        delete threadPool;
        threadPool = nullptr;
    }
}
}
}
#endif
