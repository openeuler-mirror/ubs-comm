/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "securec.h"

#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "transport_write_bw_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_WRITE_BW = 5;

int TransportWriteBwTest::DoPostWrite()
{
    PerfTestContext *ctx = GetPerfTestContext();
    // ctx->tposted[i+1] - ctx->tposted[i] 为一次write完成时间
    ock::hcom::UBSHcomNetTransRequest req;
    req.lAddress = mPostMrInfo.lAddress;
    req.rAddress = mPeerMrInfo.lAddress;
    req.lKey = mPostMrInfo.lKey;
    req.rKey = mPeerMrInfo.lKey;
    req.size = ctx->mSize;
    ctx->tposted[0] = MONOTONIC_TIME_NS();
    for (uint64_t i = 0; i < ctx->mIterations; ++i) {
        int res = mEp->PostWrite(req);
        if (res != 0) {
            LOG_ERROR("failed to send to server");
        }
    }
    return 0;
}

int TransportWriteBwTest::NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    mEp = ep;
    LOG_DEBUG("new connection from " << ipPort << " !");
    return 0;
}

int TransportWriteBwTest::RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    int result = 0;
    if (!mCfg.GetIsServer()) {
        // client
        if (memcpy_s(&mPeerMrInfo, sizeof(mPeerMrInfo), ctx.Message()->Data(), ctx.Message()->DataLen()) != 0) {
            LOG_ERROR("memcpy_s failed");
            return -1;
        }
        sem_post(&mSem);
        return result;
    }
    // server
    UBSHcomNetTransRequest rsp((void *)(&mPostMrInfo), sizeof(mPostMrInfo), 0);
    if ((result = mEp->PostSend(OP_CODE_WRITE_BW, rsp)) != 0) {
        LOG_ERROR("Failed to post message to data to server, result " << result);
    }
    return result;
}

int TransportWriteBwTest::OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    PerfTestContext *testCtx = GetPerfTestContext();
    testCtx->totrcnt++;
    if (testCtx->totrcnt == testCtx->mIterations) {
        testCtx->tposted[testCtx->mIterations] = MONOTONIC_TIME_NS();
        sem_post(&mSem);
    }
    return 0;
}

bool TransportWriteBwTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create UBSHcomNetDriver
    NewEpHandler funcNewEndpoint = bind(&TransportWriteBwTest::NewEndPoint, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    ReqRecvHandler funcReqReceived = bind(&TransportWriteBwTest::RequestReceived, this, std::placeholders::_1);
    OneSideDoneHandler funcOneSide = bind(&TransportWriteBwTest::OneSideDone, this, std::placeholders::_1);
    mHelper.RegisterNewEPHandler(funcNewEndpoint);
    mHelper.RegisterReqRecvHandler(funcReqReceived);
    mHelper.RegisterOneSideDoneHandler(funcOneSide);
    if (!mHelper.CreateNetDriver()) {
        goto ERROR_HANDLE;
    }

    if (!RegMemory()) {
        LOG_ERROR("register memory failed");
        goto ERROR_HANDLE;
    }
    // client connect to server
    if (!mCfg.GetIsServer()) {
        if (!Connect()) {
            LOG_ERROR("client connect failed");
            goto ERROR_HANDLE;
        }
        if (!ExchangeAddress()) {
            LOG_ERROR("client exchange address failed");
            goto ERROR_HANDLE;
        }
    }
    return true;

ERROR_HANDLE:
    mHelper.DestroyNetDriver();
    sem_destroy(&mSem);
    return false;
}

bool TransportWriteBwTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("create memoryRegion failed");
        return false;
    }
    return true;
}

bool TransportWriteBwTest::ExchangeAddress()
{
    if (mEp == nullptr) {
        LOG_ERROR("Exchange address failed, ep is nullptr!");
        return false;
    }
    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if (mEp->PostSend(OP_CODE_WRITE_BW, req) != 0) {
        LOG_ERROR("Failed to exchange address to data to server");
        return false;
    }
    sem_wait(&mSem);
    return true;
}

void TransportWriteBwTest::UnInitialize()
{
    if (mEp != nullptr) {
        mEp->Close();
        mEp.Set(nullptr);
    }

    mHelper.DestroyNetDriver();
    sem_destroy(&mSem);
}

bool TransportWriteBwTest::Connect()
{
    auto driver = mHelper.GetNetDriver();
    if (driver == nullptr) {
        LOG_ERROR("connect failed, net driver is nullptr!");
        return false;
    }
    int res = driver->Connect(mCfg.GetOobIp(), mCfg.GetOobPort(), "xx", mEp, 0);
    if (res != 0) {
        LOG_ERROR("connect failed, error code: " << res);
        return false;
    }
    return true;
}

bool TransportWriteBwTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    if (!mCfg.GetIsServer()) {
        DoPostWrite();
    }
    // 等待测试结束
    sem_wait(&mSem);
    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::TRANSPORT_WRITE_BW, TransportWriteBwTest);
}
}